#include "ServiceManager.h"

void ServiceManager::Initalize( int iThreadCount )
{
	m_UserSession.clear();
	m_iocp.Init( iThreadCount );
}

void ServiceManager::Start()
{
	m_iocp.Start();
}

void ServiceManager::AddIOCP( SessionData* session )
{
	m_iocp.AddIOCP( session->GetSocket() );

	InsertUserSession( session );
}

bool ServiceManager::InsertUserSession( SessionData* session )
{
	bool Result = false;

	if( session )
	{
		lock_guard<mutex> lockGuard( m_Lock );
		Result = m_UserSession.insert( make_pair( session->GetSocket(), session ) ).second;
	}

	return Result;
}

void ServiceManager::EraseUserSession( SessionData* session )
{
	if( session )
	{
		lock_guard<mutex> lockGuard( m_Lock );
		m_UserSession.erase( session->GetSocket() );
	}
}
