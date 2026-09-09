#pragma once

#include "JobQueue.h"
#include "SocketUtill.h"
#include "PacketStruct.h"

#include "RSDefine.h"

class Room
{
public:
	Room()
	{
		m_roomUser.clear();
		m_RoomID = 0;
	}
	~Room() {}

	void StartJob();

	void PushJob( JobObjectRef jobObject );

	int GetRoomID() { return m_RoomID; }
	void SetRoomID( int roomID ) { m_RoomID = roomID; }

public:
	// 잡에서만 실행될 함수
	void Enter( SessionDataRef session );
	void Leave( SessionDataRef session );
	void LeaveAll();
	void BroadCast( SessionDataRef broadSession, Client_Broadcast_Req& packet );

private:
	JobQueue m_jobQueue;
	map<SOCKET, SessionDataRef> m_roomUser;
	int m_RoomID;
};

