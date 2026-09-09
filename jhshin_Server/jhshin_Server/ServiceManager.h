#pragma once

#include "IOCP.h"
#include "SocketUtill.h"
#include "SingletonTemplate.h"
#include "ObjectPool.h"

#include "RSDefine.h"

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

	tuple<SendChunk, bool> MakeSendPacket( PacketType packetType, const char* sendData, const int sendSize );
	SendBufferRef GetSendBuffer();

	void StartMonitor( int intervalSec );

private:
	static void MonitorLoop( ServiceManager* self, int intervalSec );

	IOCP m_iocp;

	mutex m_Lock;
	unordered_map<SOCKET, SessionDataRef> m_UserSession;

	ObjectPool<SendBuffer> m_SendBuffer;
};

