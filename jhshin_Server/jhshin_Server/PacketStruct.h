#pragma once

#include "RSDefine.h"

#define CLIENT_PACKET( PACKET ) \
	PACKET( PacketType_CLIENT_ECHO ) \
	PACKET( PacketType_CLIENT_BROADCAST ) \
	PACKET( PacketType_CLIENT_ENTER ) \
	PACKET( PacketType_CLIENT_LEAVE ) \
	PACKET( PacketType_CLIENT_MOVE )
	

#define SERVER_PACKET( PACKET ) \
	PACKET( PacketType_SERVER_ECHO ) \
	PACKET( PacketType_SERVER_BROADCAST ) \
	PACKET( PacketType_SERVER_ENTER ) \
	PACKET( PacketType_SERVER_SPAWN ) \
	PACKET( PacketType_SERVER_LEAVE ) \
	PACKET( PacketType_SERVER_DESPAWN ) \
	PACKET( PacketType_SERVER_MOVE )

#pragma pack(push, 1)

enum class PacketType : uint16
{
	PacketType_NULL = 0,

#define PACKET( name ) name,
	CLIENT_PACKET( PACKET )
#undef PACKET

	PacketType_CLIENT_END = 99, // 여기까지 클라

#define PACKET( name ) name,
	SERVER_PACKET( PACKET )
#undef PACKET

	PacketType_MAX = 999,
};

struct PacketID
{
	PacketType _type;
	// 헤더 크기를 뺀 실제 데이터 크기
	uint16 _size;

	PacketID()
	{
		_type = PacketType::PacketType_NULL;
		_size = 0;
	}
	PacketID( PacketType type, uint16 size )
	{
		_type = type;
		_size = size;
	}
};

struct Client_ECHO_Req
{
public:
	char m_text[64];

public:
	Client_ECHO_Req()
	{
		memset( m_text, 0, sizeof( m_text ) );
	}

	void Set( char* text )
	{
		strncpy_s( m_text, sizeof( m_text), text, _TRUNCATE );
	}
};

struct Client_Broadcast_Req
{
private:
	char m_text[64];

public:
	Client_Broadcast_Req()
	{
		memset( m_text, 0, sizeof( m_text ) );
	}

	void Set( char* text )
	{
		strncpy_s( m_text, sizeof( m_text ), text, _TRUNCATE );
	}

	char* Get() { return m_text; }
};

struct Server_Enter_Ack
{
private:
	int m_roomID = 0;
	unsigned int m_seqID = 0;

public:

	void Set( int roomID, unsigned int seqID ) 
	{
		m_roomID = roomID; 
		m_seqID = seqID;
	}

	int GetRoomID() { return m_roomID; }
	unsigned int GetSeqID() { return m_seqID; }
};

struct SpawnData
{
	unsigned int m_seqID;
	float m_posX;
	float m_posY;
};

struct Server_Spawn
{
private:
	int m_count;
	SpawnData m_spawnData[MaxSendSpawnCount];

public:
	Server_Spawn()
	{
		Clear();
	}

	int GetSize()
	{
		return sizeof( m_count ) + ( sizeof( SpawnData ) * m_count );
	}

	void Clear()
	{
		memset( m_spawnData, 0, sizeof( m_spawnData ) );
		m_count = 0;
	}

	bool AddSpawnData( SpawnData& spawnData )
	{
		if( 0 <= m_count && m_count < MaxSendSpawnCount )
		{
			m_spawnData[m_count] = spawnData;
			++m_count;

			return true;
		}
		else
		{
			return false;
		}
	}

	bool AddSpawnData( unsigned int seqID, float posX, float posY )
	{
		if( 0 <= m_count && m_count < MaxSendSpawnCount )
		{
			m_spawnData[m_count].m_seqID = seqID;
			m_spawnData[m_count].m_posX = posX;
			m_spawnData[m_count].m_posY = posY;

			++m_count;

			return true;
		}
		else
		{
			return false;
		}
	}

	SpawnData* GetSpawnData( int Index )
	{
		if( 0 <= Index && Index < m_count )
		{
			return &m_spawnData[Index];
		}
		else
		{
			return nullptr;
		}
	}

	int GetCount() { return m_count; }
};

struct Server_Leave
{
private:
	unsigned int m_seqID = 0;

public:
	void SetSeqID( unsigned int seqID )
	{
		m_seqID = seqID;
	}

	unsigned int GetSeqID() { return m_seqID; }
};

struct Client_Move_Req
{
private:
	float m_posX = 0.0f;
	float m_posY = 0.0f;

public:
	void Set( float posX, float posY )
	{
		m_posX = posX;
		m_posY = posY;
	}

	float GetPosX() { return m_posX; }
	float GetPosY() { return m_posY; }
};

#pragma pack(pop)