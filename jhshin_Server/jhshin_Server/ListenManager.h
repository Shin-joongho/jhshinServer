#pragma once

#include "IOCP.h"
#include "RSDefine.h"
#include "SocketUtill.h"

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


	void Initalize( int ThreadCount );
	bool Listen();

	void Accept( int acceptCount );
	void Accept( AcceptObject* acceptObject, bool popSession = true );

	void Error( AcceptObject* acceptObject );

	IOCP& GetIOCP() { return m_iocp; }

private:
	inline static ListenManager* m_ListenManager = nullptr;
	LPFN_ACCEPTEX m_lpfnAcceptEx = nullptr;
	vector<AcceptObject*> m_AcceptObjects;

	IOCP m_iocp;
};
