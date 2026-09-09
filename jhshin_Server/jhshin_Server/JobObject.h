#pragma once
#include "RSDefine.h"
#include "PacketStruct.h"
#include "Room.h"

class JobObject : public enable_shared_from_this<JobObject>
{
public:
	JobObject() {}
	virtual ~JobObject() {}

	virtual void Dispatch( class Room* room ) abstract;

	void SetSession( SessionDataRef session ) { m_session = session; }
	SessionDataRef GetSession() { return m_session; }

protected:
	SessionDataRef m_session = nullptr;
};

class Job_Echo : public JobObject
{
public:
	virtual void Dispatch( class Room* room ) override;

private:
	Client_ECHO_Req m_EchoReq;
};

class Job_Broadcast : public JobObject
{
public:
	virtual void Dispatch( class Room* room ) override;

private:
	Client_Broadcast_Req m_BroadcastReq;
};