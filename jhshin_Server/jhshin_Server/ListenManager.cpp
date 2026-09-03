#include "ListenManager.h"

void ListenManager::Initalize( int SessionCount, int ThreadCount )
{
	m_iocp.Init( ThreadCount );

	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD bytes = 0;
	WSAIoctl( m_iocp.GetSocket(), SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &m_lpfnAcceptEx, sizeof(m_lpfnAcceptEx), &bytes, nullptr, nullptr);

	m_iocp.AddIOCP( m_iocp.GetSocket() );
}

bool ListenManager::Listen()
{
	int ServerPort = ConfigManager::This()->GetServerPort();
	bool bResult = 0;

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( ServerPort );

	bind( m_iocp.GetSocket(), (sockaddr*)&addr, sizeof( addr ) );

	listen( m_iocp.GetSocket(), SOMAXCONN );

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
		AcceptObject* acceptObject = new AcceptObject();
		m_AcceptObjects.push_back( acceptObject );

		Accept( acceptObject );
	}

	m_iocp.Start();
}

void ListenManager::Accept( AcceptObject* acceptObject )
{
	SessionData* session = SessionManager::This()->PopSession();
	if( session )
	{
		acceptObject->SetSession( session );
		acceptObject->GetSession()->SetSocket( SocketUtill::MakeSocket() );
		m_lpfnAcceptEx( m_iocp.GetSocket(), acceptObject->GetSession()->GetSocket(), acceptObject->GetBuffer(), 0, sizeof( SOCKADDR_IN ) + 16, sizeof( SOCKADDR_IN ) + 16, acceptObject->GetByteRecv(), static_cast<LPOVERLAPPED>( acceptObject ) );
	}
}
