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

SessionDataRef SessionManager::PopSession()
{
	SessionData* session = m_SessionPools.Pop();
	if( nullptr == session )
	{
		return nullptr;
	}

	return SessionDataRef( session, []( SessionData* psession ) { SessionManager::This()->PushSession( psession ); } );
}

void SessionManager::PushSession( SessionData* session )
{
	if( session )
	{
		session->Reset();
		m_SessionPools.Push( session );
		ReWaiting();		
	}
}

void SessionManager::InsertWait( AcceptObject* acceptObject )
{
	{
		lock_guard<mutex> lg( m_WaitLock );

		m_WaitQueue.push( acceptObject );
	}

	// 넣는 사이 반납 확인
	ReWaiting();
}

void SessionManager::ReWaiting()
{
	AcceptObject* acceptObject = nullptr;
	{
		lock_guard<mutex> lg( m_WaitLock );
		if( m_WaitQueue.empty() )
		{
			return;
		}
	}
	
	SessionDataRef session = PopSession();
	if( nullptr == session )
	{
		return;
	}

	{
		lock_guard<mutex> lg( m_WaitLock );
		if( m_WaitQueue.empty() )
		{
			return;
		}
		acceptObject = m_WaitQueue.front();
		m_WaitQueue.pop();
	}

	acceptObject->SetSession( session );
	ListenManager::This()->Accept( acceptObject, false );
}

int SessionManager::GetSessionCount()
{
	return m_SessionPools.GetFreeCount();
}
