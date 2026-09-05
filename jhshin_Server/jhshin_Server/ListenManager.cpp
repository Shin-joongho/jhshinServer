#include "ListenManager.h"

#include "ConfigManager.h"
#include "SessionData.h"
#include "SessionManager.h"
#include "SocketUtill.h"

void ListenManager::Initalize( int ThreadCount )
{
	m_iocp.Init( ThreadCount );

	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD bytes = 0;
	WSAIoctl( m_iocp.GetSocket(), SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &m_lpfnAcceptEx, sizeof(m_lpfnAcceptEx), &bytes, nullptr, nullptr);

	m_iocp.AddIOCP( m_iocp.GetSocket() );
}

bool ListenManager::Listen()
{
	//int ServerPort = ConfigManager::This()->GetServerPort();
	int ServerPort = 27130;
	bool bResult = true;

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( ServerPort );

	bind( m_iocp.GetSocket(), (sockaddr*)&addr, sizeof( addr ) );

	listen( m_iocp.GetSocket(), SOMAXCONN );

	return bResult;
}

void ListenManager::Accept( int acceptCount )
{
	if( 0 >= acceptCount )
	{
		return;
	}

	m_AcceptObjects.reserve( acceptCount );

	for( int i = 0; i < acceptCount; ++i )
	{
		AcceptObject* acceptObject = new AcceptObject();
		m_AcceptObjects.push_back( acceptObject );

		Accept( acceptObject );
	}

	m_iocp.Start();
}

void ListenManager::Accept( AcceptObject* acceptObject, bool popSession )
{
	SessionData* session = acceptObject->GetSession();

	if( popSession )
	{
		session = SessionManager::This()->PopSession();
		acceptObject->SetSession( session );
	}

	if( session )
	{
		acceptObject->GetSession()->SetSocket( SocketUtill::MakeSocket() );
		m_lpfnAcceptEx( m_iocp.GetSocket(), acceptObject->GetSession()->GetSocket(), acceptObject->GetBuffer(), 0, sizeof( SOCKADDR_IN ) + 16, sizeof( SOCKADDR_IN ) + 16, acceptObject->GetByteRecv(), static_cast<LPOVERLAPPED>( acceptObject ) );

	}
	else
	{
		SessionManager::This()->InsertWait( acceptObject );
		// 세션 부족
	}
	
}

void ListenManager::Error( AcceptObject* acceptObject )
{
	if( nullptr == acceptObject )
	{
		return;
	}

	if( acceptObject->GetSession() )
	{
		closesocket( acceptObject->GetSession()->GetSocket() );
	}

	SessionManager::This()->PushSession( acceptObject->GetSession() );
	acceptObject->Clear();
	Accept( acceptObject );
}
