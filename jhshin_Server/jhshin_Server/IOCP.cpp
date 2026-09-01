#include "IOCP.h"

IOCP::IOCP()
{
	m_vecThread.clear();
}

IOCP::IOCP( int iThreadCount )
{
	Init( iThreadCount );
}

IOCP::~IOCP()
{
}

void IOCP::Init( int iThreadCount )
{
	m_iThreadCount = iThreadCount;

	m_vecThread.clear();
	m_vecThread.reserve( m_iThreadCount );

	m_Socket = SocketUtill::MakeSocket();
	if( m_Socket == SOCKET_ERROR )
	{
		return;
	}

	int OptionVal = 1 << eSocketOption_NoDelay | 1 << eSocketOption_ReUseAddr;

	SocketUtill::SetOptions( m_Socket, OptionVal );

	m_IOCPHandle = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 0 );

	for( int i = 0; i < m_iThreadCount; ++i )
	{
		thread* tr = new thread( Worker, this );
		m_vecThread.push_back( tr );
	}

	Join();

	return;
}

void IOCP::AddIOCP( SOCKET socket )
{
	CreateIoCompletionPort( (HANDLE)socket, m_IOCPHandle, 0, 0 );

	return;;
}

void IOCP::Worker( IOCP* thisIOCP )
{
	DWORD lptransferByte;
	IOCPObject* iocpObject;
	ULONG_PTR key = 0;
	while( true )
	{
		lptransferByte = 0;
		iocpObject = nullptr;

		if( GetQueuedCompletionStatus( thisIOCP->GetIOCPHandle(), &lptransferByte, &key, static_cast<LPOVERLAPPED*>( &iocpObject ), INFINITE ) )
		{

		}
	}
}

void IOCP::Join()
{
	for( std::thread* t : m_vecThread )
	{
		if( t->joinable() )
			t->join();
	}
	m_vecThread.clear();
}
