#pragma once

#include "ConfigManager.h"
#include "SessionData.h"
#include "SessionManager.h"
#include "SingletonTemplate.h"

class ListenManager : public SingleT< ListenManager >
{
public:
	void Initalize( int ThreadCount );
	bool Listen();

	bool Accept( int acceptCount );
	bool Accept( AcceptObject* acceptObject, bool popSession = true );

	void Error( AcceptObject* acceptObject );

	IOCP& GetIOCP() { return m_iocp; }

	SOCKET GetSocket() { return m_Socket; }
	LPFN_GETACCEPTEXSOCKADDRS GetSocketAddrsFN() { return m_lpfnGetAcceptExSockaddrs; }

private:
	LPFN_ACCEPTEX m_lpfnAcceptEx = nullptr;
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockaddrs = nullptr;
	vector<AcceptObject*> m_AcceptObjects;

	SOCKET m_Socket = INVALID_SOCKET;
	IOCP m_iocp;
};
