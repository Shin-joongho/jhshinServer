#include "ServiceManager.h"

#include "ListenManager.h"
#include "SessionManager.h"

void ServiceManager::Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount )
{
	ListenManager* listenManager = ListenManager::This();
	SessionManager::This()->Initalize( 1000 );
	m_UserSession.clear();
	m_iocp.Init( ServiceThreadCount );

	listenManager->Initalize( ListenThreadCount );
	listenManager->Listen();
	listenManager->Accept( AcceptCount );
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

void ServiceManager::Join()
{
	m_iocp.Join();
	ListenManager::This()->GetIOCP().Join();
}


void ServiceManager::CloseSession( SessionData* session )
{
	if( session )
	{
		closesocket( session->GetSocket() );
		EraseUserSession( session );
		SessionManager::This()->PushSession( session );
	}
}
