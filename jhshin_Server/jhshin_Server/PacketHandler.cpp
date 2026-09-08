#include "PacketHandler.h"

bool PacketHandler::Init()
{
	bool Result = false;
#define X( name )  Result = Register( PacketType::name, HANDLE_##name );
	CLIENT_PACKET(X)
#undef X

	if( !Result )
	{
		return false;
	}
}

bool PacketHandler::Register( PacketType packetType, PacketHandlerFunc func )
{
	if( nullptr == m_PacketHandle[(int)packetType] )
	{
		m_PacketHandle[(int)packetType] = func;
		return true;
	}
	else
	{
		// ม฿บน
		cout << "PakcetType Duplication : " << (int)packetType << endl;
		return false;
	}
}

bool PacketHandler::Dispatch( PacketType packetType, const SessionDataRef& session, const char* bufferData, int bufferSize )
{
	return false;
}

bool PacketHandler::HANDLE_PacketType_CLIENT_ECHO( const SessionDataRef& session, const char* bufferData, int bufferSize )
{
	return false;
}
