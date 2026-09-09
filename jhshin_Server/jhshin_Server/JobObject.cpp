#include "JobObject.h"

void Job_Broadcast::Dispatch( Room* room )
{
	room->BroadCast( m_session, m_BroadcastReq );
}

void Job_Echo::Dispatch( Room* room )
{
}
