#pragma once

#include "ObjectPool.h"
#include "SessionData.h"
#include "ListenManager.h"

class SessionManager
{
public:
	static SessionManager* This()
	{
		if( nullptr == m_SessionManager )
		{
			m_SessionManager = new SessionManager();
		}

		return m_SessionManager;
	}

	SessionManager();
	~SessionManager();

	void Initalize( int sessionCount );

	SessionDataRef PopSession();
	void PushSession( SessionData* session );

	void InsertWait( AcceptObject* acceptObject );
	void ReWaiting();

	int GetSessionCount();

private:
	inline static SessionManager* m_SessionManager;
	ObjectPool<SessionData> m_SessionPools;

	// 대기 큐
	mutex m_WaitLock;
	queue<AcceptObject*> m_WaitQueue;
};

