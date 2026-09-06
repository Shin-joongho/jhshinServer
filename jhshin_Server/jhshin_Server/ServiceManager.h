#pragma once

#include "IOCP.h"
#include "RSDefine.h"
#include "SocketUtill.h"

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

	bool Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount );
	void Start();

	void AddIOCP( SessionData* session );

	bool InsertUserSession( SessionDataRef session );
	void EraseUserSession( SessionDataRef session );

	IOCP& GetIOCP() { return m_iocp; }

	void Join();

	void CloseSession( SessionDataRef session );

private:
	inline static ServiceManager* m_ServiceManager;
	IOCP m_iocp;

	mutex m_Lock;
	unordered_map<SOCKET, SessionDataRef> m_UserSession;
};

