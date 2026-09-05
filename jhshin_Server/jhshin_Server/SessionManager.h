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

	SessionData* PopSession();
	void PushSession( SessionData* session );

	void InsertWait( AcceptObject* acceptObject );

private:
	inline static SessionManager* m_SessionManager;
	ObjectPool<SessionData> m_SessionPools;

	// ´ë±â Å¥
	mutex m_WaitLock;
	queue<AcceptObject*> m_WaitQueue;
};

