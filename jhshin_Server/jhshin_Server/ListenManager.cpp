#include "ListenManager.h"

void ListenManager::Initalize( int ThreadCount )
{
	m_iocp.Init( ThreadCount );

	m_Socket = SocketUtill::MakeSocket();
	if( m_Socket == SOCKET_ERROR )
	{
		return;
	}

	int OptionVal = 1 << eSocketOption_NoDelay | 1 << eSocketOption_ReUseAddr;

	SocketUtill::SetOptions( m_Socket, OptionVal );

	GUID guidAcceptEx = WSAID_ACCEPTEX;
	GUID guidSocketAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD bytes = 0;
	WSAIoctl( m_Socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &m_lpfnAcceptEx, sizeof( m_lpfnAcceptEx ), &bytes, nullptr, nullptr);
	WSAIoctl( m_Socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidSocketAddrs, sizeof( guidSocketAddrs ), &m_lpfnGetAcceptExSockaddrs, sizeof( m_lpfnGetAcceptExSockaddrs ), &bytes, nullptr, nullptr);

	m_iocp.AddIOCP( m_Socket );
}

bool ListenManager::Listen()
{
	int ServerPort = 27130;
	bool Result = true;

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( ServerPort );

	if( SOCKET_ERROR == bind( m_Socket, (sockaddr*)&addr, sizeof( addr ) ) )
	{
		cout << "[Error] Bind / Port : " << ServerPort << endl;
		Result = false;
	}
	else
	{
		cout << "[Success] Bind / Port : " << ServerPort << endl;
	}

	if( SOCKET_ERROR == listen( m_Socket, SOMAXCONN ) )
	{
		cout << "[Error] Listen" << endl;
		Result = false;
	}
	else
	{
		cout << "[Success] Listen" << endl;
	}

	return Result;
}

bool ListenManager::Accept( int acceptCount )
{
	if( 0 >= acceptCount )
	{
		printf( "[Error] AccoeptCount : %d", acceptCount );
		return false;
	}

	m_AcceptObjects.reserve( acceptCount );

	for( int i = 0; i < acceptCount; ++i )
	{
		AcceptObject* acceptObject = new AcceptObject();
		m_AcceptObjects.push_back( acceptObject );

		Accept( acceptObject );
	}

	m_iocp.Start();
	return true;
}

bool ListenManager::Accept( AcceptObject* acceptObject, bool popSession )
{
	bool Result = false;
	SessionDataRef session = acceptObject->GetSession();

	if( popSession )
	{
		session = SessionManager::This()->PopSession();
		acceptObject->SetSession( session );
	}

	if( session )
	{
		acceptObject->GetSession()->SetSocket( SocketUtill::MakeSocket() );
		m_lpfnAcceptEx( m_Socket, acceptObject->GetSession()->GetSocket(), acceptObject->GetBuffer(), 0, sizeof( SOCKADDR_IN ) + 16, sizeof( SOCKADDR_IN ) + 16, acceptObject->GetByteRecv(), static_cast<LPOVERLAPPED>( acceptObject ) );
		Result = true;
	}
	else
	{
		// 세션 부족
		SessionManager::This()->InsertWait( acceptObject );
		Result = true;
	}
	
	return Result;
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

	acceptObject->Clear();
	Accept( acceptObject );
}
