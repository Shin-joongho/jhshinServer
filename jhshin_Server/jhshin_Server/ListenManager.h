#pragma once
#include "SocketUtill.h"
#include "ConfigManager.h"
#include "IOCP.h"
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

	ListenManager() {}
	~ListenManager() {}


	void Initalize( int SessionCount, int ThreadCount );
	bool Listen();

	void Accept( int AcceptCount );
	void Accept( AcceptObject* AcceptObj );

private:
	inline static ListenManager* m_ListenManager = nullptr;
	LPFN_ACCEPTEX m_lpfnAcceptEx = nullptr;
	vector<AcceptObject*> m_AcceptObjects;

	IOCP m_iocp;
};
