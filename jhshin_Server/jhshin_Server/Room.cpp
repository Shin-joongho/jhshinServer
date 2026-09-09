#include "Room.h"

void Room::StartJob()
{
	queue<JobObjectRef> jobqueue;

	while( true )
	{	
		bool stop = m_jobQueue.PopAll( jobqueue );

		while( false == jobqueue.empty() )
		{
			jobqueue.front()->Dispatch( this );
			jobqueue.pop();
		}

		if( stop )
		{
			return;
		}
	}
}

void Room::BroadCast( SessionDataRef broadSession, Client_Broadcast_Req& packet )
{
	tuple<SendChunk, bool> check = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_ECHO, ( char* )&packet, sizeof( packet ) );

	for( auto session : m_roomUser )
	{
		if( session.second == broadSession )
		{
			continue;
		}
		session.second->InsertSendQueue( get<0>( check ) );
	}
}
