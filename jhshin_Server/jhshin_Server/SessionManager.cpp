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
		m_SessionPools.Push( session );
	}
}
