#include "RoomManager.h"

RoomManager::RoomManager()
{
	m_Rooms.clear();
	m_RoomsThread.clear();
	m_RoomCount = 0;
}

void RoomManager::Initialize( int roomCount )
{
	m_RoomCount = roomCount;

	for( int i = 0; i < m_RoomCount; ++i )
	{
		RoomRef room = make_shared<Room>();
		room->SetRoomID( i );
		shared_ptr<thread> roomThread = make_shared<thread>( Work, room );

		m_Rooms.push_back( room );
		m_RoomsThread.push_back( roomThread );
	}
}


void RoomManager::Release()
{
	Join();
	SingleT::Release();
}

void RoomManager::Work( RoomRef room )
{
	if( nullptr == room )
	{
		return;
	}

	while( true )
	{
		if( false == room->StartJob() )
		{
			return;
		}
	}
}

// 종료 2단계.
// Shutdown() 에서 한 번, main 의 Release() 에서 또 한 번 불리므로 두 번째는 그냥 나간다.
void RoomManager::Join()
{
	if( m_Stop.exchange( true ) )
	{
		return;
	}

	// m_Stop 을 먼저 세운 뒤에 정지시킨다.
	// 이 순서라야 아직 완료를 처리 중인 워커가 PushJobByRooms 에서 걸러진다.
	for( int i = 0; i < m_RoomCount; ++i )
	{
		m_Rooms[i]->Stop();
	}

	for( int i = 0; i < m_RoomCount; ++i )
	{
		if( m_RoomsThread[i]->joinable() )
		{
			m_RoomsThread[i]->join();
		}
	}

	// m_Rooms 는 비우지 않는다.
	// 워커가 아직 살아 있는 동안 vector 를 줄이면 PushJobByRooms 의
	// 검사와 인덱싱 사이에서 빈 vector 를 건드릴 수 있다.
	// RoomRef 는 이 객체가 소멸할 때 같이 정리된다.
}

void RoomManager::PushJobByRooms( JobObjectRef jobObject, int index )
{
	// 룸이 정지된 뒤에 밀어 넣으면 아무도 꺼내지 않는다.
	// 그 잡이 붙들고 있는 세션 참조도 같이 묶여 풀로 돌아가지 못하므로,
	// 여기서 바로 세션을 정리하고 잡은 버린다.
	if( m_Stop || 0 > index || index >= m_RoomCount )
	{
		ServiceManager::This()->CloseSession( jobObject->GetSession() );
		return;
	}

	m_Rooms[index]->PushJob( jobObject );
}
