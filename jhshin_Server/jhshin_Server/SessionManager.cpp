#include "SessionManager.h"


SessionManager::SessionManager()
{
}

SessionManager::~SessionManager()
{

}

void SessionManager::Initialize( int sessionCount )
{
	m_SessionPools.InitObjectPool( sessionCount );
}

SessionDataRef SessionManager::PopSession()
{
	SessionData* session = m_SessionPools.Pop();
	if( nullptr == session )
	{
		return nullptr;
	}

	// 자동 반환
	return SessionDataRef( session, []( SessionData* psession ) { SessionManager::This()->PushSession( psession ); } );
}

void SessionManager::PushSession( SessionData* session )
{
	if( session )
	{
		session->Reset();
		m_SessionPools.Push( session );
		ReWaiting();		
	}
}

void SessionManager::InsertWait( AcceptObject* acceptObject )
{
	{
		lock_guard<mutex> lg( m_WaitLock );

		m_WaitQueue.push( acceptObject );
	}

	// 넣는 사이 반납 확인
	ReWaiting();
}

void SessionManager::ReWaiting()
{
	// 종료 중에는 대기 큐를 다시 채우지 않는다.
	// 막지 않으면 세션이 반납될 때마다 PushSession -> ReWaiting -> Accept -> Clear ->
	// 다시 PushSession 으로 되도는 데다, 그 경로가 재귀라 큐가 길면 스택이 깊어진다.
	if( ListenManager::This()->IsStopped() )
	{
		return;
	}

	AcceptObject* acceptObject = nullptr;
	{
		lock_guard<mutex> lg( m_WaitLock );
		if( m_WaitQueue.empty() )
		{
			return;
		}
	}
	
	SessionDataRef session = PopSession();
	if( nullptr == session )
	{
		return;
	}

	{
		lock_guard<mutex> lg( m_WaitLock );
		if( m_WaitQueue.empty() )
		{
			return;
		}
		acceptObject = m_WaitQueue.front();
		m_WaitQueue.pop();
	}

	acceptObject->SetSession( session );
	ListenManager::This()->Accept( acceptObject, false );
}

// 종료 1단계.
// 대기 큐의 AcceptObject 들은 세션이 없어 AcceptEx 에 게시된 적이 없다.
// 즉 완료 통지가 오지 않으므로 워커가 놓아줄 수 없고, 여기서 직접 비워야 한다.
void SessionManager::ClearWaitQueue()
{
	queue<AcceptObject*> waiting;
	{
		lock_guard<mutex> lg( m_WaitLock );
		m_WaitQueue.swap( waiting );
	}

	while( false == waiting.empty() )
	{
		AcceptObject* acceptObject = waiting.front();
		waiting.pop();

		if( acceptObject )
		{
			acceptObject->Clear();
		}
	}
}

int SessionManager::GetSessionCount()
{
	return m_SessionPools.GetFreeCount();
}
