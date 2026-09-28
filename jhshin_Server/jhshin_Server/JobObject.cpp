#include "JobObject.h"
#include "Room.h"

void Job_Broadcast::Dispatch( Room* room )
{
	room->BroadCast( m_session, m_BroadcastReq );
}

void Job_Enter::Dispatch( Room* room )
{
	room->Enter( m_session );
}

void Job_Leave::Dispatch( Room* room )
{
	room->Leave( m_session );
}

void Job_Move::Dispatch( Room* room )
{
	room->Move( m_session, m_posX, m_posY );
}
