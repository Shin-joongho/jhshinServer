#pragma once

#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stack>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <queue>
#include <cstring>
#include <list>

using namespace std;


using uint16 = unsigned __int16;
using uint64 = unsigned __int64;

using SessionDataRef = shared_ptr<class SessionData>;
using SendBufferRef = shared_ptr<class SendBuffer>;
using JobObjectRef = shared_ptr<class JobObject>;
using RoomRef = shared_ptr<class Room>;

const int PACKET_SIZE = 4096;

const int MaxRoomUserCount = 1000;
const int MaxSendSpawnCount = 100;

const int MaxSendMoveCount = 100;
const int MinMoveTick = 100; // 0.1초
const float MoveSpeed = 1.0f;

struct RoomUser
{
	SessionDataRef m_session = nullptr;
	float m_posX = 0.0f;
	float m_posY = 0.0f;
	unsigned int m_seqID = 0;

	bool m_IsMove = false;
	float m_destPosX = 0.0f;
	float m_destPosY = 0.0f;
	uint64 m_startMoveTick = 0;
	uint64 m_endMoveTick = 0;

	void Set( SessionDataRef session, float posX, float posY, unsigned int seqID )
	{
		m_session = session;
		m_posX = posX;
		m_posY = posY;
		m_seqID = seqID;
	}
};