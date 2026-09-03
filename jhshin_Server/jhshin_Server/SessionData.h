#pragma once

#include "SocketUtill.h"
#include "IOCP.h"
#include "RSDefine.h"

class SessionData
{
public:
	SessionData()
	{
		Reset();
	}
	~SessionData() {}

	NetAddress& GetNetAddr() { return m_NetAddress; }
	void SetNetAddr( sockaddr_in& RemoteSockAddr );

	SOCKET GetSocket() { return m_Socket; }
	void SetSocket( SOCKET Socket )
	{
		m_Socket = Socket;
	}

	RecvObject* GetRecvObject() { return m_Recv;  }
	
	bool RecvStart();

	void Reset();

private:
	SOCKET m_Socket;
	NetAddress m_NetAddress;

	RecvObject* m_Recv;
};

