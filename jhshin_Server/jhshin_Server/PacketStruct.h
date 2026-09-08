#pragma once

#include "RSDefine.h"

#define CLIENT_PACKET( X ) \
	X( PacketType_CLIENT_ECHO )

enum class PacketType : uint16
{
	PacketType_NULL = 0,

#define X( name ) name,
	CLIENT_PACKET( X )
#undef X

	PacketType_CLIENT_END = 99, // 여기까지 클라

	PacketType_SERVER_ECHO = 100,

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

#pragma pack(push, 1)

struct Client_ECHO_Req
{
public:
	char m_text[100];

public:
	Client_ECHO_Req()
	{
		memset( m_text, 0, sizeof( m_text ) );
	}

	void Set( char* text )
	{
		strcpy_s( m_text, text );
	}
};

#pragma pack(pop)