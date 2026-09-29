// jhshin_Server 이동 동기화 검증 - Room / Move 경로
//
// 빌드 : cl /nologo /utf-8 /std:c++20 /O2 /EHsc /I <서버소스경로> MoveTest.cpp /Fe:MoveTest.exe ws2_32.lib
// 실행 : MoveTest.exe
//
//   CLIENT_MOVE  -> Job_Move -> 룸 스레드 -> Room::Move (목적지 저장)
//                -> 룸 스레드의 주기 틱(MinMoveTick)마다 Room::MoveCheckUsers
//                -> 보간된 좌표를 Server_Move_Ack 로 묶어 룸 전원에게 전송
//
// 서버가 위치를 저장하지 않고 "출발 좌표 + 출발 시각 + 도착 시각"으로 계산하므로,
// 검증도 "받은 스냅샷이 시간에 비례해 목적지로 접근하는가"를 본다.
//
// 룸 배정이 무작위(rand() % 5)라 어느 룸에 들어갈지 고를 수 없다.
// 그래서 여러 개를 접속시킨 뒤 ack 의 roomID 로 묶어서
//   - 같은 룸의 관찰자
//   - 다른 룸의 관찰자
// 를 골라 쓴다.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

// 규격은 서버 헤더 하나만 본다. 두 곳에 두면 반드시 어긋난다.
#include "PacketStruct.h"

#pragma comment( lib, "ws2_32" )

static_assert( sizeof( PacketID ) == 4, "PacketID 레이아웃이 바뀌었다" );

static const char*          SERVER_IP   = "127.0.0.1";
static const unsigned short SERVER_PORT = 27130;
static const int            CLIENT_NUM  = 8;     // 룸 5개 기준, 비둘기집으로 같은 룸 쌍이 반드시 생긴다

static int g_pass = 0;
static int g_fail = 0;

// 재생용 기록. 검증과 별개로, 받은 스냅샷을 시간순으로 남겨 두면
// "언제 무엇을 받았는지"를 눈으로 확인할 수 있다.
struct Phase
{
    std::string name;
    double      t0;
    double      t1;
};

static std::vector<Phase> g_phases;

static void MarkPhase( const char* name, double t0, double t1 )
{
    g_phases.push_back( { name, t0, t1 } );
}

static double NowSec()
{
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch() )
        .count();
}

static void Check( bool ok, const char* name, const std::string& detail )
{
    if( ok )
    {
        ++g_pass;
        printf( "  [OK]   %-44s %s\n", name, detail.c_str() );
    }
    else
    {
        ++g_fail;
        printf( "  [FAIL] %-44s %s\n", name, detail.c_str() );
    }
}

struct MoveSample
{
    double       t;
    unsigned int seqID;
    float        x;
    float        y;
};

struct Client
{
    SOCKET            s      = INVALID_SOCKET;
    int               roomID = -1;
    unsigned int      seqID  = 0;
    bool              entered = false;
    std::vector<char> in;

    std::vector<MoveSample>   moves;
    std::vector<unsigned int> spawns;
    int                       leaves = 0;
};

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

    if( SOCKET_ERROR == connect( s, ( sockaddr* )&addr, sizeof( addr ) ) )
    {
        closesocket( s );
        return INVALID_SOCKET;
    }

    u_long nb = 1;
    ioctlsocket( s, FIONBIO, &nb );

    int one = 1;
    setsockopt( s, IPPROTO_TCP, TCP_NODELAY, ( const char* )&one, sizeof( one ) );

    return s;
}

static bool SendPacket( SOCKET s, PacketType type, const void* body, int bodySize )
{
    char buf[PACKET_SIZE] = {};

    PacketID header( type, ( uint16 )bodySize );
    memcpy( buf, &header, sizeof( header ) );
    if( 0 < bodySize )
    {
        memcpy( buf + sizeof( header ), body, bodySize );
    }

    const int total = ( int )sizeof( header ) + bodySize;
    int       sent  = 0;
    while( sent < total )
    {
        const int n = send( s, buf + sent, total - sent, 0 );
        if( 0 >= n )
        {
            if( WSAEWOULDBLOCK == WSAGetLastError() )
            {
                continue;
            }
            return false;
        }
        sent += n;
    }

    return true;
}

static bool SendMove( SOCKET s, float posX, float posY )
{
    Client_Move_Req req;
    req.Set( posX, posY );
    return SendPacket( s, PacketType::PacketType_CLIENT_MOVE, &req, sizeof( req ) );
}

// 받은 바이트에서 완성된 패킷만 꺼내 처리한다. 서버의 RecvBuffer 와 같은 방식이다.
static void ParseClient( Client& c )
{
    size_t read = 0;
    while( true )
    {
        if( c.in.size() - read < sizeof( PacketID ) )
        {
            break;
        }

        PacketID header;
        memcpy( &header, c.in.data() + read, sizeof( header ) );

        const size_t total = sizeof( PacketID ) + header._size;
        if( c.in.size() - read < total )
        {
            break;
        }

        const char* body     = c.in.data() + read + sizeof( PacketID );
        const int   bodySize = header._size;

        switch( header._type )
        {
        case PacketType::PacketType_SERVER_ENTER:
            {
                Server_Enter_Ack ack;
                if( bodySize == ( int )sizeof( ack ) )
                {
                    memcpy( &ack, body, bodySize );
                    c.roomID  = ack.GetRoomID();
                    c.seqID   = ack.GetSeqID();
                    c.entered = ( 0 <= c.roomID );
                }
            }
            break;

        case PacketType::PacketType_SERVER_SPAWN:
            {
                Server_Spawn spawn;
                if( 0 < bodySize && bodySize <= ( int )sizeof( spawn ) )
                {
                    memcpy( &spawn, body, bodySize );
                    for( int i = 0; i < spawn.GetCount(); ++i )
                    {
                        SpawnData* data = spawn.GetSpawnData( i );
                        if( nullptr != data )
                        {
                            c.spawns.push_back( data->m_seqID );
                        }
                    }
                }
            }
            break;

        case PacketType::PacketType_SERVER_MOVE:
            {
                Server_Move_Ack move;
                if( 0 < bodySize && bodySize <= ( int )sizeof( move ) )
                {
                    memcpy( &move, body, bodySize );
                    const double now = NowSec();
                    for( int i = 0; i < move.GetCount(); ++i )
                    {
                        MoveData* data = move.GetMoveData( i );
                        if( nullptr != data )
                        {
                            c.moves.push_back( { now, data->m_seqID, data->m_posX, data->m_posY } );
                        }
                    }
                }
            }
            break;

        case PacketType::PacketType_SERVER_LEAVE:
            ++c.leaves;
            break;

        default:
            break;
        }

        read += total;
    }

    if( 0 < read )
    {
        c.in.erase( c.in.begin(), c.in.begin() + read );
    }
}

// seconds 동안 모든 연결에서 수신만 한다.
static void Pump( std::vector<Client>& clients, double seconds )
{
    const double end = NowSec() + seconds;
    char         buf[8192];

    while( NowSec() < end )
    {
        for( Client& c : clients )
        {
            while( true )
            {
                const int n = recv( c.s, buf, sizeof( buf ), 0 );
                if( 0 < n )
                {
                    c.in.insert( c.in.end(), buf, buf + n );
                    continue;
                }
                break;
            }

            ParseClient( c );
        }

        Sleep( 5 );
    }
}

static size_t CountMoves( const Client& c, unsigned int seqID, double sinceSec )
{
    size_t count = 0;
    for( const MoveSample& m : c.moves )
    {
        if( m.seqID == seqID && m.t >= sinceSec )
        {
            ++count;
        }
    }
    return count;
}

static bool LastMove( const Client& c, unsigned int seqID, MoveSample& out )
{
    bool found = false;
    for( const MoveSample& m : c.moves )
    {
        if( m.seqID == seqID )
        {
            out   = m;
            found = true;
        }
    }
    return found;
}

// 재생 페이지가 읽을 수 있도록 기록을 남긴다.
//   C,클라 번호,룸,seqID
//   P,구간 이름,시작,끝
//   M,클라 번호,시각,seqID,x,y
// dumpIdx 에 든 연결의 수신만 남긴다. 군집 모드에서는 룸마다 한 명만 남겨도
// 그 룸에서 무엇이 보였는지 전부 재현된다. 전원을 남기면 기록이 연결 수의 제곱으로 커진다.
static void WriteLog( const std::vector<Client>& clients, double testStart, const char* path,
                      const std::vector<int>& dumpIdx )
{
    FILE* fp = nullptr;
    if( 0 != fopen_s( &fp, path, "w" ) || nullptr == fp )
    {
        printf( "기록 파일을 열지 못했다 : %s\n", path );
        return;
    }

    fprintf( fp, "# moveSpeed=%.3f tickMs=%d\n", MoveSpeed, MinMoveTick );

    for( size_t i = 0; i < clients.size(); ++i )
    {
        fprintf( fp, "C,%zu,%d,%u\n", i, clients[i].roomID, clients[i].seqID );
    }

    for( const Phase& p : g_phases )
    {
        fprintf( fp, "P,%s,%.3f,%.3f\n", p.name.c_str(), p.t0 - testStart, p.t1 - testStart );
    }

    size_t rows = 0;
    for( int i : dumpIdx )
    {
        for( const MoveSample& m : clients[i].moves )
        {
            fprintf( fp, "M,%d,%.3f,%u,%.4f,%.4f\n", i, m.t - testStart, m.seqID, m.x, m.y );
            ++rows;
        }
    }

    fclose( fp );
    printf( "기록 저장 : %s (%zu 행)\n", path, rows );
}

// 여러 연결이 동시에 이동하는 모드.
// 각자 자기 주기로 새 목적지를 찍으므로, 이동 중 방향을 바꾸는 경로도 섞인다.
static int RunSwarm( int clientNum, double seconds )
{
    printf( "== 군집 이동 ==\n" );
    printf( "연결 %d / 지속 %.1f 초 / 이동 속도 %.2f / 틱 %d ms\n\n",
            clientNum, seconds, MoveSpeed, MinMoveTick );

    const double        testStart = NowSec();
    std::vector<Client> clients( clientNum );

    for( Client& c : clients )
    {
        c.s = ConnectServer();
        if( INVALID_SOCKET == c.s )
        {
            printf( "접속 실패. 서버가 떠 있는지 확인\n" );
            return 1;
        }
    }

    for( Client& c : clients )
    {
        SendPacket( c.s, PacketType::PacketType_CLIENT_ENTER, nullptr, 0 );
    }

    Pump( clients, 0.8 );
    MarkPhase( "입장", testStart, NowSec() );

    int entered = 0;
    std::map<int, std::vector<int>> byRoom;
    for( int i = 0; i < clientNum; ++i )
    {
        if( clients[i].entered )
        {
            ++entered;
            byRoom[clients[i].roomID].push_back( i );
        }
    }

    printf( "입장 %d/%d, 룸 %d 개\n", entered, clientNum, ( int )byRoom.size() );
    for( auto& room : byRoom )
    {
        printf( "  룸 %d : %zu 명\n", room.first, room.second.size() );
    }
    printf( "\n" );

    // 각자 다음 목적지를 찍을 시각을 흩어 둔다
    std::vector<double> nextAt( clientNum );
    for( int i = 0; i < clientNum; ++i )
    {
        nextAt[i] = NowSec() + ( rand() % 100 ) / 100.0;
    }

    // 도착한 뒤에 쉬었다가 다음 목적지를 찍도록, 직전 목적지를 들고 있는다.
    // 다음 요청까지의 간격 = 이동에 걸리는 시간 + 쉬는 시간
    std::vector<float> lastX( clientNum, 0.0f );
    std::vector<float> lastY( clientNum, 0.0f );

    const double moveStart = NowSec();
    const double endAt     = moveStart + seconds;
    int          sent      = 0;

    while( NowSec() < endAt )
    {
        const double now = NowSec();

        for( int i = 0; i < clientNum; ++i )
        {
            if( false == clients[i].entered || now < nextAt[i] )
            {
                continue;
            }

            // 0 ~ 3 안에서 목적지를 고른다. 속도 1.0 기준 한 번에 1~2 초 걸린다
            const float x = ( rand() % 3000 ) / 1000.0f;
            const float y = ( rand() % 3000 ) / 1000.0f;

            SendMove( clients[i].s, x, y );
            ++sent;

            const float dx     = x - lastX[i];
            const float dy     = y - lastY[i];
            const double travel = sqrtf( dx * dx + dy * dy ) / MoveSpeed;
            const double idle   = 0.5 + ( rand() % 100 ) / 100.0;   // 도착 후 0.5 ~ 1.5 초 정지

            nextAt[i] = now + travel + idle;
            lastX[i]  = x;
            lastY[i]  = y;
        }

        Pump( clients, 0.03 );
    }

    MarkPhase( "군집 이동", moveStart, NowSec() );

    // 마지막 이동이 끝나기를 기다린다. 가장 먼 이동(대각선 4.2)이 끝날 만큼 잡는다
    const double settle = NowSec();
    Pump( clients, 5.0 );
    MarkPhase( "정지까지 대기", settle, NowSec() );

    size_t received = 0;
    for( const Client& c : clients )
    {
        received += c.moves.size();
    }

    printf( "이동 요청 %d 건, 수신한 좌표 갱신 %zu 건 (전 연결 합계)\n", sent, received );

    // 룸마다 한 명씩만 기록에 남긴다
    std::vector<int> recorders;
    for( auto& room : byRoom )
    {
        recorders.push_back( room.second[0] );
    }

    WriteLog( clients, testStart, "swarm_log.csv", recorders );

    for( Client& c : clients )
    {
        if( INVALID_SOCKET != c.s )
        {
            closesocket( c.s );
        }
    }

    return 0;
}

int main( int argc, char** argv )
{
    WSADATA wsa = {};
    if( 0 != WSAStartup( MAKEWORD( 2, 2 ), &wsa ) )
    {
        printf( "WSAStartup 실패\n" );
        return 1;
    }

    // MoveTest.exe            - 검증 14 항목
    // MoveTest.exe swarm N 초 - 여러 연결이 동시에 이동
    if( 2 <= argc && 0 == strcmp( argv[1], "swarm" ) )
    {
        srand( ( unsigned int )GetTickCount64() );

        const int    num  = ( 3 <= argc ) ? atoi( argv[2] ) : 24;
        const double secs = ( 4 <= argc ) ? atof( argv[3] ) : 12.0;

        const int rc = RunSwarm( num, secs );
        WSACleanup();
        return rc;
    }

    printf( "== 이동 동기화 검증 ==\n" );
    printf( "서버 설정 : MoveSpeed %.2f / MinMoveTick %d ms / 맵 상한은 Room 멤버\n\n",
            MoveSpeed, MinMoveTick );

    const double        testStart = NowSec();
    std::vector<Client> clients( CLIENT_NUM );

    for( Client& c : clients )
    {
        c.s = ConnectServer();
        if( INVALID_SOCKET == c.s )
        {
            printf( "접속 실패. 서버가 떠 있는지 확인\n" );
            return 1;
        }
    }

    for( Client& c : clients )
    {
        SendPacket( c.s, PacketType::PacketType_CLIENT_ENTER, nullptr, 0 );
    }

    Pump( clients, 0.6 );
    MarkPhase( "입장", testStart, NowSec() );

    // 1. 입장
    {
        int entered = 0;
        for( const Client& c : clients )
        {
            if( c.entered )
            {
                ++entered;
            }
        }

        char detail[128];
        sprintf_s( detail, "%d/%d 입장", entered, CLIENT_NUM );
        Check( entered == CLIENT_NUM, "입장 응답 수신", detail );
    }

    // 같은 룸 쌍과 다른 룸 하나를 고른다
    int mover = -1, sameRoom = -1, otherRoom = -1;
    {
        std::map<int, std::vector<int>> byRoom;
        for( int i = 0; i < CLIENT_NUM; ++i )
        {
            if( clients[i].entered )
            {
                byRoom[clients[i].roomID].push_back( i );
            }
        }

        for( auto& room : byRoom )
        {
            if( 2 <= room.second.size() && -1 == mover )
            {
                mover    = room.second[0];
                sameRoom = room.second[1];
            }
        }

        for( auto& room : byRoom )
        {
            if( -1 != mover && room.first != clients[mover].roomID && -1 == otherRoom )
            {
                otherRoom = room.second[0];
            }
        }

        char detail[128];
        sprintf_s( detail, "룸 %d 개에 분산, 이동자 #%d(룸 %d)",
                   ( int )byRoom.size(), mover, ( -1 != mover ) ? clients[mover].roomID : -1 );
        Check( -1 != mover && -1 != sameRoom, "같은 룸 관찰자 확보", detail );
    }

    if( -1 == mover || -1 == sameRoom )
    {
        printf( "\n같은 룸 쌍을 못 만들어 이후 검사를 건너뛴다\n" );
        return 1;
    }

    Client&            A     = clients[mover];
    Client&            B     = clients[sameRoom];
    const unsigned int seqA  = A.seqID;

    // 2. 기본 이동 : 거리 1.0 -> MoveSpeed 기준 소요 시간
    const float  destX      = 0.6f;
    const float  destY      = 0.8f;
    const double expectSec  = 1.0 / MoveSpeed;

    const double moveStart = NowSec();
    SendMove( A.s, destX, destY );
    Pump( clients, expectSec + 0.6 );
    MarkPhase( "기본 이동", moveStart, NowSec() );

    {
        const size_t n = CountMoves( A, seqA, moveStart );
        char         detail[128];
        sprintf_s( detail, "스냅샷 %zu 개 (틱 %d ms 기준 기대 %d 개 내외)",
                   n, MinMoveTick, ( int )( expectSec * 1000 / MinMoveTick ) );
        Check( 3 <= n, "이동 중 스냅샷 수신", detail );
    }

    {
        MoveSample last = {};
        const bool found = LastMove( A, seqA, last );

        const float dx = ( found ? last.x : 0.0f ) - destX;
        const float dy = ( found ? last.y : 0.0f ) - destY;
        const float err = sqrtf( dx * dx + dy * dy );

        char detail[128];
        sprintf_s( detail, "마지막 좌표 (%.4f, %.4f), 오차 %.6f", found ? last.x : 0.0f, found ? last.y : 0.0f, err );
        Check( found && err < 0.001f, "목적지 도달", detail );
    }

    {
        // 좌표가 시간에 따라 목적지 방향으로만 진행했는지
        bool   monotonic = true;
        float  prev      = -1.0f;
        for( const MoveSample& m : A.moves )
        {
            if( m.seqID != seqA || m.t < moveStart )
            {
                continue;
            }

            if( m.x + 0.0001f < prev )
            {
                monotonic = false;
            }
            prev = m.x;
        }

        Check( monotonic, "좌표가 뒤로 가지 않음", "x 단조 증가" );
    }

    {
        MoveSample last = {};
        LastMove( A, seqA, last );

        const double took = last.t - moveStart;
        char         detail[128];
        sprintf_s( detail, "소요 %.3f 초 (기대 %.3f 초)", took, expectSec );
        Check( took > expectSec * 0.7 && took < expectSec * 1.5 + 0.2, "이동 소요 시간", detail );
    }

    // 3. 도착 후에는 스냅샷이 멈춘다
    {
        const double t0 = NowSec();
        Pump( clients, 0.5 );
        MarkPhase( "도착 후 정지", t0, NowSec() );

        const size_t n = CountMoves( A, seqA, t0 );
        char         detail[64];
        sprintf_s( detail, "도착 후 0.5초 동안 %zu 개", n );
        Check( 0 == n, "도착 후 스냅샷 중단", detail );
    }

    // 4. 같은 룸 관찰자도 이동을 본다
    {
        const size_t n = CountMoves( B, seqA, moveStart );
        char         detail[64];
        sprintf_s( detail, "관찰자 #%d 가 %zu 개 수신", sameRoom, n );
        Check( 3 <= n, "같은 룸 관찰자 수신", detail );
    }

    // 5. 다른 룸으로는 새지 않는다
    if( -1 != otherRoom )
    {
        const Client& C = clients[otherRoom];
        char          detail[96];
        sprintf_s( detail, "다른 룸 #%d 가 받은 이동 %zu 개", otherRoom, C.moves.size() );
        Check( C.moves.empty(), "다른 룸으로 누출 없음", detail );
    }

    // 6. 맵 경계 밖 요청은 거부
    {
        const double t0 = NowSec();
        SendMove( A.s, -5.0f, 0.0f );
        SendMove( A.s, 999999.0f, 0.0f );
        Pump( clients, 0.4 );
        MarkPhase( "경계 밖 요청", t0, NowSec() );

        const size_t n = CountMoves( A, seqA, t0 );
        char         detail[64];
        sprintf_s( detail, "요청 후 스냅샷 %zu 개", n );
        Check( 0 == n, "경계 밖 요청 거부", detail );
    }

    // 7. 제자리 이동 요청은 무시
    {
        const double t0 = NowSec();
        SendMove( A.s, destX, destY );
        Pump( clients, 0.4 );
        MarkPhase( "제자리 요청", t0, NowSec() );

        const size_t n = CountMoves( A, seqA, t0 );
        char         detail[64];
        sprintf_s( detail, "요청 후 스냅샷 %zu 개", n );
        Check( 0 == n, "제자리 이동 무시", detail );
    }

    // 8. NaN 좌표
    {
        const double t0 = NowSec();
        const float  nan = std::numeric_limits<float>::quiet_NaN();
        SendMove( A.s, nan, nan );
        Pump( clients, 0.4 );
        MarkPhase( "NaN 요청", t0, NowSec() );

        bool corrupted = false;
        for( const MoveSample& m : A.moves )
        {
            if( m.seqID == seqA && m.t >= t0 && ( 0 == isfinite( m.x ) || 0 == isfinite( m.y ) ) )
            {
                corrupted = true;
            }
        }

        const size_t n = CountMoves( A, seqA, t0 );
        char         detail[96];
        sprintf_s( detail, "스냅샷 %zu 개, NaN 좌표 %s", n, corrupted ? "포함됨" : "없음" );
        Check( false == corrupted, "NaN 좌표가 전파되지 않음", detail );
    }

    // 9. NaN 이후에도 정상 이동이 되는가 (오염 여부의 진짜 판정)
    {
        const double t0 = NowSec();
        SendMove( A.s, 1.2f, 1.6f );   // 현재 위치에서 거리 1.0
        Pump( clients, 1.0 / MoveSpeed + 0.6 );
        MarkPhase( "NaN 이후 정상 이동", t0, NowSec() );

        MoveSample last  = {};
        bool       found = false;
        for( const MoveSample& m : A.moves )
        {
            if( m.seqID == seqA && m.t >= t0 )
            {
                last  = m;
                found = true;
            }
        }

        const bool ok = found && 0 != isfinite( last.x ) && 0 != isfinite( last.y ) &&
                        fabsf( last.x - 1.2f ) < 0.001f && fabsf( last.y - 1.6f ) < 0.001f;

        char detail[128];
        sprintf_s( detail, "마지막 좌표 (%.4f, %.4f)", found ? last.x : 0.0f, found ? last.y : 0.0f );
        Check( ok, "NaN 이후 정상 이동 복구", detail );
    }

    // 10. 퇴장 통지
    {
        const int    before = B.leaves;
        const double t0     = NowSec();
        closesocket( A.s );
        A.s = INVALID_SOCKET;

        Pump( clients, 0.5 );
        MarkPhase( "퇴장", t0, NowSec() );

        char detail[64];
        sprintf_s( detail, "관찰자 수신 %d 건", B.leaves - before );
        Check( B.leaves > before, "퇴장 통지 수신", detail );
    }

    printf( "\n결과 : 통과 %d / 실패 %d\n", g_pass, g_fail );

    std::vector<int> all;
    for( int i = 0; i < CLIENT_NUM; ++i )
    {
        all.push_back( i );
    }
    WriteLog( clients, testStart, "movetest_log.csv", all );

    for( Client& c : clients )
    {
        if( INVALID_SOCKET != c.s )
        {
            closesocket( c.s );
        }
    }

    WSACleanup();
    return ( 0 == g_fail ) ? 0 : 1;
}
