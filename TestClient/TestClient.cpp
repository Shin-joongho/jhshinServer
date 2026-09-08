// jhshin_Server 에코 왕복 테스트 클라이언트
//
// 빌드 (MSVC)  : cl /EHsc /std:c++17 TestClient.cpp
// 빌드 (MinGW) : g++ -std=c++17 TestClient.cpp -o TestClient.exe -lws2_32
//
// 서버를 먼저 띄운 뒤 실행한다.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#pragma comment( lib, "ws2_32" )

using uint16 = unsigned short;

// ---- 서버와 동일한 패킷 규격 (Buffer.h) --------------------------------

enum class PacketType : uint16
{
    PacketType_NULL = 0,
    PacketType_Server,
    PacketType_Client,
};

struct PacketID
{
    PacketType _type;
    uint16     _size;   // 헤더를 뺀 바디 크기
};

static_assert( sizeof( PacketID ) == 4, "PacketID 레이아웃이 서버와 다르다" );

static const char*          SERVER_IP   = "127.0.0.1";
static const unsigned short SERVER_PORT = 27130;

// ---- 소켓 헬퍼 ---------------------------------------------------------

static SOCKET ConnectServer()
{
    SOCKET s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( INVALID_SOCKET == s )
    {
        return INVALID_SOCKET;
    }

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

// send 는 부분 전송될 수 있으므로 전량 보낼 때까지 반복한다.
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

// recv 도 마찬가지. 요청한 바이트를 다 받을 때까지 반복한다.
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

static bool RecvPacket( SOCKET s, PacketID& header, std::string& body )
{
    if( false == RecvExact( s, (char*)&header, sizeof( header ) ) )
    {
        return false;
    }

    body.assign( header._size, 0 );

    if( header._size > 0 )
    {
        if( false == RecvExact( s, &body[0], header._size ) )
        {
            return false;
        }
    }

    return true;
}

static std::string MakePacket( const std::string& body )
{
    PacketID h;
    h._type = PacketType::PacketType_Client;
    h._size = (uint16)body.size();

    std::string out;
    out.append( (const char*)&h, sizeof( h ) );
    out.append( body );
    return out;
}

// ---- 테스트 -----------------------------------------------------------

// [1] 패킷 하나를 보내고 그대로 돌아오는지
static bool Test_Single()
{
    SOCKET s = ConnectServer();
    if( INVALID_SOCKET == s )
    {
        printf( "    접속 실패 (서버가 떠 있는지 확인)\n" );
        return false;
    }

    const std::string body = "Hello Echo";
    const std::string pkt  = MakePacket( body );

    bool ok = SendAll( s, pkt.data(), (int)pkt.size() );

    PacketID    h = {};
    std::string echo;
    ok = ok && RecvPacket( s, h, echo );

    printf( "    보냄 : \"%s\" (%d bytes)\n", body.c_str(), (int)body.size() );
    if( ok )
    {
        printf( "    받음 : \"%s\" (type=%u, size=%u)\n",
                echo.c_str(), (unsigned)h._type, (unsigned)h._size );
    }

    ok = ok && ( echo == body );

    closesocket( s );
    return ok;
}

// [2] 여러 패킷을 한 번의 send 로 밀어넣어 서버의 뭉침 분리를 검증
static bool Test_Batched( int count )
{
    SOCKET s = ConnectServer();
    if( INVALID_SOCKET == s )
    {
        printf( "    접속 실패\n" );
        return false;
    }

    std::vector<std::string> bodies;
    std::string              all;

    for( int i = 0; i < count; ++i )
    {
        char buf[64];
        snprintf( buf, sizeof( buf ), "packet-%02d", i );

        bodies.push_back( buf );
        all += MakePacket( buf );
    }

    printf( "    %d 개 패킷 %d bytes 를 send 한 번으로 전송\n", count, (int)all.size() );

    bool ok = SendAll( s, all.data(), (int)all.size() );

    for( int i = 0; ok && i < count; ++i )
    {
        PacketID    h = {};
        std::string echo;

        if( false == RecvPacket( s, h, echo ) )
        {
            printf( "    %d 번째 수신 실패\n", i );
            ok = false;
            break;
        }

        if( echo != bodies[i] )
        {
            printf( "    %d 번째 불일치 : 기대 \"%s\" / 실제 \"%s\"\n",
                    i, bodies[i].c_str(), echo.c_str() );
            ok = false;
            break;
        }
    }

    if( ok )
    {
        printf( "    %d 개 모두 순서대로 에코됨\n", count );
    }

    closesocket( s );
    return ok;
}

// [3] 패킷 하나를 헤더 중간에서 잘라 두 번에 나눠 보내 부분 수신을 검증
static bool Test_Split()
{
    SOCKET s = ConnectServer();
    if( INVALID_SOCKET == s )
    {
        printf( "    접속 실패\n" );
        return false;
    }

    const std::string body = "SplitAcrossTwoSends";
    const std::string pkt  = MakePacket( body );

    const int cut = 2;   // 4바이트 헤더의 중간

    printf( "    %d bytes 패킷을 %d / %d 로 나눠 전송 (사이 150ms)\n",
            (int)pkt.size(), cut, (int)pkt.size() - cut );

    bool ok = SendAll( s, pkt.data(), cut );
    std::this_thread::sleep_for( std::chrono::milliseconds( 150 ) );
    ok = ok && SendAll( s, pkt.data() + cut, (int)pkt.size() - cut );

    PacketID    h = {};
    std::string echo;
    ok = ok && RecvPacket( s, h, echo );

    if( ok )
    {
        printf( "    받음 : \"%s\"\n", echo.c_str() );
    }

    ok = ok && ( echo == body );

    closesocket( s );
    return ok;
}

// [4] 단일 연결 왕복 처리량
static bool Test_Throughput( int count )
{
    SOCKET s = ConnectServer();
    if( INVALID_SOCKET == s )
    {
        printf( "    접속 실패\n" );
        return false;
    }

    const std::string pkt = MakePacket( std::string( 64, 'x' ) );

    const auto start = std::chrono::steady_clock::now();

    for( int i = 0; i < count; ++i )
    {
        if( false == SendAll( s, pkt.data(), (int)pkt.size() ) )
        {
            printf( "    %d 회차 송신 실패\n", i );
            closesocket( s );
            return false;
        }

        PacketID    h = {};
        std::string echo;
        if( false == RecvPacket( s, h, echo ) )
        {
            printf( "    %d 회차 수신 실패\n", i );
            closesocket( s );
            return false;
        }
    }

    const double elapsed =
        std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();

    printf( "    %d 회 왕복 / %.3f 초  =>  %.0f 왕복/초 (평균 %.3f ms)\n",
            count, elapsed, count / elapsed, elapsed * 1000.0 / count );

    closesocket( s );
    return true;
}

// ---- main -------------------------------------------------------------

int main()
{
    SetConsoleOutputCP( CP_UTF8 );

    WSADATA wsa;
    if( 0 != WSAStartup( MAKEWORD( 2, 2 ), &wsa ) )
    {
        printf( "WSAStartup 실패\n" );
        return 1;
    }

    printf( "== jhshin_Server 에코 테스트 (%s:%u) ==\n\n", SERVER_IP, SERVER_PORT );

    int pass  = 0;
    int total = 0;

    printf( "[1] 단일 패킷 왕복\n" );
    ++total;
    if( Test_Single() ) { ++pass; printf( "    => PASS\n\n" ); } else { printf( "    => FAIL\n\n" ); }
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );

    printf( "[2] 여러 패킷 뭉쳐 보내기\n" );
    ++total;
    if( Test_Batched( 5 ) ) { ++pass; printf( "    => PASS\n\n" ); } else { printf( "    => FAIL\n\n" ); }
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );

    printf( "[3] 한 패킷 쪼개 보내기\n" );
    ++total;
    if( Test_Split() ) { ++pass; printf( "    => PASS\n\n" ); } else { printf( "    => FAIL\n\n" ); }
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );

    printf( "[4] 왕복 처리량\n" );
    ++total;
    if( Test_Throughput( 2000 ) ) { ++pass; printf( "    => PASS\n\n" ); } else { printf( "    => FAIL\n\n" ); }

    printf( "결과 : %d / %d 통과\n", pass, total );

    WSACleanup();
    return ( pass == total ) ? 0 : 1;
}
