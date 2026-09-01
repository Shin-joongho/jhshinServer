#pragma once
#pragma comment(lib, "ws2_32")
#include "RSDefine.h"
#include <WinSock2.h>
#include <thread>
#include "SessionData.h"

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

	IOCPObject( SOCKET Socket ) : m_Session( nullptr ), m_IocpType( IOCP_TYPE::IOCP_TYPE_NONE )
	{

	}
	virtual ~IOCPObject() {}

	virtual void Execute() abstract;
	void SetType( IOCP_TYPE iocpType )
	{
		m_IocpType = iocpType;
	}
	IOCP_TYPE GetType() { return m_IocpType; }
	SessionData* GetSession() { return m_Session; }
	void SetSession( SessionData* Session )
	{
		m_Session = Session;
	}
	
	void Clear()
	{
		m_Session = nullptr;
	}

private:
	SessionData* m_Session;
	IOCP_TYPE m_IocpType;
};

class AcceptObject : public IOCPObject
{
public:
	AcceptObject( SOCKET socket ) : IOCPObject( socket )
	{
		SetType( IOCP_TYPE::IOCP_TYPE_ACCEPT );
		Clear();
	}
	virtual ~AcceptObject() {}

	virtual void Execute() override
	{
		sockaddr* pLocalAddr = nullptr;
		sockaddr* pRemoteAddr = nullptr;
		int LocalLen = 0;
		int RemoteLen = 0;

		GetAcceptExSockaddrs( m_OutputBuffer, 0, sizeof( SOCKADDR_IN ) + 16, sizeof( SOCKADDR_IN ) + 16, &pLocalAddr, &LocalLen, &pRemoteAddr, &RemoteLen );

		SOCKADDR_IN RemoteSockAddr;
		memcpy_s( &RemoteSockAddr, sizeof( RemoteSockAddr), reinterpret_cast< SOCKADDR_IN* >( pRemoteAddr ), sizeof( SOCKADDR_IN ) );

		SessionData* thisSesssion = GetSession();
		if( thisSesssion )
		{
			thisSesssion->SetNetAddr( RemoteSockAddr );
		}
		
	}

	void Clear()
	{
		this->IOCPObject::Clear();
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
	static void Worker();

private:
	HANDLE m_IOCPHandle;
	vector<thread*> m_vecThread;
	int m_iThreadCount;
};

