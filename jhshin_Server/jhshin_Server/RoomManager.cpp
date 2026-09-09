#include "RoomManager.h"

RoomManager::RoomManager()
{
	m_Rooms.clear();
	m_RoomsThread.clear();
	m_RoomCount = 0;
}

void RoomManager::Initalize( int roomCount )
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
		room->StartJob();
	}
}

void RoomManager::Join()
{
	for( int i = 0; i < m_RoomCount; ++i )
	{
		m_Rooms[i]->LeaveAll();
	}

	for( int i = 0; i < m_RoomCount; ++i )
	{
		if( m_RoomsThread[i]->joinable() )
		{
			m_RoomsThread[i]->join();
		}
	}

	m_Rooms.clear();
}

void RoomManager::PushJobByRooms( JobObjectRef jobObject, int index )
{
	if( 0 > index || index >= m_RoomCount )
	{
		ServiceManager::This()->CloseSession( jobObject->GetSession() );
		return;
	}

	m_Rooms[index]->PushJob( jobObject );
}
