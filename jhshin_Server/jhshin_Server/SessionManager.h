#pragma once

#include "ObjectPool.h"
#include "SessionData.h"
#include "ListenManager.h"

class SessionManager : public SingleT< SessionManager >
{
public:
	SessionManager();
	~SessionManager();

	void Initialize( int sessionCount );

	SessionDataRef PopSession();
	void PushSession( SessionData* session );

	void InsertWait( AcceptObject* acceptObject );
	void ReWaiting();

	// 종료 1단계 - 대기 큐에 남은 AcceptObject 정리.
	void ClearWaitQueue();

	int GetSessionCount();
	int GetSessionMaxCount() { return m_SessionPools.GetMaxCount(); }

private:
	ObjectPool<SessionData> m_SessionPools;

	// 대기 큐
	mutex m_WaitLock;
	queue<AcceptObject*> m_WaitQueue;
};

