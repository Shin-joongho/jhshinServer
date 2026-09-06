#pragma once

#include "ConfigManager.h"
#include "SessionData.h"
#include "SessionManager.h"

class ListenManager
{
public:
	static ListenManager* This()
	{
		if( nullptr == m_ListenManager )
		{
			m_ListenManager = new ListenManager();
		}

		return m_ListenManager;
	}

	ListenManager() 
	{
		m_Socket = INVALID_SOCKET;
	}
	~ListenManager() {}


	void Initalize( int ThreadCount );
	bool Listen();

	bool Accept( int acceptCount );
	bool Accept( AcceptObject* acceptObject, bool popSession = true );

	void Error( AcceptObject* acceptObject );

	IOCP& GetIOCP() { return m_iocp; }

	SOCKET GetSocket() { return m_Socket; }
	LPFN_GETACCEPTEXSOCKADDRS GetSocketAddrsFN() { return m_lpfnGetAcceptExSockaddrs; }
private:
	inline static ListenManager* m_ListenManager = nullptr;
	LPFN_ACCEPTEX m_lpfnAcceptEx = nullptr;
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockaddrs = nullptr;
	vector<AcceptObject*> m_AcceptObjects;

	SOCKET m_Socket;
	IOCP m_iocp;
};
