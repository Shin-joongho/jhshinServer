#pragma once

#include "PacketStruct.h"

#include "RSDefine.h"

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

class Job_Broadcast : public JobObject
{
public:
	virtual void Dispatch( class Room* room ) override;

	void SetReq( Client_Broadcast_Req req ) { m_BroadcastReq = req; }
	Client_Broadcast_Req GetReq() { return m_BroadcastReq; }

private:
	Client_Broadcast_Req m_BroadcastReq;
};

class Job_Enter : public JobObject
{
public:
	virtual void Dispatch( class Room* room ) override;

private:
};


class Job_Leave : public JobObject
{
public:
	virtual void Dispatch( class Room* room ) override;

private:
};