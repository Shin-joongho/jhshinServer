#include "ServiceManager.h"

#include "ListenManager.h"
#include "SessionManager.h"

bool ServiceManager::Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount )
{
	bool Result = false;
	ListenManager* listenManager = ListenManager::This();
	SessionManager::This()->Initalize( 3 );
	m_UserSession.clear();
	m_iocp.Init( ServiceThreadCount );

	listenManager->Initalize( ListenThreadCount );

	Result = listenManager->Listen();
	if( Result )
	{
		Result = listenManager->Accept( AcceptCount );
	}

	return Result;
}

void ServiceManager::Start()
{
	m_iocp.Start();
}

void ServiceManager::AddIOCP( SessionData* session )
{
	if( nullptr == session )
	{
		return;
	}

	m_iocp.AddIOCP( session->GetSocket() );

	InsertUserSession( session->shared_from_this() );
}

bool ServiceManager::InsertUserSession( SessionDataRef session )
{
	bool Result = false;

	if( session )
	{
		lock_guard<mutex> lockGuard( m_Lock );
		Result = m_UserSession.insert( make_pair( session->GetSocket(), session ) ).second;
	}

	return Result;
}

void ServiceManager::EraseUserSession( SessionDataRef session )
{
	if( session )
	{
		lock_guard<mutex> lockGuard( m_Lock );
		m_UserSession.erase( session->GetSocket() );
	}
}

void ServiceManager::Join()
{
	m_iocp.Join();
	ListenManager::This()->GetIOCP().Join();
}


void ServiceManager::CloseSession( SessionDataRef session )
{
	if( session )
	{
		closesocket( session->GetSocket() );
		EraseUserSession( session );
	}
}
