#pragma once
#include "SocketUtill.h"
#include "ConfigManager.h"
#include "IOCP.h"
#include "MemoryPool.h"

class ListenManager
{
public:
	ListenManager() {}
	~ListenManager() {}

	static ListenManager* This()
	{
		if( nullptr == m_ListenManager )
		{
			m_ListenManager = new ListenManager();
		}

		return m_ListenManager;
	}

	void Initalize();
	bool Listen();

	void Accept( int AcceptCount );
	void Accept( shared_ptr<AcceptObject>& AcceptObj );

	SessionData* PopSession();

private:
	inline static ListenManager* m_ListenManager = nullptr;
	SOCKET m_Socket;
	LPFN_ACCEPTEX m_lpfnAcceptEx = nullptr;
	HANDLE m_ListenHandle;
	vector<shared_ptr<AcceptObject>> m_AcceptObjects;
	MemoryPool<SessionData> m_SessionPools;
};

