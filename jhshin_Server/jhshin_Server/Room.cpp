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

		uint64 nowTick = GetTickCount64();
		if( lastTick + MinMoveTick <= nowTick )
		{
			lastTick = nowTick;
			MoveCheckUsers( nowTick );
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
	if( nullptr == session )
	{
		return;
	}

	if( session->IsConnected() )
	{
		return;
	}

	Server_Enter_Ack packet;

	if( m_seqIDs.empty() )
	{
		// 최대 인원 초과
		packet.Set( -1, 0 );
		tuple<SendChunk, bool> sendNak = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_ENTER, ( char* )&packet, sizeof( packet ) );
		if( get<1>( sendNak ) )
		{
			session->InsertSendQueue( get<0>( sendNak ) );
		}
	}
	else
	{
		unsigned int seqID = m_seqIDs.front();
		m_seqIDs.pop();

		RoomUser roomUser;
		roomUser.Set( session, 0.0f, 0.0f, seqID );

		packet.Set( m_RoomID, seqID );

		Server_Spawn spawnPacket;
		spawnPacket.AddSpawnData( seqID, roomUser.m_posX, roomUser.m_posY );

		tuple<SendChunk, bool> sendCliet = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_ENTER, ( char* )&packet, sizeof( packet ) );
		tuple<SendChunk, bool> sendBroadCast = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_SPAWN, ( char* )&spawnPacket, spawnPacket.GetSize() );
		if( get<1>( sendCliet ) && get<1>( sendBroadCast ) )
		{
			if( m_roomUser.insert( make_pair( session->GetSocket(), roomUser ) ).second )
			{
				session->Connect();
				session->SetRoomID( m_RoomID );

				session->InsertSendQueue( get<0>( sendCliet ) );

				Server_Spawn enterSpawn;
				for( auto& users : m_roomUser )
				{
					if( users.second.m_session == session )
					{
						continue;
					}

					users.second.m_session->InsertSendQueue( get<0>( sendBroadCast ) );

					if( false == enterSpawn.AddSpawnData( users.second.m_seqID, users.second.m_posX, users.second.m_posY ) )
					{
						tuple<SendChunk, bool> sendEnterSpawn = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_SPAWN, ( char* )&enterSpawn, enterSpawn.GetSize() );
						if( false == get<1>( sendEnterSpawn ) )
						{
							// 여기 반복해서 확인하기는 애매하고 오지 않을 가능성 높으니 그냥 로그만 남김
							cout << "AddSpawnData Error Not Pool" << endl;
							continue;
						}
						else
						{
							session->InsertSendQueue( get<0>( sendEnterSpawn ) );

							enterSpawn.Clear();
							if( false == enterSpawn.AddSpawnData( users.second.m_seqID, users.second.m_posX, users.second.m_posY ) )
							{
								cout << "AddSpawnData Error" << endl;
							}
						}
					}
				}

				if( enterSpawn.GetCount() > 0 )
				{
					tuple<SendChunk, bool> sendEnterSpawn = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_SPAWN, ( char* )&enterSpawn, enterSpawn.GetSize() );
					if( false == get<1>( sendEnterSpawn ) )
					{
						cout << "AddSpawnData Error Not Pool" << endl;
					}
					else
					{
						session->InsertSendQueue( get<0>( sendEnterSpawn ) );
					}

				}
			}
			else
			{
				m_seqIDs.push( seqID );
			}
		}
		else
		{
			m_seqIDs.push( seqID );
			cout << "[Error] MakeSendPacket - SendBuffer pool exhausted" << endl;
		}
	}
}

void Room::Leave( SessionDataRef session )
{
	if( nullptr == session )
	{
		return;
	}

	if( false == session->IsConnected() )
	{
		return;
	}

	session->DisConnected();
	session->SetRoomID( -1 );

	auto user = m_roomUser.find( session->GetSocket() );
	if( user != m_roomUser.end() )
	{
		unsigned int seqID = user->second.m_seqID;
		Server_Leave leavePacket;
		leavePacket.SetSeqID( seqID );

		tuple<SendChunk, bool> leaveBroadCast = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_LEAVE, ( char* )&leavePacket, sizeof( leavePacket ) );
		if( get<1>( leaveBroadCast ) )
		{
			for( auto& user : m_roomUser )
			{
				if( user.second.m_session == session )
				{
					continue;
				}

				user.second.m_session->InsertSendQueue( get<0>( leaveBroadCast ) );
			}

			// 오류가 있을경우 seq반납하지 않음
			m_seqIDs.push( seqID );
		}
		else
		{
			cout << "[Error] MakeSendPacket - SendBuffer pool exhausted : seqID  " << seqID << endl;
		}

		m_roomUser.erase( session->GetSocket() );
	}

	// 없었던 경우 그냥 종료
	ServiceManager::This()->CloseSession( session );

	return;
}

void Room::BroadCast( SessionDataRef broadSession, Client_Broadcast_Req& packet )
{
	tuple<SendChunk, bool> check = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_BROADCAST, ( char* )&packet, sizeof( packet ) );
	if( get<1>( check ) )
	{
		for( auto& users : m_roomUser )
		{
			if( users.second.m_session == broadSession )
			{
				continue;
			}
			users.second.m_session->InsertSendQueue( get<0>( check ) );
		}
	}
	else
	{
		cout << "[Error] MakeSendPacket - SendBuffer pool exhausted" << endl;
	}
}

void Room::Move( SessionDataRef session, float posX, float posY )
{
	if( nullptr == session )
	{
		return;
	}

	if( false == session->IsConnected() )
	{
		return;
	}

	if( 0 > posX || posX >= m_MaxX )
	{
		return;
	}
	if( 0 > posY || posY >= m_MaxY )
	{
		return;
	}

	uint64 nowTick = GetTickCount64();

	auto roomUser = m_roomUser.find( session->GetSocket() );
	if( roomUser == m_roomUser.end() )
	{
		return;
	}

	RoomUser& user = roomUser->second;

	MovePosition( user, nowTick );

	float dx = posX - user.m_posX;
	float dy = posY - user.m_posY;

	float dist = sqrtf( dx * dx + dy * dy );

	if( dist <= 0 )
	{
		return;
	}

	user.m_IsMove = true;

	user.m_destPosX = posX;
	user.m_destPosY = posY;
	
	user.m_startMoveTick = nowTick;
	user.m_endMoveTick = nowTick + ( dist / MoveSpeed ) * 1000;
}

void Room::MoveCheckUsers( uint64 nowTick )
{
	Server_Move_Ack packet;

	for( auto& moveUser : m_roomUser )
	{
		RoomUser& user = moveUser.second;
		if( false == user.m_IsMove )
		{
			continue;
		}

		MovePosition( user, nowTick );

		// BroadCast
		if( false == packet.AddMoveData( user.m_seqID, user.m_posX, user.m_posY ) )
		{
			SendMoveData( packet );
			packet.Clear();
			packet.AddMoveData( user.m_seqID, user.m_posX, user.m_posY );
		}
	}

	if( packet.GetCount() > 0 )
	{
		SendMoveData( packet );
	}
}

void Room::MovePosition( RoomUser& user, uint64 nowTick )
{
	if( false == user.m_IsMove )
	{
		return;
	}

	if( user.m_endMoveTick <= nowTick )
	{
		user.m_posX = user.m_destPosX;
		user.m_posY = user.m_destPosY;
		user.m_startMoveTick = nowTick;
		user.m_IsMove = false;

		return;
	}

	if( user.m_endMoveTick < user.m_startMoveTick )
	{
		user.m_IsMove = false;
		return;
	}

	uint64 totalTick = user.m_endMoveTick - user.m_startMoveTick;
	if( totalTick == 0 )
	{
		user.m_IsMove = false;
		return;
	}

	float ratio = ( float )( nowTick - user.m_startMoveTick ) / ( float )totalTick;

	user.m_posX = user.m_posX + ( user.m_destPosX - user.m_posX ) * ratio;
	user.m_posY = user.m_posY + ( user.m_destPosY - user.m_posY ) * ratio;
	user.m_startMoveTick = nowTick;

}

void Room::SendMoveData( Server_Move_Ack& movePacket )
{
	tuple<SendChunk, bool> check = ServiceManager::This()->MakeSendPacket( PacketType::PacketType_SERVER_MOVE, ( char* )&movePacket, movePacket.GetSize() );
	if( get<1>( check ) )
	{
		for( auto& users : m_roomUser )
		{
			users.second.m_session->InsertSendQueue( get<0>( check ) );
		}
	}
	else
	{
		cout << "[Error] MakeSendPacket - SendBuffer pool exhausted" << endl;
	}
}
