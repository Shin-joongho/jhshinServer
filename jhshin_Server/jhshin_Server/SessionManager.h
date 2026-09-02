#pragma once

#include "ObjectPool.h"
#include "SessionData.h"
#include <unordered_map>

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

private:
	inline static SessionManager* m_SessionManager;

	ObjectPool<SessionData> m_SessionPools;
};

