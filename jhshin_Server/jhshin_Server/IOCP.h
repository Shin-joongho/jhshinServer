#pragma once
#pragma comment(lib, "ws2_32")
#include <WinSock2.h>
#include <thread>

#include "SessionManager.h"
#include "ServiceManager.h"
#include "ListenManager.h"

#include "RSDefine.h"

enum class IOCP_TYPE
{
	IOCP_TYPE_NONE,
	IOCP_TYPE_ACCEPT,
	IOCP_TYPE_RECV,
	IOCP_TYPE_SEND,
};

class IOCPObject : public OVERLAPPED
{
public:
	friend class SessionData;

	IOCPObject() : m_IocpType( IOCP_TYPE::IOCP_TYPE_NONE )
	{

	}
	virtual ~IOCPObject() {}

	virtual void Execute( int transferByte ) abstract;
	void SetType( IOCP_TYPE iocpType )
	{
		m_IocpType = iocpType;
	}
	IOCP_TYPE GetType() { return m_IocpType; }

private:
	IOCP_TYPE m_IocpType;
};

class AcceptObject : public IOCPObject
{
public:
	AcceptObject()
	{
		SetType( IOCP_TYPE::IOCP_TYPE_ACCEPT );
		Clear();
	}
	virtual ~AcceptObject() {}

	virtual void Execute( int transferByte ) override;
	void Clear();

	SessionData* GetSession() { return m_Session; }
	void SetSession( SessionData* session ) { m_Session = session; }

	char* GetBuffer() { return m_OutputBuffer; }
	DWORD* GetByteRecv() { return &m_ByteRecv;  }
private:
	SessionData* m_Session;
	char m_OutputBuffer[256];
	DWORD m_ByteRecv;
};

class RecvObject : public IOCPObject
{
public:
	RecvObject() {}
	virtual ~RecvObject() {}

	void Initalize( SessionData* session );

	virtual void Execute( int transferByte ) override;
	void Clear();

	WSABUF& GetWSABUF() { return m_wsabuf;  }
	char* GetRecvBuffer() { return m_RecvBuffer;  }

private:
	WSABUF m_wsabuf;
	char m_RecvBuffer[4056];
	SessionData* m_Session = nullptr;
};


class IOCP
{
public:
	IOCP();
	IOCP( int iThreadCount );
	~IOCP();

	void Init( int iThreadCount );
	void AddIOCP( SOCKET socket );
	void Start();
	static void Worker( IOCP* thisIOCP );

	SOCKET GetSocket() { return m_Socket;  }
	HANDLE GetIOCPHandle() { return m_IOCPHandle;  }

	void Join();
	
protected:
	SOCKET m_Socket;

private:
	HANDLE m_IOCPHandle;
	vector<thread*> m_vecThread;
	int m_iThreadCount;
};

