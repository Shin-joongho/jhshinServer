#include "IOCP.h"

#include "ListenManager.h"
#include "ServiceManager.h"
#include "SessionData.h"
#include "RoomManager.h"
#include "SocketUtill.h"

IOCP::IOCP()
{
	m_IOCPHandle = INVALID_HANDLE_VALUE;
	m_vecThread.clear();
	m_iThreadCount = 0;
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

	m_IOCPHandle = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 0 );

	return;
}

void IOCP::AddIOCP( SOCKET socket )
{
	CreateIoCompletionPort( (HANDLE)socket, m_IOCPHandle, 0, 0 );

	return;
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
			iocpObject->Execute( lptransferByte );			
		}
		else
		{
			// 풀반환 필요
			if( iocpObject->GetType() == IOCP_TYPE::IOCP_TYPE_ACCEPT )
			{
				AcceptObject* acceptObject = (AcceptObject*)iocpObject;
				ListenManager::This()->Error( acceptObject );
			}
			else if( iocpObject->GetType() == IOCP_TYPE::IOCP_TYPE_RECV )
			{
				RecvObject* recvObject = (RecvObject*)iocpObject;
				SessionDataRef session = iocpObject->GetSession();
				if( session )
				{
					iocpObject->SetSession( nullptr );
					if( false == session->RecvStart() )
					{
						// 룸에서 지우기
						shared_ptr<Job_Leave> job_leave = make_shared<Job_Leave>();
						job_leave->SetSession( session );

						RoomManager::This()->PushJobByRooms( job_leave, session->GetRoomID() );
					}
				}
			}
			else if( iocpObject->GetType() == IOCP_TYPE::IOCP_TYPE_SEND )
			{
				SendObject* sendObject = (SendObject*)iocpObject;
				SessionDataRef session = iocpObject->GetSession();
				sendObject->Clear();
				if( session )
				{
					// 룸에서 지우기
					shared_ptr<Job_Leave> job_leave = make_shared<Job_Leave>();
					job_leave->SetSession( session );

					RoomManager::This()->PushJobByRooms( job_leave, session->GetRoomID() );
				}
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
		{
			t->join();
		}

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

	ListenManager::This()->GetSocketAddrsFN() ( m_OutputBuffer, 0, sizeof( SOCKADDR_IN ) + 16, sizeof( SOCKADDR_IN ) + 16, &pLocalAddr, &LocalLen, &pRemoteAddr, &RemoteLen );

	SOCKADDR_IN RemoteSockAddr = {};
	if( RemoteLen < sizeof( RemoteSockAddr ) )
	{
		cout << "[Error] GetSocketAddrsFN" << endl;
		ListenManager::This()->Error( this );
		return;
	}
	memcpy_s( &RemoteSockAddr, sizeof( RemoteSockAddr ), reinterpret_cast<SOCKADDR_IN*>( pRemoteAddr ), RemoteLen );

	m_Session->SetNetAddr( RemoteSockAddr );

	// 메인 IOCP에 연결
	SOCKET listenSocket = ListenManager::This()->GetSocket();
	setsockopt( m_Session->GetSocket(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket, sizeof( listenSocket ));

	ServiceManager::This()->AddIOCP( m_Session.get() );
	m_Session->RecvStart();

	Clear();
	if( false == ListenManager::This()->Accept( this ) )
	{
		cout << "[Error] Insert Accept" << endl;
		ListenManager::This()->Error( this );
	}
}


void RecvObject::Initalize()
{
	m_RecvBuffer.Initalize( PACKET_SIZE );
}

void RecvObject::Execute( int transferByte )
{
	// 패킷이 다 왔는지 확인
	if( nullptr == m_Session )
	{
		return;
	}

	SessionDataRef session = m_Session;

	if( transferByte <= 0 )
	{
		// 종료 처리 추가
		// 룸에서 지우기
		shared_ptr<Job_Leave> job_leave = make_shared<Job_Leave>();
		job_leave->SetSession( session );

		RoomManager::This()->PushJobByRooms( job_leave, session->GetRoomID() );
		ReleaseSession();

		DWORD errCode = WSAGetLastError();
		switch( errCode )
		{
		case WAIT_TIMEOUT:
			break;
		default:

			break;
		}

		return;
	}

	if( false == session->Recv( transferByte ) )
	{
		cout << "PakcetHandler Error " << endl;
		shared_ptr<Job_Leave> job_leave = make_shared<Job_Leave>();
		job_leave->SetSession( session );

		RoomManager::This()->PushJobByRooms( job_leave, session->GetRoomID() );
		ReleaseSession();
	}
}

void RecvObject::Clear()
{
	memset( static_cast<OVERLAPPED*>( this ), 0, sizeof( OVERLAPPED ) );
}

void SendObject::Execute( int transferByte )
{
	SessionDataRef session = GetSession();
	Clear();

	if( nullptr == session )
	{
		return;
	}

	if( transferByte <= 0 )
	{
		// 세션 종료

		ServiceManager::This()->CloseSession( session );

		DWORD errCode = WSAGetLastError();
		switch( errCode )
		{
		case WAIT_TIMEOUT:
			break;
		default:

			break;
		}

		return;
	}

	session->CheckSendComplete();
}

void SendObject::Clear()
{
	memset( static_cast<OVERLAPPED*>( this ), 0, sizeof( OVERLAPPED ) );
	m_Session = nullptr;
	m_wsabufs.clear();
	m_SendChunks.clear();
}

void SendObject::AddSendChunk( SendChunk& sendChunk )
{
	WSABUF wsabuf;
	wsabuf.buf = sendChunk.m_buffer;
	wsabuf.len = sendChunk.m_size;

	m_SendChunks.push_back( sendChunk );
	m_wsabufs.push_back( wsabuf );
}

void AcceptObject::Clear()
{
	memset( static_cast<OVERLAPPED*>( this ), 0, sizeof( OVERLAPPED ) );
	m_Session = nullptr;
	m_ByteRecv = 0;
	memset( m_OutputBuffer, 0, sizeof( m_OutputBuffer ) );
}
