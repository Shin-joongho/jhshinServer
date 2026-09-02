#pragma once

#include "IOCP.h"
#include "ObjectPool.h"

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

	void Initalize( int iThreadCount );
	void Start();

	void AddIOCP( SessionData* session );

	bool InsertUserSession( SessionData* session );
	void EraseUserSession( SessionData* session );

public:
	inline static ServiceManager* m_ServiceManager;

	IOCP m_iocp;

	mutex m_Lock;
	unordered_map<SOCKET, SessionData*> m_UserSession;
};

