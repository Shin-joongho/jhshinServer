#pragma once
#include "JobQueue.h"
#include "SessionManager.h"

class Room
{
public:
	Room()
	{
		m_roomUser.clear();
	}
	~Room() {}

	void StartJob();

	void BroadCast( SessionDataRef broadSession, Client_Broadcast_Req& packet );

private:
	JobQueue m_jobQueue;
	map<SOCKET, SessionDataRef> m_roomUser;
};

