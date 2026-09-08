#pragma once

#include "SocketUtill.h"
#include "IOCP.h"
#include "ServiceManager.h"
#include "PacketHandler.h"

class SessionData : public enable_shared_from_this<SessionData>
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

	RecvObject& GetRecvObject() { return m_Recv; }
	SendObject& GetSendObject() { return m_Send; }
	bool Recv( int transferByte );
	bool RecvStart();

	void InsertSendQueue( SendChunk sendChunk );
	bool Send();
	void CheckSendComplete();

	void Reset();

private:
	SOCKET m_Socket;
	NetAddress m_NetAddress;

	RecvObject m_Recv;

	mutex m_SendLock;
	queue<SendChunk> m_SendQueue;
	SendObject m_Send;
	bool SendFlag;
};

