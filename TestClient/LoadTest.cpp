// jhshin_Server 부하 테스트
//
// 빌드 : g++ -std=c++17 -O2 LoadTest.cpp -o LoadTest.exe -lws2_32
// 실행 : LoadTest.exe [연결수] [지속초] [윈도우] [바디바이트]
//
// 연결마다 스레드 하나를 두고, 응답을 기다리지 않고 [윈도우] 개만큼
// 미리 보내둔 뒤 "응답 1개 수신 -> 1개 추가 송신"으로 윈도우를 유지한다.
// 바디 앞 8바이트에 송신 시각을 심어 에코로 돌아온 값으로 왕복 지연을 잰다.

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

#pragma comment( lib, "ws2_32" )

using uint16 = unsigned short;
using int64  = long long;

enum class PacketType : uint16
{
    PacketType_NULL = 0,
    PacketType_Server,
    PacketType_Client,
};

struct PacketID
{
    PacketType _type;
    uint16     _size;
};

static_assert( sizeof( PacketID ) == 4, "PacketID 레이아웃이 서버와 다르다" );

static const char*          SERVER_IP      = "127.0.0.1";
static const unsigned short SERVER_PORT    = 27130;
static const size_t         MAX_SAMPLES    = 50000;   // 스레드당 지연 샘플 상한

// 이 프로세스가 지금까지 쓴 CPU 시간(커널+유저)을 초 단위로 돌려준다.
static double ProcessCpuSeconds()
{
    FILETIME createTime = {};
    FILETIME exitTime   = {};
    FILETIME kernelTime = {};
    FILETIME userTime   = {};

    if( FALSE == GetProcessTimes( GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime ) )
    {
        return 0.0;
    }

    ULARGE_INTEGER k = {};
    ULARGE_INTEGER u = {};
    k.LowPart  = kernelTime.dwLowDateTime;
    k.HighPart = kernelTime.dwHighDateTime;
    u.LowPart  = userTime.dwLowDateTime;
    u.HighPart = userTime.dwHighDateTime;

    return ( k.QuadPart + u.QuadPart ) / 10000000.0;
}

static int64 NowNanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch() )
        .count();
}

static SOCKET ConnectServer()
{
    SOCKET s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( INVALID_SOCKET == s )
    {
        return INVALID_SOCKET;
    }

    // 파이프라이닝이 Nagle 에 막히지 않도록 클라이언트도 꺼둔다.
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

    return s;
}

static bool SendAll( SOCKET s, const char* data, int len )
{
    int sent = 0;
    while( sent < len )
    {
        int n = send( s, data + sent, len - sent, 0 );
        if( n <= 0 )
        {
            return false;
        }
        sent += n;
    }
    return true;
}

static bool RecvExact( SOCKET s, char* buf, int len )
{
    int got = 0;
    while( got < len )
    {
        int n = recv( s, buf + got, len - got, 0 );
        if( n <= 0 )
        {
            return false;
        }
        got += n;
    }
    return true;
}

struct Result
{
    long long          roundTrips = 0;
    bool               connected  = false;
    bool               brokeEarly = false;
    double             runSeconds = 0.0;   // 접속 완료 후 루프 종료까지
    std::vector<int64> rtts;
};

// 모든 연결이 접속을 마친 뒤 동시에 출발시키기 위한 배리어.
// 스레드를 순차 생성하면 먼저 뜬 스레드가 CPU 를 점유해 뒤쪽 생성이 늦어지고,
// 결과적으로 "N개 동시" 가 아니라 계단식 부하가 되어버린다.
static std::atomic<int>                      g_arrived{ 0 };
static std::atomic<bool>                     g_go{ false };
static std::chrono::steady_clock::time_point g_deadline;

static void Worker( int window, int bodySize, Result* out )
{
    SOCKET s = ConnectServer();
    if( INVALID_SOCKET == s )
    {
        g_arrived.fetch_add( 1 );
        return;
    }
    out->connected = true;
    out->rtts.reserve( 4096 );

    // 접속 완료 보고 후 출발 신호 대기
    g_arrived.fetch_add( 1 );
    while( false == g_go.load( std::memory_order_acquire ) )
    {
        std::this_thread::yield();
    }

    // 헤더 + 바디를 한 버퍼에 담아두고 매번 타임스탬프만 갈아끼운다.
    const int   packetSize = (int)sizeof( PacketID ) + bodySize;
    std::string packet( packetSize, 'x' );
    {
        PacketID h;
        h._type = PacketType::PacketType_Client;
        h._size = (uint16)bodySize;
        memcpy( &packet[0], &h, sizeof( h ) );
    }

    auto sendOne = [&]() -> bool
    {
        const int64 now = NowNanos();
        memcpy( &packet[sizeof( PacketID )], &now, sizeof( now ) );
        return SendAll( s, packet.data(), packetSize );
    };

    // 윈도우 채우기
    for( int i = 0; i < window; ++i )
    {
        if( false == sendOne() )
        {
            out->brokeEarly = true;
            closesocket( s );
            return;
        }
    }

    std::string body( bodySize, 0 );
    const auto  t0 = std::chrono::steady_clock::now();

    while( std::chrono::steady_clock::now() < g_deadline )
    {
        PacketID h = {};
        if( false == RecvExact( s, (char*)&h, sizeof( h ) ) )
        {
            out->brokeEarly = true;
            break;
        }

        if( h._size != bodySize )
        {
            out->brokeEarly = true;
            break;
        }

        if( false == RecvExact( s, &body[0], h._size ) )
        {
            out->brokeEarly = true;
            break;
        }

        int64 sentAt = 0;
        memcpy( &sentAt, &body[0], sizeof( sentAt ) );

        ++out->roundTrips;
        if( out->rtts.size() < MAX_SAMPLES )
        {
            out->rtts.push_back( NowNanos() - sentAt );
        }

        if( false == sendOne() )
        {
            out->brokeEarly = true;
            break;
        }
    }

    out->runSeconds =
        std::chrono::duration<double>( std::chrono::steady_clock::now() - t0 ).count();

    closesocket( s );
}

static double Percentile( const std::vector<int64>& sorted, double p )
{
    if( sorted.empty() )
    {
        return 0.0;
    }
    size_t idx = (size_t)( p * ( sorted.size() - 1 ) );
    return sorted[idx] / 1000000.0;   // ns -> ms
}

int main( int argc, char** argv )
{
    SetConsoleOutputCP( CP_UTF8 );

    const int connections = ( argc > 1 ) ? atoi( argv[1] ) : 100;
    const int seconds     = ( argc > 2 ) ? atoi( argv[2] ) : 10;
    const int window      = ( argc > 3 ) ? atoi( argv[3] ) : 8;
    const int bodySize    = ( argc > 4 ) ? atoi( argv[4] ) : 64;

    if( bodySize < 8 )
    {
        printf( "바디는 최소 8바이트여야 한다 (타임스탬프)\n" );
        return 1;
    }

    WSADATA wsa;
    if( 0 != WSAStartup( MAKEWORD( 2, 2 ), &wsa ) )
    {
        printf( "WSAStartup 실패\n" );
        return 1;
    }

    printf( "== jhshin_Server 부하 테스트 ==\n" );
    printf( "설정 : 연결 %d / 지속 %d초 / 윈도우 %d / 바디 %d bytes\n\n",
            connections, seconds, window, bodySize );

    std::vector<Result>      results( connections );
    std::vector<std::thread> threads;
    threads.reserve( connections );

    for( int i = 0; i < connections; ++i )
    {
        threads.emplace_back( Worker, window, bodySize, &results[i] );
    }

    // 전원 접속 완료까지 대기
    const auto waitStart = std::chrono::steady_clock::now();
    while( g_arrived.load() < connections )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );

        if( std::chrono::steady_clock::now() - waitStart > std::chrono::seconds( 60 ) )
        {
            printf( "접속 대기 시간 초과 (%d / %d)\n", g_arrived.load(), connections );
            break;
        }
    }

    const double setupSec =
        std::chrono::duration<double>( std::chrono::steady_clock::now() - waitStart ).count();

    // 동시 출발
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
    const double cpuUsed = ProcessCpuSeconds() - cpuStart;
    const unsigned int cores = std::thread::hardware_concurrency();

    long long          total     = 0;
    int                connected = 0;
    int                broke     = 0;
    double             runSum    = 0.0;
    double             runMax    = 0.0;
    std::vector<int64> allRtts;

    for( const Result& r : results )
    {
        total += r.roundTrips;
        if( r.connected )  ++connected;
        if( r.brokeEarly ) ++broke;
        runSum += r.runSeconds;
        runMax = ( r.runSeconds > runMax ) ? r.runSeconds : runMax;
        allRtts.insert( allRtts.end(), r.rtts.begin(), r.rtts.end() );
    }

    const double runAvg = ( connected > 0 ) ? runSum / connected : 0.0;

    std::sort( allRtts.begin(), allRtts.end() );

    double avg = 0.0;
    for( int64 v : allRtts )
    {
        avg += (double)v;
    }
    if( false == allRtts.empty() )
    {
        avg = avg / allRtts.size() / 1000000.0;
    }

    printf( "연결 성공 : %d / %d", connected, connections );
    if( broke > 0 )
    {
        printf( "   (중간 끊김 %d)", broke );
    }
    printf( "\n" );

    printf( "접속 준비  : %.2f 초\n", setupSec );
    printf( "총 왕복    : %lld\n", total );
    printf( "측정 구간  : %.2f 초 (설정 %d 초 / 스레드 평균 %.2f, 최대 %.2f)\n",
            elapsed, seconds, runAvg, runMax );
    printf( "처리량     : %.0f 왕복/초\n", elapsed > 0 ? total / elapsed : 0.0 );
    printf( "클라 CPU   : %.2f / %u 코어 (%.1f%%)\n",
            elapsed > 0 ? cpuUsed / elapsed : 0.0,
            cores,
            ( elapsed > 0 && cores > 0 ) ? ( cpuUsed / elapsed / cores * 100.0 ) : 0.0 );

    printf( "\n왕복 지연 (ms, 샘플 %zu)\n", allRtts.size() );
    printf( "  평균 %8.3f\n", avg );
    printf( "  p50  %8.3f\n", Percentile( allRtts, 0.50 ) );
    printf( "  p95  %8.3f\n", Percentile( allRtts, 0.95 ) );
    printf( "  p99  %8.3f\n", Percentile( allRtts, 0.99 ) );
    printf( "  최대 %8.3f\n", allRtts.empty() ? 0.0 : allRtts.back() / 1000000.0 );

    WSACleanup();
    return 0;
}
