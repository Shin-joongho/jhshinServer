#pragma once

#include "IOCP.h"
#include "RSDefine.h"
#include "SocketUtill.h"

class SessionData;

class ServiceManager
{
public:
	static ServiceManager* This()
	{
		if( nullptr == m_ServiceManager )
		{
			m_ServiceManager = new ServiceManager();
		}

		return m_ServiceManager;
	}

	ServiceManager() {}
	~ServiceManager() {}

	void Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount );
	void Start();

	void AddIOCP( SessionData* session );

	bool InsertUserSession( SessionData* session );
	void EraseUserSession( SessionData* session );

	IOCP& GetIOCP() { return m_iocp; }

	void Join();

	void CloseSession( SessionData* session );

private:
	inline static ServiceManager* m_ServiceManager;

	IOCP m_iocp;

	mutex m_Lock;
	unordered_map<SOCKET, SessionData*> m_UserSession;
};

