#pragma once

#include "RSDefine.h"
#include "SocketUtil.h"
#include "Buffer.h"

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
	// 여기 포인터로 받아야할듯
	SessionDataRef m_Session;

private:
	IOCP_TYPE m_IocpType;
	
};

class AcceptObject : public IOCPObject
{
public:
	AcceptObject()
	{
		Clear();
		SetType( IOCP_TYPE::IOCP_TYPE_ACCEPT );
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
		Clear();
		SetType( IOCP_TYPE::IOCP_TYPE_RECV );
	}
	virtual ~RecvObject() {}

	void Initialize();

	virtual void Execute( int transferByte ) override;
	void Clear();

	WSABUF& GetWSABUF() { return m_wsabuf;  }
	RecvBuffer& GetRecvBuffer() { return m_RecvBuffer;  }

private:
	WSABUF m_wsabuf;
	RecvBuffer m_RecvBuffer;
};

struct SendChunk
{
	SendBufferRef m_sendBuffer;
	char* m_buffer;
	int m_size;

	void Set( SendBufferRef sendBuffer, char* buffer, int size )
	{
		m_sendBuffer = sendBuffer;
		m_buffer = buffer;
		m_size = size;
	}
};

class SendObject : public IOCPObject
{
public:
	SendObject()
	{
		Clear();
		SetType( IOCP_TYPE::IOCP_TYPE_SEND );
	}
	virtual ~SendObject() {}
	virtual void Execute( int transferByte ) override;

	void Clear();

	WSABUF* GetWSABUFs() { return m_wsabufs.data(); }
	int GetWSABUFSize() { return (int)m_wsabufs.size(); }

	bool Empty() { return m_SendChunks.empty(); }

	void AddSendChunk( SendChunk& sendChunk );
private:
	vector<WSABUF> m_wsabufs;
	vector<SendChunk> m_SendChunks;
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
	void Stop();
	
private:
	HANDLE m_IOCPHandle;
	vector<thread*> m_vecThread;
	int m_iThreadCount;
	atomic<bool> m_Stop;
};

