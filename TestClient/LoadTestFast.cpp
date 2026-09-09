// jhshin_Server 부하 테스트 - 경량 클라이언트
//
// 빌드 : g++ -std=c++17 -O2 LoadTestFast.cpp -o LoadTestFast.exe -lws2_32
// 실행 : LoadTestFast.exe [연결수] [지속초] [윈도우] [바디바이트] [스레드수]
//
// LoadTest.cpp 와의 차이 (클라이언트 CPU 를 줄이는 것이 목적):
//   1. 연결마다 스레드를 두지 않는다. 스레드 하나가 여러 연결을 논블로킹 + WSAPoll 로 돌린다.
//      -> 컨텍스트 스위치가 패킷당이 아니라 poll 당으로 줄어든다.
//   2. recv 를 큰 버퍼로 한 번 받고 그 안에서 여러 패킷을 파싱한다.
//      -> 왕복당 recv 2회(헤더/바디)가 아니라, 여러 왕복당 recv 1회.
//   3. send 도 여러 패킷을 모아 한 번에 보낸다.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// 프로토콜 정의는 서버 헤더를 그대로 쓴다. 규격을 두 곳에 두면 반드시 어긋난다.
//   g++ -std=c++17 -O2 -I <서버 소스 경로> LoadTestFast.cpp -o LoadTestFast.exe -lws2_32
#include "PacketStruct.h"

#pragma comment( lib, "ws2_32" )

using int64 = long long;

static_assert( sizeof( PacketID ) == 4, "PacketID 레이아웃이 바뀌었다" );

static const char*          SERVER_IP   = "127.0.0.1";
static const unsigned short SERVER_PORT = 27130;
static const size_t         MAX_SAMPLES = 50000;
static const size_t         IN_CAP      = 256 * 1024;

// 서버 핸들러가 sizeof( Client_ECHO_Req ) 와 정확히 일치하는 바디만 받는다.
static const int ECHO_BODY_SIZE = (int)sizeof( Client_ECHO_Req );

static std::atomic<int>                      g_arrived{ 0 };
static std::atomic<bool>                     g_go{ false };
static std::chrono::steady_clock::time_point g_deadline;

static int64 NowNanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch() )
        .count();
}

static double ProcessCpuSeconds()
{
    FILETIME c = {}, e = {}, k = {}, u = {};
    if( FALSE == GetProcessTimes( GetCurrentProcess(), &c, &e, &k, &u ) )
    {
        return 0.0;
    }

    ULARGE_INTEGER kk = {}, uu = {};
    kk.LowPart = k.dwLowDateTime;  kk.HighPart = k.dwHighDateTime;
    uu.LowPart = u.dwLowDateTime;  uu.HighPart = u.dwHighDateTime;
    return ( kk.QuadPart + uu.QuadPart ) / 10000000.0;
}

static SOCKET ConnectServer()
{
    SOCKET s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( INVALID_SOCKET == s )
    {
        return INVALID_SOCKET;
    }

    BOOL nodelay = TRUE;
    setsockopt( s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof( nodelay ) );

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons( SERVER_PORT );
    inet_pton( AF_INET, SERVER_IP, &addr.sin_addr );

    if( SOCKET_ERROR == connect( s, (sockaddr*)&addr, sizeof( addr ) ) )
    {
        closesocket( s );
        return INVALID_SOCKET;
    }

    u_long nb = 1;
    ioctlsocket( s, FIONBIO, &nb );   // 접속 후 논블로킹으로 전환

    return s;
}

struct Conn
{
    SOCKET      s      = INVALID_SOCKET;
    std::string in;                       // 수신 누적 버퍼
    size_t      inUsed = 0;
    std::string out;                      // 송신 대기 버퍼
    size_t      outOff = 0;
    bool        dead   = false;
};

struct Result
{
    long long          roundTrips = 0;
    int                connected  = 0;
    int                broke      = 0;
    std::vector<int64> rtts;
};

// out 버퍼 뒤에 패킷 하나를 붙인다. 바디 앞 8바이트는 송신 시각.
static void AppendPacket( std::string& out, int bodySize )
{
    PacketID h;
    h._type = PacketType::PacketType_CLIENT_ECHO;
    h._size = (uint16)bodySize;

    const size_t base = out.size();
    out.resize( base + sizeof( PacketID ) + bodySize, 'x' );

    memcpy( &out[base], &h, sizeof( h ) );

    const int64 now = NowNanos();
    memcpy( &out[base + sizeof( PacketID )], &now, sizeof( now ) );
}

static void Worker( int connCount, int window, int bodySize, Result* out )
{
    std::vector<Conn> conns( connCount );

    for( int i = 0; i < connCount; ++i )
    {
        conns[i].s = ConnectServer();
        if( INVALID_SOCKET == conns[i].s )
        {
            conns[i].dead = true;
            continue;
        }
        conns[i].in.resize( IN_CAP );
        conns[i].out.reserve( (size_t)window * ( sizeof( PacketID ) + bodySize ) * 2 );
        ++out->connected;
    }

    g_arrived.fetch_add( 1 );
    while( false == g_go.load( std::memory_order_acquire ) )
    {
        std::this_thread::yield();
    }

    // 초기 윈도우 채우기
    for( Conn& c : conns )
    {
        if( c.dead ) continue;
        for( int w = 0; w < window; ++w )
        {
            AppendPacket( c.out, bodySize );
        }
    }

    out->rtts.reserve( 8192 );

    std::vector<WSAPOLLFD> fds;
    std::vector<int>       idx;
    fds.reserve( connCount );
    idx.reserve( connCount );

    while( std::chrono::steady_clock::now() < g_deadline )
    {
        fds.clear();
        idx.clear();

        for( int i = 0; i < connCount; ++i )
        {
            Conn& c = conns[i];
            if( c.dead ) continue;

            WSAPOLLFD p = {};
            p.fd     = c.s;
            p.events = POLLRDNORM;
            if( c.outOff < c.out.size() )
            {
                p.events |= POLLWRNORM;
            }

            fds.push_back( p );
            idx.push_back( i );
        }

        if( fds.empty() )
        {
            break;
        }

        const int ready = WSAPoll( fds.data(), (ULONG)fds.size(), 100 );
        if( ready <= 0 )
        {
            continue;
        }

        for( size_t f = 0; f < fds.size(); ++f )
        {
            Conn& c = conns[idx[f]];
            const short re = fds[f].revents;

            if( re & ( POLLERR | POLLHUP | POLLNVAL ) )
            {
                c.dead = true;
                ++out->broke;
                continue;
            }

            // ---- 송신: 대기 중인 것을 한 번에 ----
            if( ( re & POLLWRNORM ) && c.outOff < c.out.size() )
            {
                const int n = send( c.s, c.out.data() + c.outOff,
                                    (int)( c.out.size() - c.outOff ), 0 );
                if( n > 0 )
                {
                    c.outOff += n;
                    if( c.outOff == c.out.size() )
                    {
                        c.out.clear();
                        c.outOff = 0;
                    }
                }
                else if( n == 0 || WSAGetLastError() != WSAEWOULDBLOCK )
                {
                    c.dead = true;
                    ++out->broke;
                    continue;
                }
            }

            // ---- 수신: 큰 버퍼로 한 번 받고 안에서 여러 패킷 파싱 ----
            if( re & POLLRDNORM )
            {
                const int n = recv( c.s, &c.in[c.inUsed], (int)( IN_CAP - c.inUsed ), 0 );
                if( n == 0 )
                {
                    c.dead = true;
                    ++out->broke;
                    continue;
                }
                if( n < 0 )
                {
                    if( WSAGetLastError() != WSAEWOULDBLOCK )
                    {
                        c.dead = true;
                        ++out->broke;
                    }
                    continue;
                }

                c.inUsed += n;

                size_t pos = 0;
                while( c.inUsed - pos >= sizeof( PacketID ) )
                {
                    PacketID h = {};
                    memcpy( &h, &c.in[pos], sizeof( h ) );

                    // 서버는 보낸 바디를 그대로 돌려준다. 크기가 다르면 스트림이 깨진 것.
                    if( h._size != (uint16)bodySize
                        || h._type != PacketType::PacketType_SERVER_ECHO )
                    {
                        printf( "[불일치] type=%u size=%u (기대 type=1 size=%d)\n",
                                (unsigned)h._type, (unsigned)h._size, bodySize );
                        c.dead = true;
                        ++out->broke;
                        break;
                    }

                    const size_t total = sizeof( PacketID ) + h._size;
                    if( c.inUsed - pos < total )
                    {
                        break;   // 아직 덜 왔다
                    }

                    int64 sentAt = 0;
                    memcpy( &sentAt, &c.in[pos + sizeof( PacketID )], sizeof( sentAt ) );

                    if( out->rtts.size() < MAX_SAMPLES )
                    {
                        out->rtts.push_back( NowNanos() - sentAt );
                    }

                    ++out->roundTrips;
                    pos += total;

                    AppendPacket( c.out, bodySize );   // 윈도우 유지
                }

                if( pos > 0 )
                {
                    memmove( &c.in[0], &c.in[pos], c.inUsed - pos );
                    c.inUsed -= pos;
                }
            }
        }
    }

    for( Conn& c : conns )
    {
        if( INVALID_SOCKET != c.s )
        {
            closesocket( c.s );
        }
    }
}

static double Percentile( const std::vector<int64>& sorted, double p )
{
    if( sorted.empty() ) return 0.0;
    return sorted[(size_t)( p * ( sorted.size() - 1 ) )] / 1000000.0;
}

int main( int argc, char** argv )
{
    SetConsoleOutputCP( CP_UTF8 );

    const int connections = ( argc > 1 ) ? atoi( argv[1] ) : 200;
    const int seconds     = ( argc > 2 ) ? atoi( argv[2] ) : 10;
    const int window      = ( argc > 3 ) ? atoi( argv[3] ) : 16;
    const int bodySize    = ECHO_BODY_SIZE;   // 서버 규격 고정
    const int threadCount = ( argc > 5 ) ? atoi( argv[5] ) : 8;

    if( threadCount < 1 || connections < threadCount )
    {
        printf( "인자 오류 (바디>=8, 스레드>=1, 연결수>=스레드수)\n" );
        return 1;
    }

    WSADATA wsa;
    if( 0 != WSAStartup( MAKEWORD( 2, 2 ), &wsa ) )
    {
        printf( "WSAStartup 실패\n" );
        return 1;
    }

    printf( "== 경량 클라이언트 부하 테스트 ==\n" );
    printf( "설정 : 연결 %d / 스레드 %d (스레드당 %d 연결) / 지속 %d초 / 윈도우 %d / 바디 %d bytes\n\n",
            connections, threadCount, connections / threadCount, seconds, window, bodySize );

    std::vector<Result>      results( threadCount );
    std::vector<std::thread> threads;
    threads.reserve( threadCount );

    for( int t = 0; t < threadCount; ++t )
    {
        int n = connections / threadCount;
        if( t == threadCount - 1 )
        {
            n += connections % threadCount;
        }
        threads.emplace_back( Worker, n, window, bodySize, &results[t] );
    }

    while( g_arrived.load() < threadCount )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }

    const auto   start    = std::chrono::steady_clock::now();
    const double cpuStart = ProcessCpuSeconds();
    g_deadline            = start + std::chrono::seconds( seconds );
    g_go.store( true, std::memory_order_release );

    for( auto& t : threads )
    {
        t.join();
    }

    const double elapsed =
        std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
    const double       cpuUsed = ProcessCpuSeconds() - cpuStart;
    const unsigned int cores   = std::thread::hardware_concurrency();

    long long          total     = 0;
    int                connected = 0;
    int                broke     = 0;
    std::vector<int64> allRtts;

    for( const Result& r : results )
    {
        total     += r.roundTrips;
        connected += r.connected;
        broke     += r.broke;
        allRtts.insert( allRtts.end(), r.rtts.begin(), r.rtts.end() );
    }

    std::sort( allRtts.begin(), allRtts.end() );

    double avg = 0.0;
    for( int64 v : allRtts ) avg += (double)v;
    if( false == allRtts.empty() ) avg = avg / allRtts.size() / 1000000.0;

    printf( "연결 성공 : %d / %d", connected, connections );
    if( broke > 0 ) printf( "   (중간 끊김 %d)", broke );
    printf( "\n" );

    printf( "총 왕복    : %lld\n", total );
    printf( "측정 구간  : %.2f 초\n", elapsed );
    printf( "처리량     : %.0f 왕복/초\n", elapsed > 0 ? total / elapsed : 0.0 );
    printf( "클라 CPU   : %.2f / %u 코어 (%.1f%%)\n",
            elapsed > 0 ? cpuUsed / elapsed : 0.0,
            cores,
            ( elapsed > 0 && cores > 0 ) ? ( cpuUsed / elapsed / cores * 100.0 ) : 0.0 );
    printf( "클라 효율  : %.0f 왕복/초/코어\n",
            ( cpuUsed > 0 ) ? total / cpuUsed : 0.0 );

    printf( "\n왕복 지연 (ms, 샘플 %zu)\n", allRtts.size() );
    printf( "  평균 %8.3f\n", avg );
    printf( "  p50  %8.3f\n", Percentile( allRtts, 0.50 ) );
    printf( "  p95  %8.3f\n", Percentile( allRtts, 0.95 ) );
    printf( "  p99  %8.3f\n", Percentile( allRtts, 0.99 ) );
    printf( "  최대 %8.3f\n", allRtts.empty() ? 0.0 : allRtts.back() / 1000000.0 );

    WSACleanup();
    return 0;
}
