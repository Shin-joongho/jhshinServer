#include "ServiceManager.h"

#include "ListenManager.h"
#include "SessionManager.h"

bool ServiceManager::Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount )
{
	bool Result = false;
	ListenManager* listenManager = ListenManager::This();
	SessionManager::This()->Initalize( 10000 );
	m_UserSession.clear();
	m_iocp.Init( ServiceThreadCount );

	listenManager->Initalize( ListenThreadCount );

	Result = listenManager->Listen();
	if( Result )
	{
		Result = listenManager->Accept( AcceptCount );
	}

	// 테스트용 고정치
	m_SendBuffer.InitObjectPool( 1024 );

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

// 스레드마다 "지금 잘라 쓰는 청크"를 하나씩 들고 있다.
// 커서를 전진시키는 주체가 자기 스레드 하나뿐이라 락이 필요 없고,
// 락은 청크를 새로 꺼낼 때 ObjectPool 내부에서만 잡힌다.
// 이전 청크는 거기서 잘려나간 SendChunk 들이 shared_ptr 로 붙잡고 있으므로
// 전송이 전부 끝나야 풀로 반납된다.
thread_local SendBufferRef LSendBuffer;

tuple<SendChunk, bool>  ServiceManager::MakeSendPacket( const char* sendData, const int sendSize )
{
	SendChunk sendChunk;

	if( nullptr == LSendBuffer )
	{
		LSendBuffer = GetSendBuffer();
		if( nullptr == LSendBuffer )
		{
			// 풀 부족
			return make_tuple( sendChunk, false );
		}
	}

	tuple<int, int> tp = LSendBuffer->CopyBuffer( sendData, sendSize );
	int bufferPointer = get<0>( tp );
	int totalSize = get<1>( tp );
	if( bufferPointer == -1 )
	{
		// 현재 청크에 자리가 없다. 새 청크를 받아 재시도.
		LSendBuffer = GetSendBuffer();
		if( nullptr == LSendBuffer )
		{
			// 풀 부족
			return make_tuple( sendChunk, false );
		}

		tp = LSendBuffer->CopyBuffer( sendData, sendSize );
		bufferPointer = get<0>( tp );
		totalSize = get<1>( tp );

		if( bufferPointer == -1 )
		{
			// 빈 청크에도 안 들어간다 = 청크 크기를 넘는 패킷
			return make_tuple( sendChunk, false );
		}
	}

	sendChunk.Set( LSendBuffer, LSendBuffer->GetSendBuffer( bufferPointer ), totalSize );

	return make_tuple( sendChunk, true );
}

SendBufferRef ServiceManager::GetSendBuffer()
{
	SendBuffer* sendBuffer = m_SendBuffer.Pop();
	if( sendBuffer )
	{
		return SendBufferRef( sendBuffer, [this]( SendBuffer* psendBuffer ) { this->m_SendBuffer.Push( psendBuffer ); } );
	}
	else
	{
		return nullptr;
	}
}
