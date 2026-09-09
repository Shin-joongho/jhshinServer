// jhshin_Server 부하 테스트 - Room / Broadcast 경로
//
// 빌드 : cl /nologo /utf-8 /std:c++20 /O2 /EHsc /I <서버소스경로> LoadTestRoom.cpp /Fe:LoadTestRoom.exe ws2_32.lib
// 실행 : LoadTestRoom.exe verify
//        LoadTestRoom.exe load [연결수] [지속초] [초당송신/연결] [스레드수]
//
// LoadTestFast.cpp 는 ECHO(1:1) 경로를 잰다. 이 파일은 Room 경로를 잰다.
//
//   CLIENT_ENTER  -> RoomManager::PushJobByRooms -> 룸 스레드 -> Room::Enter
//                 -> SERVER_ENTER + Server_Enter_Ack(roomID) 회신
//   CLIENT_BROADCAST -> 룸 스레드 -> Room::BroadCast
//                 -> "같은 룸의 자신을 제외한" 모든 세션에게 1개씩
//
// 서버가 룸을 무작위로 배정하므로(PacketHandler.cpp, rand() % 4) 연결마다
// 어느 룸에 들어갔는지 ack 로 받아서 기록한다. 기대 유출은 룸별로 따로 계산해야 한다.
//   기대 유출 = Σ(룸 r 에서 보낸 수 x (룸 r 의 멤버 수 - 1))
//
// ECHO 와 달리 1 요청이 N-1 응답으로 증폭되므로 왕복 개념이 없다.
// 송신은 "연결당 초당 rate 개"로 페이싱하고 수신은 따로 센다.
//
// 지연은 편도로 잰다. 모든 연결이 한 프로세스 안에 있어 steady_clock 이 공유되므로
// "A 가 보낸 시각"과 "B 가 받은 시각"의 차가 그대로 편도 지연이 된다.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// 규격은 서버 헤더 하나만 본다. 두 곳에 두면 반드시 어긋난다.
#include "PacketStruct.h"

#pragma comment( lib, "ws2_32" )
#pragma comment( lib, "winmm" )

using int64 = long long;

static_assert( sizeof( PacketID ) == 4, "PacketID 레이아웃이 바뀌었다" );

static const char*          SERVER_IP   = "127.0.0.1";
static const unsigned short SERVER_PORT = 27130;
static const size_t         MAX_SAMPLES = 200000;
static const size_t         IN_CAP      = 1024 * 1024;
static const int            MAX_ROOMS   = 64;

static const int BROAD_BODY     = (int)sizeof( Client_Broadcast_Req );   // 64
static const int ENTER_ACK_BODY = (int)sizeof( Server_Enter_Ack );       // 4

static const PacketType BROAD_ACK_TYPE = PacketType::PacketType_SERVER_BROADCAST;

static std::atomic<int>                      g_ready{ 0 };
static std::atomic<bool>                     g_go{ false };
static std::atomic<int>                      g_roomMembers[MAX_ROOMS];
static int                                   g_totalConns = 0;
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

static SOCKET ConnectServer( bool nonBlocking )
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

    if( nonBlocking )
    {
        u_long nb = 1;
        ioctlsocket( s, FIONBIO, &nb );
    }

    return s;
}

// 헤더만 있는 패킷 (ENTER / LEAVE 는 바디를 읽지 않는다)
static void AppendHeaderOnly( std::string& out, PacketType type )
{
    PacketID h;
    h._type = type;
    h._size = 0;

    const size_t base = out.size();
    out.resize( base + sizeof( PacketID ) );
    memcpy( &out[base], &h, sizeof( h ) );
}

// BROADCAST 패킷. 바디 앞 8바이트에 송신 시각, 그 뒤 4바이트에 보낸 연결 id.
static void AppendBroadcast( std::string& out, int connId )
{
    PacketID h;
    h._type = PacketType::PacketType_CLIENT_BROADCAST;
    h._size = (uint16)BROAD_BODY;

    const size_t base = out.size();
    out.resize( base + sizeof( PacketID ) + BROAD_BODY, 'x' );
    memcpy( &out[base], &h, sizeof( h ) );

    const int64 now = NowNanos();
    memcpy( &out[base + sizeof( PacketID )], &now, sizeof( now ) );
    memcpy( &out[base + sizeof( PacketID ) + 8], &connId, sizeof( connId ) );
}

// ---------------------------------------------------------------- verify

static bool RecvExact( SOCKET s, char* buf, int need, int timeoutMs )
{
    int got = 0;
    while( got < need )
    {
        fd_set rd;
        FD_ZERO( &rd );
        FD_SET( s, &rd );

        timeval tv;
        tv.tv_sec  = timeoutMs / 1000;
        tv.tv_usec = ( timeoutMs % 1000 ) * 1000;

        if( select( 0, &rd, nullptr, nullptr, &tv ) <= 0 )
        {
            return false;
        }

        const int n = recv( s, buf + got, need - got, 0 );
        if( n <= 0 )
        {
            return false;
        }
        got += n;
    }
    return true;
}

static bool HasPending( SOCKET s, int timeoutMs )
{
    fd_set rd;
    FD_ZERO( &rd );
    FD_SET( s, &rd );

    timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = ( timeoutMs % 1000 ) * 1000;

    return select( 0, &rd, nullptr, nullptr, &tv ) > 0;
}

static int Verify()
{
    const int N = 16;
    printf( "== Room 경로 정확성 검사 (연결 %d) ==\n\n", N );

    std::vector<SOCKET> s( N );
    std::vector<int>    room( N, -1 );

    for( int i = 0; i < N; ++i )
    {
        s[i] = ConnectServer( false );
        if( INVALID_SOCKET == s[i] )
        {
            printf( "[실패] 연결 %d 접속 불가\n", i );
            return 1;
        }
    }
    printf( "1) 접속 %d/%d ... OK\n", N, N );

    // --- ENTER 후 ack 에서 roomID 를 받는다 ---
    for( int i = 0; i < N; ++i )
    {
        std::string out;
        AppendHeaderOnly( out, PacketType::PacketType_CLIENT_ENTER );
        send( s[i], out.data(), (int)out.size(), 0 );
    }

    std::vector<int> members( MAX_ROOMS, 0 );
    for( int i = 0; i < N; ++i )
    {
        char hdr[sizeof( PacketID )] = {};
        if( false == RecvExact( s[i], hdr, sizeof( hdr ), 3000 ) )
        {
            printf( "2) [실패] 연결 %d ENTER ack timeout\n", i );
            return 1;
        }

        PacketID h = {};
        memcpy( &h, hdr, sizeof( h ) );
        if( h._type != PacketType::PacketType_SERVER_ENTER || h._size != ENTER_ACK_BODY )
        {
            printf( "2) [실패] 연결 %d ack type=%u size=%u (기대 type=%u size=%d)\n",
                    i, (unsigned)h._type, (unsigned)h._size,
                    (unsigned)PacketType::PacketType_SERVER_ENTER, ENTER_ACK_BODY );
            return 1;
        }

        char body[ENTER_ACK_BODY] = {};
        if( false == RecvExact( s[i], body, ENTER_ACK_BODY, 3000 ) )
        {
            printf( "2) [실패] 연결 %d ack 바디 timeout\n", i );
            return 1;
        }

        memcpy( &room[i], body, sizeof( int ) );
        if( room[i] < 0 || room[i] >= MAX_ROOMS )
        {
            printf( "2) [실패] 연결 %d roomID=%d 범위 밖\n", i, room[i] );
            return 1;
        }
        ++members[room[i]];
    }

    printf( "2) ENTER ack %d/%d, 룸 분포 :", N, N );
    for( int r = 0; r < MAX_ROOMS; ++r )
    {
        if( members[r] > 0 ) printf( " room%d=%d", r, members[r] );
    }
    printf( " ... OK\n" );

    // --- 멤버 2명 이상인 룸을 골라 브로드캐스트 ---
    int target = -1;
    for( int r = 0; r < MAX_ROOMS; ++r )
    {
        if( members[r] >= 2 ) { target = r; break; }
    }
    if( target < 0 )
    {
        printf( "3) [건너뜀] 멤버 2명 이상인 룸이 없다\n" );
        for( int i = 0; i < N; ++i ) closesocket( s[i] );
        return 1;
    }

    int sender = -1;
    for( int i = 0; i < N; ++i )
    {
        if( room[i] == target ) { sender = i; break; }
    }

    const int64 marker = 0x1234567890ABCDEFLL;
    {
        std::string out;
        PacketID    h;
        h._type = PacketType::PacketType_CLIENT_BROADCAST;
        h._size = (uint16)BROAD_BODY;
        out.resize( sizeof( PacketID ) + BROAD_BODY, 'x' );
        memcpy( &out[0], &h, sizeof( h ) );
        memcpy( &out[sizeof( PacketID )], &marker, sizeof( marker ) );
        send( s[sender], out.data(), (int)out.size(), 0 );
    }

    int got = 0;
    for( int i = 0; i < N; ++i )
    {
        if( i == sender || room[i] != target ) continue;

        char hdr[sizeof( PacketID )] = {};
        if( false == RecvExact( s[i], hdr, sizeof( hdr ), 3000 ) )
        {
            printf( "3) [실패] 연결 %d (room%d) 수신 timeout\n", i, room[i] );
            return 1;
        }

        PacketID h = {};
        memcpy( &h, hdr, sizeof( h ) );
        if( h._type != BROAD_ACK_TYPE || h._size != BROAD_BODY )
        {
            printf( "3) [실패] 연결 %d type=%u size=%u (기대 type=%u size=%d)\n",
                    i, (unsigned)h._type, (unsigned)h._size,
                    (unsigned)BROAD_ACK_TYPE, BROAD_BODY );
            return 1;
        }

        char body[BROAD_BODY] = {};
        if( false == RecvExact( s[i], body, BROAD_BODY, 3000 ) )
        {
            printf( "3) [실패] 연결 %d 바디 timeout\n", i );
            return 1;
        }

        int64 back = 0;
        memcpy( &back, body, sizeof( back ) );
        if( back != marker )
        {
            printf( "3) [실패] 연결 %d 페이로드 불일치\n", i );
            return 1;
        }
        ++got;
    }
    printf( "3) room%d 에서 1건 송신 -> 같은 룸의 나머지 %d개 전부 수신, 페이로드 일치 ... OK\n",
            target, got );

    // --- 송신자 본인은 받지 않아야 한다 ---
    if( HasPending( s[sender], 300 ) )
    {
        printf( "4) [실패] 송신자 본인에게도 브로드캐스트가 돌아왔다\n" );
        return 1;
    }
    printf( "4) 송신자 본인은 수신 없음 ... OK\n" );

    // --- 다른 룸으로 새면 안 된다 ---
    int leaked = 0;
    for( int i = 0; i < N; ++i )
    {
        if( room[i] == target ) continue;
        if( HasPending( s[i], 50 ) )
        {
            printf( "5) [실패] 연결 %d (room%d) 이 room%d 의 브로드캐스트를 받았다\n",
                    i, room[i], target );
            ++leaked;
        }
    }
    if( leaked > 0 )
    {
        return 1;
    }
    printf( "5) 다른 룸(%d개 연결)으로 새지 않음 ... OK\n", N - members[target] );

    // --- 연속 송신 ---
    const int burst    = 100;
    const int expectEa = burst;
    {
        std::string out;
        for( int k = 0; k < burst; ++k )
        {
            AppendBroadcast( out, sender );
        }
        send( s[sender], out.data(), (int)out.size(), 0 );
    }

    for( int i = 0; i < N; ++i )
    {
        if( i == sender || room[i] != target ) continue;

        const int         total = expectEa * ( (int)sizeof( PacketID ) + BROAD_BODY );
        std::vector<char> buf( total );
        if( false == RecvExact( s[i], buf.data(), total, 5000 ) )
        {
            printf( "6) [실패] 연결 %d 연속 %d건 수신 timeout\n", i, burst );
            return 1;
        }

        for( int k = 0; k < expectEa; ++k )
        {
            PacketID h = {};
            memcpy( &h, &buf[(size_t)k * ( sizeof( PacketID ) + BROAD_BODY )], sizeof( h ) );
            if( h._type != BROAD_ACK_TYPE || h._size != BROAD_BODY )
            {
                printf( "6) [실패] 연결 %d, %d번째 패킷 깨짐 type=%u size=%u\n",
                        i, k, (unsigned)h._type, (unsigned)h._size );
                return 1;
            }
        }
    }
    printf( "6) 연속 %d건, 같은 룸 전 연결 프레이밍 정상 ... OK\n", burst );

    for( int i = 0; i < N; ++i )
    {
        closesocket( s[i] );
    }

    printf( "\n6/6 통과\n" );
    return 0;
}

// ---------------------------------------------------------------- load

struct Conn
{
    SOCKET            s          = INVALID_SOCKET;
    int               id         = 0;
    int               roomId     = -1;
    std::vector<char> in;
    size_t            inUsed     = 0;
    std::string       out;
    size_t            outOff     = 0;
    bool              dead       = false;
    int64             nextSendAt = 0;
};

struct Result
{
    long long          sent      = 0;
    long long          recved    = 0;
    long long          skipped   = 0;
    long long          polls     = 0;
    int                connected = 0;
    int                entered   = 0;
    int                broke     = 0;
    int                mismatch  = 0;
    long long          sentByRoom[MAX_ROOMS] = {};
    std::vector<int64> lat;
};

static void Worker( int connCount, int firstId, int ratePerSec, Result* out )
{
    std::vector<Conn> conns( connCount );

    for( int i = 0; i < connCount; ++i )
    {
        conns[i].id = firstId + i;
        conns[i].s  = ConnectServer( true );
        if( INVALID_SOCKET == conns[i].s )
        {
            conns[i].dead = true;
            continue;
        }
        conns[i].in.resize( IN_CAP );
        ++out->connected;
    }

    // --- ENTER 를 보내고 ack(roomID) 를 다 받을 때까지 기다린다 ---
    for( Conn& c : conns )
    {
        if( c.dead ) continue;

        std::string e;
        AppendHeaderOnly( e, PacketType::PacketType_CLIENT_ENTER );

        size_t off = 0;
        while( off < e.size() )
        {
            const int n = send( c.s, e.data() + off, (int)( e.size() - off ), 0 );
            if( n > 0 ) { off += n; continue; }
            if( WSAGetLastError() == WSAEWOULDBLOCK ) { continue; }
            c.dead = true;
            break;
        }
    }

    const auto ackDeadline = std::chrono::steady_clock::now() + std::chrono::seconds( 15 );
    const int  need        = out->connected;
    while( out->entered < need && std::chrono::steady_clock::now() < ackDeadline )
    {
        for( Conn& c : conns )
        {
            if( c.dead || c.roomId >= 0 ) continue;

            const int n = recv( c.s, c.in.data() + c.inUsed, (int)( IN_CAP - c.inUsed ), 0 );
            if( n > 0 )
            {
                c.inUsed += n;

                size_t pos = 0;
                while( c.inUsed - pos >= sizeof( PacketID ) )
                {
                    PacketID h = {};
                    memcpy( &h, c.in.data() + pos, sizeof( h ) );

                    const size_t total = sizeof( PacketID ) + h._size;
                    if( c.inUsed - pos < total ) break;

                    if( h._type == PacketType::PacketType_SERVER_ENTER
                        && h._size == ENTER_ACK_BODY )
                    {
                        int rid = -1;
                        memcpy( &rid, c.in.data() + pos + sizeof( PacketID ), sizeof( rid ) );
                        if( rid >= 0 && rid < MAX_ROOMS )
                        {
                            c.roomId = rid;
                            g_roomMembers[rid].fetch_add( 1 );
                            ++out->entered;
                        }
                    }
                    pos += total;
                }

                if( pos > 0 )
                {
                    memmove( c.in.data(), c.in.data() + pos, c.inUsed - pos );
                    c.inUsed -= pos;
                }
            }
            else if( n == 0 )
            {
                c.dead = true;
                ++out->broke;
            }
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }

    g_ready.fetch_add( 1 );
    while( false == g_go.load( std::memory_order_acquire ) )
    {
        std::this_thread::yield();
    }

    out->lat.reserve( 16384 );

    const int64 interval = ( ratePerSec > 0 ) ? ( 1000000000LL / ratePerSec ) : 0;
    const int64 startNs  = NowNanos();
    for( Conn& c : conns )
    {
        // 연결마다 위상을 흩어 동시 송신 폭주를 막는다.
        // ( interval * id ) % interval 은 항상 0 이라 분산이 안 된다. 전체 연결 수로 나눠야 한다.
        c.nextSendAt = startNs + ( ( g_totalConns > 0 ) ? ( interval * c.id ) / g_totalConns : 0 );
    }

    // 송신 버퍼가 이만큼 밀리면 서버가 못 받고 있는 것 -> 그 틱은 건너뛴다.
    const size_t OUT_LIMIT = 64 * ( sizeof( PacketID ) + BROAD_BODY );

    std::vector<WSAPOLLFD> fds;
    std::vector<int>       idx;
    fds.reserve( connCount );
    idx.reserve( connCount );

    while( std::chrono::steady_clock::now() < g_deadline )
    {
        ++out->polls;
        const int64 now = NowNanos();

        for( Conn& c : conns )
        {
            if( c.dead || c.roomId < 0 ) continue;

            while( interval > 0 && now >= c.nextSendAt )
            {
                if( c.out.size() - c.outOff < OUT_LIMIT )
                {
                    AppendBroadcast( c.out, c.id );
                    ++out->sent;
                    ++out->sentByRoom[c.roomId];
                }
                else
                {
                    ++out->skipped;
                }

                c.nextSendAt += interval;

                if( now - c.nextSendAt > interval * 8 )
                {
                    c.nextSendAt = now + interval;
                }
            }
        }

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

        if( fds.empty() ) break;

        const int ready = WSAPoll( fds.data(), (ULONG)fds.size(), 1 );
        if( ready <= 0 ) continue;

        for( size_t f = 0; f < fds.size(); ++f )
        {
            Conn&       c  = conns[idx[f]];
            const short re = fds[f].revents;

            if( re & ( POLLERR | POLLHUP | POLLNVAL ) )
            {
                c.dead = true;
                ++out->broke;
                continue;
            }

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

            if( re & POLLRDNORM )
            {
                const int n = recv( c.s, c.in.data() + c.inUsed,
                                    (int)( IN_CAP - c.inUsed ), 0 );
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
                    memcpy( &h, c.in.data() + pos, sizeof( h ) );

                    if( h._type != BROAD_ACK_TYPE || h._size != BROAD_BODY )
                    {
                        if( out->mismatch < 5 )
                        {
                            printf( "[불일치] conn %d type=%u size=%u (기대 type=%u size=%d)\n",
                                    c.id, (unsigned)h._type, (unsigned)h._size,
                                    (unsigned)BROAD_ACK_TYPE, BROAD_BODY );
                        }
                        ++out->mismatch;
                        c.dead = true;
                        ++out->broke;
                        break;
                    }

                    const size_t total = sizeof( PacketID ) + h._size;
                    if( c.inUsed - pos < total ) break;

                    int64 sentAt = 0;
                    memcpy( &sentAt, c.in.data() + pos + sizeof( PacketID ), sizeof( sentAt ) );

                    if( out->lat.size() < MAX_SAMPLES )
                    {
                        out->lat.push_back( NowNanos() - sentAt );
                    }

                    ++out->recved;
                    pos += total;
                }

                if( pos > 0 )
                {
                    memmove( c.in.data(), c.in.data() + pos, c.inUsed - pos );
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

static int Load( int connections, int seconds, int ratePerSec, int threadCount )
{
    if( threadCount < 1 || connections < threadCount )
    {
        printf( "인자 오류 (연결수 >= 스레드수 >= 1)\n" );
        return 1;
    }

    g_totalConns = connections;
    for( int r = 0; r < MAX_ROOMS; ++r )
    {
        g_roomMembers[r].store( 0 );
    }

    printf( "== Room 브로드캐스트 부하 테스트 ==\n" );
    printf( "설정 : 연결 %d / 스레드 %d / 지속 %d초 / 송신 %d회당초/연결 / 바디 %d B\n\n",
            connections, threadCount, seconds, ratePerSec, BROAD_BODY );

    std::vector<Result>      results( threadCount );
    std::vector<std::thread> threads;
    threads.reserve( threadCount );

    int assigned = 0;
    for( int t = 0; t < threadCount; ++t )
    {
        int n = connections / threadCount;
        if( t == threadCount - 1 )
        {
            n += connections % threadCount;
        }
        threads.emplace_back( Worker, n, assigned, ratePerSec, &results[t] );
        assigned += n;
    }

    while( g_ready.load() < threadCount )
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

    long long sent = 0, recved = 0, skipped = 0, polls = 0;
    int       connected = 0, entered = 0, broke = 0, mismatch = 0;
    long long sentByRoom[MAX_ROOMS] = {};
    std::vector<int64> lat;

    for( const Result& r : results )
    {
        sent      += r.sent;
        recved    += r.recved;
        skipped   += r.skipped;
        polls     += r.polls;
        connected += r.connected;
        entered   += r.entered;
        broke     += r.broke;
        mismatch  += r.mismatch;
        for( int i = 0; i < MAX_ROOMS; ++i ) sentByRoom[i] += r.sentByRoom[i];
        lat.insert( lat.end(), r.lat.begin(), r.lat.end() );
    }

    std::sort( lat.begin(), lat.end() );

    double avg = 0.0;
    for( int64 v : lat ) avg += (double)v;
    if( false == lat.empty() ) avg = avg / lat.size() / 1000000.0;

    // 기대 유출은 룸별로 계산해야 한다. 룸이 다르면 서로 안 받는다.
    long long expected = 0;
    for( int r = 0; r < MAX_ROOMS; ++r )
    {
        const int m = g_roomMembers[r].load();
        if( m > 1 )
        {
            expected += sentByRoom[r] * ( m - 1 );
        }
    }

    printf( "연결 성공 : %d / %d\n", connected, connections );
    printf( "룸 입장   : %d / %d   분포 :", entered, connected );
    for( int r = 0; r < MAX_ROOMS; ++r )
    {
        const int m = g_roomMembers[r].load();
        if( m > 0 ) printf( " room%d=%d", r, m );
    }
    printf( "\n" );

    if( broke > 0 )    printf( "중간 끊김 : %d\n", broke );
    if( mismatch > 0 ) printf( "패킷 불일치: %d\n", mismatch );
    if( skipped > 0 )  printf( "송신 스킵 : %lld  (클라 송신버퍼 포화)\n", skipped );

    printf( "\n측정 구간 : %.2f 초\n", elapsed );
    printf( "유입      : %lld  (%.0f/초)\n", sent, elapsed > 0 ? sent / elapsed : 0.0 );
    printf( "유출      : %lld  (%.0f/초)\n", recved, elapsed > 0 ? recved / elapsed : 0.0 );
    printf( "기대 유출 : %lld  -> 전달률 %.2f%%\n",
            expected, expected > 0 ? ( 100.0 * recved / expected ) : 0.0 );
    printf( "편도 MB/s : %.1f\n",
            elapsed > 0 ? ( recved * ( sizeof( PacketID ) + BROAD_BODY ) / elapsed / 1048576.0 ) : 0.0 );

    printf( "\n폴 루프   : %.0f 회/초/스레드 (반복당 %.3f ms)\n",
            elapsed > 0 ? polls / elapsed / threadCount : 0.0,
            polls > 0 ? elapsed * 1000.0 * threadCount / polls : 0.0 );
    printf( "클라 CPU  : %.2f / %u 코어 (%.1f%%)\n",
            elapsed > 0 ? cpuUsed / elapsed : 0.0,
            cores,
            ( elapsed > 0 && cores > 0 ) ? ( cpuUsed / elapsed / cores * 100.0 ) : 0.0 );

    printf( "\n편도 지연 (ms, 샘플 %zu)\n", lat.size() );
    printf( "  평균 %8.3f\n", avg );
    printf( "  p50  %8.3f\n", Percentile( lat, 0.50 ) );
    printf( "  p95  %8.3f\n", Percentile( lat, 0.95 ) );
    printf( "  p99  %8.3f\n", Percentile( lat, 0.99 ) );
    printf( "  최대 %8.3f\n", lat.empty() ? 0.0 : lat.back() / 1000000.0 );

    return 0;
}

int main( int argc, char** argv )
{
    SetConsoleOutputCP( CP_UTF8 );
    timeBeginPeriod( 1 );

    WSADATA wsa;
    if( 0 != WSAStartup( MAKEWORD( 2, 2 ), &wsa ) )
    {
        printf( "WSAStartup 실패\n" );
        return 1;
    }

    const std::string mode = ( argc > 1 ) ? argv[1] : "verify";

    int rc = 0;
    if( mode == "verify" )
    {
        rc = Verify();
    }
    else if( mode == "load" )
    {
        const int connections = ( argc > 2 ) ? atoi( argv[2] ) : 50;
        const int seconds     = ( argc > 3 ) ? atoi( argv[3] ) : 10;
        const int rate        = ( argc > 4 ) ? atoi( argv[4] ) : 20;
        const int threads     = ( argc > 5 ) ? atoi( argv[5] ) : 4;
        rc = Load( connections, seconds, rate, threads );
    }
    else
    {
        printf( "사용법: LoadTestRoom.exe verify\n" );
        printf( "        LoadTestRoom.exe load [연결수] [지속초] [초당송신/연결] [스레드수]\n" );
        rc = 1;
    }

    timeEndPeriod( 1 );
    WSACleanup();
    return rc;
}
