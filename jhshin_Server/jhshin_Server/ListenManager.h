#pragma once

#include "SessionData.h"
#include "SessionManager.h"
#include "SingletonTemplate.h"

class ListenManager : public SingleT< ListenManager >
{
public:
	void Initialize( int ThreadCount );
	bool Listen();

	bool Accept( int acceptCount );
	bool Accept( AcceptObject* acceptObject, bool popSession = true );

	void Error( AcceptObject* acceptObject );

	// 종료 1단계 - 유입 차단.
	void Shutdown();
	bool IsStopped() { return m_Stop; }

	IOCP& GetIOCP() { return m_iocp; }

	SOCKET GetSocket() { return m_Socket; }
	LPFN_GETACCEPTEXSOCKADDRS GetSocketAddrsFN() { return m_lpfnGetAcceptExSockaddrs; }

private:
	LPFN_ACCEPTEX m_lpfnAcceptEx = nullptr;
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockaddrs = nullptr;
	vector<AcceptObject*> m_AcceptObjects;

	SOCKET m_Socket = INVALID_SOCKET;
	IOCP m_iocp;

	// 워커 스레드가 완료를 처리하면서 읽는다.
	atomic<bool> m_Stop = false;
};
