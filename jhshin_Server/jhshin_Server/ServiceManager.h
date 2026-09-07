#pragma once

#include "IOCP.h"
#include "RSDefine.h"
#include "SocketUtill.h"
#include "SingletonTemplate.h""
#include "ObjectPool.h"

class ServiceManager : public SingleT< ServiceManager >
{
public:
	ServiceManager() {}
	~ServiceManager() {}

	bool Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount );
	void Start();

	void AddIOCP( SessionData* session );

	bool InsertUserSession( SessionDataRef session );
	void EraseUserSession( SessionDataRef session );

	IOCP& GetIOCP() { return m_iocp; }

	void Join();

	void CloseSession( SessionDataRef session );

	tuple<SendChunk, bool> MakeSendPacket( const char* sendData, const int sendSize );
	SendBufferRef GetSendBuffer();
private:
	IOCP m_iocp;

	mutex m_Lock;
	unordered_map<SOCKET, SessionDataRef> m_UserSession;

	mutex m_SendLock;
	ObjectPool<SendBuffer> m_SendBuffer;
	SendBufferRef m_LastSendBuffer = nullptr;
};

