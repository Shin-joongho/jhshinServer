#pragma once
#include "SocketUtill.h"
#include "ConfigManager.h"
#include "IOCP.h"
#include "MemoryPool.h"

class ListenManager : public IOCP
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

	void Initalize( int SessionCount );
	bool Listen();

	void Accept( int AcceptCount );
	void Accept( shared_ptr<AcceptObject>& AcceptObj );

	SessionData* PopSession();
	void PushSession( SessionData* Session );

private:
	inline static ListenManager* m_ListenManager = nullptr;
	LPFN_ACCEPTEX m_lpfnAcceptEx = nullptr;
	vector<shared_ptr<AcceptObject>> m_AcceptObjects;
	MemoryPool<SessionData> m_SessionPools;
};
