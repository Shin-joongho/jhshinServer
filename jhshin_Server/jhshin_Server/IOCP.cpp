#include "IOCP.h"

IOCP::IOCP()
{
	m_vecThread.clear();
}

IOCP::IOCP( int iThreadCount )
{
	Init( iThreadCount );
}

IOCP::~IOCP()
{
}

void IOCP::Init( int iThreadCount )
{
	m_iThreadCount = iThreadCount;

	m_vecThread.clear();
	m_vecThread.reserve( m_iThreadCount );

	m_Socket = SocketUtill::MakeSocket();
	if( m_Socket == SOCKET_ERROR )
	{
		return;
	}

	int OptionVal = 1 << eSocketOption_NoDelay | 1 << eSocketOption_ReUseAddr;

	SocketUtill::SetOptions( m_Socket, OptionVal );

	m_IOCPHandle = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 0 );

	return;
}

void IOCP::AddIOCP( SOCKET socket )
{
	CreateIoCompletionPort( (HANDLE)socket, m_IOCPHandle, 0, 0 );

	return;;
}

void IOCP::Start()
{
	for( int i = 0; i < m_iThreadCount; ++i )
	{
		thread* tr = new thread( Worker, this );
		m_vecThread.push_back( tr );
	}
}

void IOCP::Worker( IOCP* thisIOCP )
{
	DWORD lptransferByte;
	IOCPObject* iocpObject;
	ULONG_PTR key = 0;
	while( true )
	{
		lptransferByte = 0;
		iocpObject = nullptr;

		if( GetQueuedCompletionStatus( thisIOCP->GetIOCPHandle(), &lptransferByte, &key, (LPOVERLAPPED*)&iocpObject, INFINITE ) )
		{
			if( lptransferByte > 0 )
			{
				// 정상 처리
				iocpObject->Execute( lptransferByte );
			}
			else if( lptransferByte == 0 )
			{
				// 정상 종료
			}
			else
			{
				DWORD errCode = WSAGetLastError();
				switch( errCode )
				{
				case WAIT_TIMEOUT:
					return;
				default:

					break;
				}
				return;
			}
		}
	}
}

void IOCP::Join()
{
	for( std::thread* t : m_vecThread )
	{
		if( t->joinable() )
			t->join();
	}
	m_vecThread.clear();
}

void AcceptObject::Execute( int transferByte )
{
	sockaddr* pLocalAddr = nullptr;
	sockaddr* pRemoteAddr = nullptr;
	int LocalLen = 0;
	int RemoteLen = 0;

	GetAcceptExSockaddrs( m_OutputBuffer, 0, sizeof( SOCKADDR_IN ) + 16, sizeof( SOCKADDR_IN ) + 16, &pLocalAddr, &LocalLen, &pRemoteAddr, &RemoteLen );

	SOCKADDR_IN RemoteSockAddr;
	memcpy_s( &RemoteSockAddr, sizeof( RemoteSockAddr ), reinterpret_cast<SOCKADDR_IN*>( pRemoteAddr ), RemoteLen );

	if( m_Session )
	{
		m_Session->SetNetAddr( RemoteSockAddr );
	}

	// 메인 IOCP에 연결
	ServiceManager::This()->AddIOCP( m_Session );
	m_Session->RecvStart();

	m_Session = nullptr;

	ListenManager::This()->Accept( this );
}


void RecvObject::Initalize( SessionData* session )
{
	m_wsabuf.buf = m_RecvBuffer;
	m_wsabuf.len = sizeof( m_RecvBuffer );
	m_Session = session;
}

void RecvObject::Execute( int transferByte )
{
	// 패킷이 다 왔는지 확인
}

void AcceptObject::Clear()
{
	m_Session = nullptr;
	m_ByteRecv = 0;
	memset( m_OutputBuffer, 0, sizeof( m_OutputBuffer ) );
}