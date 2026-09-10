#include "ListenManager.h"

void ListenManager::Initialize( int ThreadCount )
{
	m_iocp.Init( ThreadCount );

	m_Socket = SocketUtil::MakeSocket();
	if( m_Socket == SOCKET_ERROR )
	{
		return;
	}

	int OptionVal = 1 << eSocketOption_NoDelay | 1 << eSocketOption_ReUseAddr;

	SocketUtil::SetOptions( m_Socket, OptionVal );

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
		printf( "[Error] AcceptCount : %d", acceptCount );
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
	if( nullptr == acceptObject )
	{
		return false;
	}

	// 종료 중이면 재게시하지 않는다.
	// 여기서 막지 않으면 닫힌 리슨 소켓에 AcceptEx 를 다시 걸게 되고,
	// 그 요청은 완료 통지가 오지 않으므로 물고 있던 세션이 영영 풀로 돌아가지 못한다.
	if( m_Stop )
	{
		acceptObject->Clear();
		return false;
	}

	bool Result = false;
	SessionDataRef session = acceptObject->GetSession();

	if( popSession )
	{
		session = SessionManager::This()->PopSession();
		acceptObject->SetSession( session );
	}

	if( session )
	{
		acceptObject->GetSession()->SetSocket( SocketUtil::MakeSocket() );
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

	// Clear() 가 m_Session 을 놓아 세션이 풀로 돌아간다. 재게시보다 먼저 해야 한다.
	acceptObject->Clear();

	if( m_Stop )
	{
		// 종료 중 - 재무장하지 않는다.
		return;
	}

	Accept( acceptObject );
}

// 종료 1단계. 리슨 소켓을 닫으면 게시해둔 AcceptEx 가 전부 에러로 완료되고,
// 그 완료들은 워커의 IOCP_TYPE_ACCEPT 실패 분기 -> Error() 로 들어온다.
void ListenManager::Shutdown()
{
	// 소켓보다 플래그를 먼저 세운다.
	// 순서가 반대면 닫는 순간 쏟아지는 완료가 아직 false 인 플래그를 보고 재게시한다.
	m_Stop = true;

	if( INVALID_SOCKET != m_Socket )
	{
		closesocket( m_Socket );
		m_Socket = INVALID_SOCKET;
	}
}
