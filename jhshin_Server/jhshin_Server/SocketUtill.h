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

};

class NetAddress
{
public:
	char* GetIP()
	{
		return m_IPBuffer;
	}
	int GetPort()
	{
		return m_Port;
	}

	void SetSockAddr_In( sockaddr_in addr )
	{
		m_addr = m_addr;
		InetNtopA( AF_INET, &m_addr.sin_addr, m_IPBuffer, sizeof( m_IPBuffer ) );
		m_Port = ntohs( m_addr.sin_port );
	}
	sockaddr_in& GetSockAddr_In()
	{
		return m_addr;
	}

private:
	sockaddr_in m_addr = {};
	char m_IPBuffer[INET_ADDRSTRLEN];
	u_short m_Port;
};