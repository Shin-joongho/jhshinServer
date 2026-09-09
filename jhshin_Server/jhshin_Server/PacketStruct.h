#pragma once

#include "RSDefine.h"

#define CLIENT_PACKET( X ) \
	X( PacketType_CLIENT_ECHO )

#define SERVER_PACKET( X ) \
	X( PacketType_SERVER_ECHO )

#pragma pack(push, 1)

enum class PacketType : uint16
{
	PacketType_NULL = 0,

#define X( name ) name,
	CLIENT_PACKET( X )
#undef X

	PacketType_CLIENT_END = 99, // 여기까지 클라

#define X( name ) name,
	SERVER_PACKET( X )
#undef X

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

#pragma pack(pop)