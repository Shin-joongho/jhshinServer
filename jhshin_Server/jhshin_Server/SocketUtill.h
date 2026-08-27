#pragma once

#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#include <map>
#include <ws2tcpip.h>
#include <mswsock.h>

#pragma comment(lib, "ws2_32")

enum eSocketOption
{
	eSocketOption_NoDelay = 0,
	eSocketOption_ReUseAddr,
	eSocketOption_Linger,
	eSocketOption_KeepAlive,
};

class SocketUtill
{
public:
	static SOCKET MakeSocket();
	static bool SetOptions( SOCKET& socket, int OptionBit );

private:

};

