#pragma once

#include "RSDefine.h"
#include "SocketUtill.h"
#include "Buffer.h"

class SessionData;

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
	IOCPObject() : m_IocpType( IOCP_TYPE::IOCP_TYPE_NONE )
	{
		m_Session = nullptr;
	}
	virtual ~IOCPObject() {}

	virtual void Execute( int transferByte ) abstract;
	void SetType( IOCP_TYPE iocpType )
	{
		m_IocpType = iocpType;
	}
	IOCP_TYPE GetType() { return m_IocpType; }

	SessionDataRef GetSession() { return m_Session; }
	void SetSession( SessionDataRef session ) { m_Session = session; }

	void ReleaseSession() { m_Session = nullptr; }

protected:
	SessionDataRef m_Session;

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

	char* GetBuffer() { return m_OutputBuffer; }
	DWORD* GetByteRecv() { return &m_ByteRecv;  }

private:
	char m_OutputBuffer[256];
	DWORD m_ByteRecv;
};

class RecvObject : public IOCPObject
{
public:
	RecvObject() 
	{
		SetType( IOCP_TYPE::IOCP_TYPE_RECV );
		Clear();
	}
	virtual ~RecvObject() {}

	void Initalize();

	virtual void Execute( int transferByte ) override;
	void Clear();

	WSABUF& GetWSABUF() { return m_wsabuf;  }
	RecvBuffer& GetRecvBuffer() { return m_RecvBuffer;  }
private:
	WSABUF m_wsabuf;
	RecvBuffer m_RecvBuffer;
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

	HANDLE GetIOCPHandle() { return m_IOCPHandle;  }

	void Join();
	
private:
	HANDLE m_IOCPHandle;
	vector<thread*> m_vecThread;
	int m_iThreadCount;
};

