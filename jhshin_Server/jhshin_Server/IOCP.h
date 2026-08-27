#pragma once
#pragma comment(lib, "ws2_32")
#include "RSDefine.h"
#include <WinSock2.h>
#include <thread>

enum IOCP_TYPE
{
	IOCP_TYPE_NONE,
	IOCP_TYPE_ACCEPT,
	IOCP_TYPE_RECV,
	IOCP_TYPE_SEND,
};

class IOCPObject : public OVERLAPPED
{
public:
	IOCPObject( SOCKET Socket ) : m_Socket( Socket ), m_IocpType( IOCP_TYPE_NONE )
	{

	}
	virtual ~IOCPObject() {}

	virtual void Execute() abstract;
	void SetType( IOCP_TYPE iocpType )
	{
		m_IocpType = iocpType;
	}
	IOCP_TYPE GetType() { return m_IocpType; }
	SOCKET GetSocket() { return m_Socket; }
private:
	SOCKET m_Socket;
	IOCP_TYPE m_IocpType;
};

class AcceptObject : public IOCPObject
{
public:
	AcceptObject( SOCKET socket ) : IOCPObject( socket )
	{
		SetType( IOCP_TYPE_ACCEPT );
		m_ByteRecv = 0;
		memset( m_OutputBuffer, 0, sizeof( m_OutputBuffer ) );
	}
	virtual ~AcceptObject() {}

	virtual void Execute() override
	{

	}

	void Clear()
	{
		m_ByteRecv = 0;
		memset( m_OutputBuffer, 0, sizeof( m_OutputBuffer ) );
	}

	char* GetBuffer() { return m_OutputBuffer; }
	DWORD* GetByteRecv() { return &m_ByteRecv;  }
private:
	char m_OutputBuffer[256];
	DWORD m_ByteRecv;
};


class IOCP
{
public:
	IOCP( int iThreadCount );
	~IOCP();

	void Init();
	void Worker();

private:
	HANDLE m_IOCPHandle;
	vector<thread*> m_vecThread;
	int m_iThreadCount;
};

