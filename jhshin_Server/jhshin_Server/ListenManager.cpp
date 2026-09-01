#include "ListenManager.h"

void ListenManager::Initalize( int SessionCount )
{
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD bytes = 0;
	WSAIoctl( m_Socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof( guidAcceptEx ), &m_lpfnAcceptEx, sizeof( m_lpfnAcceptEx ), &bytes, nullptr, nullptr );

	Init( 5 );
	AddIOCP( m_Socket );

	m_SessionPools.InitMemoryPool( SessionCount );
}

bool ListenManager::Listen()
{
	int ServerPort = ConfigManager::This()->GetServerPort();
	bool bResult = 0;

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( ServerPort );

	if( false == bind( m_Socket, (sockaddr*)&addr, sizeof( addr ) ) )
	{
		return false;
	}

	if( false == listen( m_Socket, SOMAXCONN ) )
	{
		return false;
	}

	int AcceptCount = ConfigManager::This()->GetAcceptCount();
	if( 0 >= AcceptCount )
	{
		return false;
	}

	Accept( AcceptCount );
}

void ListenManager::Accept( int AcceptCount )
{
	m_AcceptObjects.reserve( AcceptCount );

	for( int i = 0; i < AcceptCount; ++i )
	{
		shared_ptr<AcceptObject> acceptObject = make_shared<AcceptObject>( );
		m_AcceptObjects.push_back( acceptObject );

		Accept( acceptObject );
	}
}

void ListenManager::Accept( shared_ptr<AcceptObject>& acceptObject )
{
	SessionData* session = PopSession();
	if( session )
	{
		acceptObject->SetSession( session );
		acceptObject->GetSession()->SetSocket( SocketUtill::MakeSocket() );
		m_lpfnAcceptEx( m_Socket, acceptObject->GetSession()->GetSocket(), acceptObject->GetBuffer(), 0, sizeof( SOCKADDR_IN ) + 16, sizeof( SOCKADDR_IN ) + 16, acceptObject->GetByteRecv(), static_cast<LPOVERLAPPED>( acceptObject.get() ) );
	}
}

SessionData* ListenManager::PopSession()
{
	return m_SessionPools.Pop();
}

void ListenManager::PushSession( SessionData* Session )
{
	if( Session )
	{
		Session->Reset();
		m_SessionPools.Push( Session );
	}
}
