#include "ServiceManager.h"

#include "ListenManager.h"
#include "SessionManager.h"

bool ServiceManager::Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount )
{
	bool Result = false;
	ListenManager* listenManager = ListenManager::This();
	SessionManager::This()->Initalize( 3 );
	m_UserSession.clear();
	m_iocp.Init( ServiceThreadCount );

	listenManager->Initalize( ListenThreadCount );

	Result = listenManager->Listen();
	if( Result )
	{
		Result = listenManager->Accept( AcceptCount );
	}

	// 테스트용 고정치
	m_SendBuffer.InitObjectPool( 10 );

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

tuple<SendChunk, bool>  ServiceManager::MakeSendPacket( const char* sendData, const int sendSize )
{
	lock_guard<mutex> lg( m_SendLock );

	bool Result = false;
	SendChunk sendChunk;

	if( nullptr == m_LastSendBuffer )
	{
		m_LastSendBuffer = GetSendBuffer();
		if( nullptr == m_LastSendBuffer )
		{
			// 풀 부족
			return make_tuple( sendChunk, false );
		}
		
	}

	tuple<int, int> tp = m_LastSendBuffer->CopyBuffer( sendData, sendSize );
	int bufferPointer = get<0>( tp );
	int totalSize = get<1>( tp );
	if( bufferPointer == -1 )
	{
		m_LastSendBuffer = GetSendBuffer();
		if( nullptr == m_LastSendBuffer )
		{
			// 풀 부족 
			return make_tuple( sendChunk, false );
		}

		tp = m_LastSendBuffer->CopyBuffer( sendData, sendSize );
		bufferPointer = get<0>( tp );
		totalSize = get<1>( tp );
	}

	sendChunk.Set( m_LastSendBuffer, m_LastSendBuffer->GetSendBuffer( bufferPointer ), totalSize );

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
