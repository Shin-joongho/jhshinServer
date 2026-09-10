#include "PacketHandler.h"
#include "RoomManager.h"

bool PacketHandler::Init()
{
#define PACKET( name )  if( false == Register( PacketType::name, HANDLE_##name ) ) { return false; }
	CLIENT_PACKET( PACKET )
#undef PACKET
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
		cout << "PacketType Duplication : " << (int)packetType << endl;
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

	memcpy( &echoReq, bufferData, bufferSize );

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

bool PacketHandler::HANDLE_PacketType_CLIENT_BROADCAST( const SessionDataRef& session, const char* bufferData, int bufferSize )
{
	Client_Broadcast_Req broadReq;

	if( bufferSize != sizeof( broadReq ) )
	{
		return false;
	}

	memcpy( &broadReq, bufferData, bufferSize );
	shared_ptr<Job_Broadcast> job_broadcast = make_shared<Job_Broadcast>();
	job_broadcast->SetSession( session );
	job_broadcast->SetReq( broadReq );

	// 룸 메니저를 ID로 찾아서 잡큐에 넣기
	RoomManager::This()->PushJobByRooms( job_broadcast, session->GetRoomID() );

	return true;
}

bool PacketHandler::HANDLE_PacketType_CLIENT_ENTER( const SessionDataRef& session, const char* bufferData, int bufferSize )
{
	shared_ptr<Job_Enter> job_enter = make_shared<Job_Enter>();
	job_enter->SetSession( session );

	int i = rand() % 5;
	RoomManager::This()->PushJobByRooms( job_enter, i );
	return true;
}

bool PacketHandler::HANDLE_PacketType_CLIENT_LEAVE( const SessionDataRef& session, const char* bufferData, int bufferSize )
{
	shared_ptr<Job_Leave> job_leave = make_shared<Job_Leave>();
	job_leave->SetSession( session );

	RoomManager::This()->PushJobByRooms( job_leave, session->GetRoomID() );
	return false;
}
