#pragma once

#include "IOCP.h"
#include "SocketUtil.h"
#include "SingletonTemplate.h"
#include "ObjectPool.h"
#include "RoomManager.h"

#include "RSDefine.h"

class ServiceManager : public SingleT< ServiceManager >
{
public:
	ServiceManager() {}
	~ServiceManager() {}

	bool Initialize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount );
	void Start();

	void AddIOCP( SessionData* session );

	bool InsertUserSession( SessionDataRef session );
	void EraseUserSession( SessionDataRef session );

	IOCP& GetIOCP() { return m_iocp; }

	void Join();
	void Shutdown();

	void CloseSession( SessionDataRef session );

	tuple<SendChunk, bool> MakeSendPacket( PacketType packetType, const char* sendData, const int sendSize );
	SendBufferRef GetSendBuffer();

	void StartMonitor( int intervalSec );
	void StopMonitor();

private:
	static void MonitorLoop( ServiceManager* self, int intervalSec );

	// 모니터는 detach 하지 않는다.
	// detach 하면 Release() 로 이 객체가 사라진 뒤에도 스레드가 깨어나
	// self->m_Lock 을 잡으려 들어 use-after-free 가 된다.
	thread m_MonitorThread;
	HANDLE m_MonitorStop = nullptr;

	IOCP m_iocp;

	mutex m_Lock;
	unordered_map<SOCKET, SessionDataRef> m_UserSession;

	ObjectPool<SendBuffer> m_SendBuffer;
};

