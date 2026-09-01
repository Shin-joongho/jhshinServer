#pragma once

#include "RSDefine.h"
#include "SocketUtill.h"

class SessionData
{
public:
	SessionData() {}
	~SessionData() {}

	NetAddress& GetNetAddr() { return m_NetAddress; }
	void SetNetAddr( sockaddr_in& RemoteSockAddr );

	SOCKET GetSocket() { return m_Socket; }
	void SetSocket( SOCKET Socket )
	{
		m_Socket = Socket;
	}

private:
	SOCKET m_Socket;
	NetAddress m_NetAddress;
};

