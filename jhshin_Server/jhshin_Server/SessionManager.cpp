#include "SessionManager.h"


SessionManager::SessionManager()
{
}

SessionManager::~SessionManager()
{

}

void SessionManager::Initalize( int sessionCount )
{
	m_SessionPools.InitObjectPool( sessionCount );
}

SessionData* SessionManager::PopSession()
{
	return m_SessionPools.Pop();
}

void SessionManager::PushSession( SessionData* session )
{
	if( session )
	{
		session->Reset();
		AcceptObject* acceptObject = nullptr;

		{
			lock_guard<mutex> lg( m_WaitLock );

			if( m_WaitQueue.empty() )
			{
				m_SessionPools.Push( session );
			}
			else
			{

				{
					acceptObject = m_WaitQueue.front();
					m_WaitQueue.pop();
				}
			}
		}

		if( acceptObject )
		{
			acceptObject->SetSession( session );
			ListenManager::This()->Accept( acceptObject, false );
		}
		
	}
}

void SessionManager::InsertWait( AcceptObject* acceptObject )
{
	{
		lock_guard<mutex> lg( m_WaitLock );

		m_WaitQueue.push( acceptObject );
	}

	// 넣는 사이 반납 확인
	SessionData* session = m_SessionPools.Pop();
	if( session )
	{
		PushSession( session );
	}
}
