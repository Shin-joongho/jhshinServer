#include "IOCP.h"

#include "ListenManager.h"
#include "ServiceManager.h"
#include "SessionData.h"
#include "SocketUtill.h"

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
	LPOVERLAPPED lpOverlapped;
	ULONG_PTR key = 0;
	bool QueueResult = false;
	while( true )
	{
		lptransferByte = 0;
		QueueResult = GetQueuedCompletionStatus( thisIOCP->GetIOCPHandle(), &lptransferByte, &key, &lpOverlapped, INFINITE );

		IOCPObject* iocpObject = static_cast<IOCPObject*>( lpOverlapped );
		if( nullptr == iocpObject )
		{
			continue;
		}

		if( QueueResult )
		{
			switch( iocpObject->GetType() )
			{
				case IOCP_TYPE::IOCP_TYPE_ACCEPT:
				{
					iocpObject->Execute( lptransferByte );
					break;
				}
				case IOCP_TYPE::IOCP_TYPE_RECV:
				{
					if( lptransferByte > 0 )
					{
						// 정상 처리
						iocpObject->Execute( lptransferByte );
					}
					else
					{
						// 종료 처리 추가
						ServiceManager::This()->CloseSession( iocpObject->GetSession() );

						DWORD errCode = WSAGetLastError();
						switch( errCode )
						{
						case WAIT_TIMEOUT:
							break;
						default:

							break;
						}
					}

					break;
				}
				default:
					break;
			}

			
		}
		else
		{
			// 풀반환 필요
			if( iocpObject->GetType() == IOCP_TYPE::IOCP_TYPE_ACCEPT )
			{
				AcceptObject* acceptObject = (AcceptObject*)iocpObject;
				ListenManager::This()->Error( acceptObject );
			}
		}
	}
}

void IOCP::Join()
{
	for( std::thread* t : m_vecThread )
	{
		if( nullptr == t )
		{
			continue;
		}

		if( t->joinable() )
			t->join();

		delete t;
	}

	m_vecThread.clear();
}

void AcceptObject::Execute( int transferByte )
{
	sockaddr* pLocalAddr = nullptr;
	sockaddr* pRemoteAddr = nullptr;
	int LocalLen = 0;
	int RemoteLen = 0;

	if( nullptr == m_Session )
	{
		return;
	}

	GetAcceptExSockaddrs( m_OutputBuffer, 0, sizeof( SOCKADDR_IN ) + 16, sizeof( SOCKADDR_IN ) + 16, &pLocalAddr, &LocalLen, &pRemoteAddr, &RemoteLen );

	SOCKADDR_IN RemoteSockAddr;
	memcpy_s( &RemoteSockAddr, sizeof( RemoteSockAddr ), reinterpret_cast<SOCKADDR_IN*>( pRemoteAddr ), RemoteLen );

	m_Session->SetNetAddr( RemoteSockAddr );

	// 메인 IOCP에 연결
	SOCKET listenSocket = ListenManager::This()->GetIOCP().GetSocket();
	setsockopt( m_Session->GetSocket(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket, sizeof( listenSocket ));

	ServiceManager::This()->AddIOCP( m_Session );
	m_Session->RecvStart();

	Clear();
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
	if( nullptr == m_Session )
	{
		return;
	}

	m_Session->RecvStart();
}

void RecvObject::Clear()
{
	memset( static_cast<OVERLAPPED*>( this ), 0, sizeof( OVERLAPPED ) );
}

void AcceptObject::Clear()
{
	memset( static_cast<OVERLAPPED*>( this ), 0, sizeof( OVERLAPPED ) );
	m_Session = nullptr;
	m_ByteRecv = 0;
	memset( m_OutputBuffer, 0, sizeof( m_OutputBuffer ) );
}
