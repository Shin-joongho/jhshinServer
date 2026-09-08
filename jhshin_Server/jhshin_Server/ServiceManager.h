#pragma once

#include "IOCP.h"
#include "RSDefine.h"
#include "SocketUtill.h"
#include "SingletonTemplate.h"
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

	// 청크 풀은 ObjectPool 내부 뮤텍스로 보호된다.
	// "현재 잘라 쓰는 청크"는 ServiceManager.cpp 의 thread_local 변수로 옮겼다.
	// 모든 송신이 공유 멤버 하나를 거치면 전역 락이 되어버리기 때문.
	ObjectPool<SendBuffer> m_SendBuffer;
};

