#pragma once

#include "RSDefine.h"
#include "Buffer.h"

using PacketHandlerFunc = bool ( * )( const SessionDataRef& session, const char* bufferData, int bufferSize );
#define REGISTER_HANDLE( packettype ) Register( PacketType::packettype, HANDLE_##packettype );

class PacketHandler
{
public:
	static bool Init();

	static bool Register( PacketType packetType, PacketHandlerFunc func );
	static bool Dispatch( PacketType packetType, const SessionDataRef& session, const char* bufferData, int bufferSize );


	static bool HANDLE_PacketType_CLIENT_ECHO( const SessionDataRef& session, const char* bufferData, int bufferSize );

private:
	inline static PacketHandlerFunc m_PacketHandle[(int)PacketType::PacketType_MAX] = {};
};

