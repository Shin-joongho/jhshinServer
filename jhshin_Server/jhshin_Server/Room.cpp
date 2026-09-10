#include "Room.h"

#include "SessionData.h"
#include "ServiceManager.h"

bool Room::StartJob()
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

		return false == stop;
	}
}

void Room::PushJob( JobObjectRef jobObject )
{
	m_jobQueue.Push( jobObject );
}

void Room::Stop()
{
	m_jobQueue.Stop();
}

void Room::Enter( SessionDataRef session )
{
	if( session->IsConnected() )
	{
		return;
	}

	session->Connect();
	session->SetRoomID( m_RoomID );
	m_roomUser.insert( make_pair( session->GetSocket(), session ) );

	Server_Enter_Ack packet;
	packet.Set( m_RoomID );
	tuple<SendChunk, bool> check = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_ENTER, (char*)&packet, sizeof( packet ) );
	if( get<1>( check ) )
	{
		session->InsertSendQueue( get<0>( check ) );
	}
	else
	{
		cout << "[Error] MakeSendPacket - SendBuffer pool exhausted" << endl;
	}

}

void Room::Leave( SessionDataRef session )
{
	if( false == session->IsConnected() )
	{
		return;
	}

	session->DisConnected();
	session->SetRoomID( -1 );
	m_roomUser.erase( session->GetSocket() );
	ServiceManager::This()->CloseSession( session );
}

void Room::BroadCast( SessionDataRef broadSession, Client_Broadcast_Req& packet )
{
	tuple<SendChunk, bool> check = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_BROADCAST, ( char* )&packet, sizeof( packet ) );
	if( get<1>( check ) )
	{
		for( auto& session : m_roomUser )
		{
			if( session.second == broadSession )
			{
				continue;
			}
			session.second->InsertSendQueue( get<0>( check ) );
		}
	}
	else
	{
		cout << "[Error] MakeSendPacket - SendBuffer pool exhausted" << endl;
	}
}
