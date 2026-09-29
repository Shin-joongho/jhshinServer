#pragma once

#include "JobQueue.h"
#include "SocketUtil.h"
#include "PacketStruct.h"

#include "RSDefine.h"

class Room
{
public:
	Room()
	{
		m_roomUser.clear();
		m_RoomID = 0;

		for( unsigned int i = 0; i < MaxRoomUserCount; ++i )
		{
			m_seqIDs.push( i );
		}
	}
	~Room() {}

	bool StartJob();

	void PushJob( JobObjectRef jobObject );

	int GetRoomID() { return m_RoomID; }
	void SetRoomID( int roomID ) { m_RoomID = roomID; }

	void Stop();

public:
	// 잡에서만 실행될 함수
	void Enter( SessionDataRef session );
	void Leave( SessionDataRef session );
	void BroadCast( SessionDataRef broadSession, Client_Broadcast_Req& packet );

	void Move( SessionDataRef session, float posX, float posY );

	void MoveCheckUsers( uint64 nowTick );
	void MovePosition( RoomUser& user, uint64 nowTick );
	void SendMoveData( Server_Move_Ack& movePacket );

private:
	JobQueue m_jobQueue;
	map<SOCKET, RoomUser> m_roomUser;
	int m_RoomID;

	queue<unsigned int> m_seqIDs;

	int m_MaxX = 10000;
	int m_MaxY = 10000;

	uint64 lastTick = 0;
};

