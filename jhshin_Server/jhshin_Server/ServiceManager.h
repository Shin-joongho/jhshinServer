#pragma once

#include "IOCP.h"
#include "RSDefine.h"
#include "SocketUtill.h"
#include "SingletonTemplate.h"

class ServiceManager : public SingleT< ServiceManager >
{
public:
	ServiceManager() {}
	~ServiceManager() {}

	bool Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount );
	void Start();

	void AddIOCP( SessionData* session );

	bool InsertUserSession( SessionDataRef session );
	void EraseUserSession( SessionDataRef session );

	IOCP& GetIOCP() { return m_iocp; }

	void Join();

	void CloseSession( SessionDataRef session );

private:
	IOCP m_iocp;

	mutex m_Lock;
	unordered_map<SOCKET, SessionDataRef> m_UserSession;
};

