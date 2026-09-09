#pragma once

#include "SingletonTemplate.h"
#include "ServiceManager.h"
#include "Room.h"
#include "RSDefine.h"

class RoomManager : public SingleT<RoomManager>
{
public:
	RoomManager();
	~RoomManager() {}

	void Initalize( int roomCount );
	void Release();

	static void Work( RoomRef room );

	void Join();

	void PushJobByRooms( JobObjectRef jobObject, int index );

private:
	vector<RoomRef> m_Rooms;
	vector<shared_ptr<thread>> m_RoomsThread;
	int m_RoomCount;
};

