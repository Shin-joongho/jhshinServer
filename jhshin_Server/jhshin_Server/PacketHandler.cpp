#include "PacketHandler.h"

bool PacketHandler::Init()
{
	bool Result = false;
#define X( name )  if( false == Register( PacketType::name, HANDLE_##name ) ) { return false; }
	CLIENT_PACKET( X )
#undef X
	return true;
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
		// 중복
		cout << "PakcetType Duplication : " << (int)packetType << endl;
		return false;
	}
}

bool PacketHandler::Dispatch( PacketType packetType, const SessionDataRef& session, const char* bufferData, int bufferSize )
{
	if( packetType >= PacketType::PacketType_CLIENT_END )
	{
		return false;
	}

	if( nullptr != m_PacketHandle[(int)packetType] )
	{
		return m_PacketHandle[(int)packetType]( session, bufferData, bufferSize );
	}

	return false;
}

bool PacketHandler::HANDLE_PacketType_CLIENT_ECHO( const SessionDataRef& session, const char* bufferData, int bufferSize )
{
	Client_ECHO_Req echoReq;

	if( bufferSize != sizeof( echoReq ) )
	{
		return false;
	}

	memcpy_s( &echoReq, sizeof( echoReq ), bufferData, bufferSize );

	tuple<SendChunk, bool> check = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_ECHO, (char*)&echoReq, sizeof( echoReq ) );
	if( get<1>( check ) )
	{
		session->InsertSendQueue( get<0>( check ) );
	}
	else
	{
		cout << "[Error] MakeSendPacket - SendBuffer pool exhausted" << endl;
	}

	return true;
}
