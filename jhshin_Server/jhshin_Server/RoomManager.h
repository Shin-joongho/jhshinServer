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

	void Initialize( int roomCount );
	void Release();

	static void Work( RoomRef room );

	void Join();

	void PushJobByRooms( JobObjectRef jobObject, int index );

private:
	vector<RoomRef> m_Rooms;
	vector<shared_ptr<thread>> m_RoomsThread;
	int m_RoomCount;

	// IOCP 워커가 PushJobByRooms 에서 읽는다.
	atomic<bool> m_Stop = false;
};

