#include "SocketUtill.h"

SOCKET SocketUtill::MakeSocket()
{
    WSADATA WSAData;
    SOCKET socket;
    int WSAStartupResult = WSAStartup( MAKEWORD( 2, 2 ), &WSAData );
    if( WSAStartupResult != 0 )
    {
        return false;
    }

    socket = WSASocket( AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED );
    if( socket == SOCKET_ERROR )
    {
        return false;
    }
}

bool SocketUtill::SetOptions( SOCKET& socket, int OptionBit )
{
    bool opt_val = true;

    if( OptionBit & 1 << eSocketOption_NoDelay )
    {
        setsockopt( socket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt_val, sizeof( opt_val ) );
    }

    if( OptionBit & 1 << eSocketOption_ReUseAddr )
    {
        setsockopt( socket, SOL_SOCKET, TCP_NODELAY, (char*)&opt_val, sizeof( opt_val ) );
    }

    if( OptionBit & 1 << eSocketOption_Linger )
    {
        LINGER linger{};
        linger.l_onoff = 1;
        linger.l_linger = 0;
        setsockopt( socket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof( linger ) );
    }

    if( OptionBit & 1 << eSocketOption_KeepAlive )
    {
        setsockopt( socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt_val, sizeof( opt_val ) );
    }
}
