#include "IOCP.h"

IOCP::IOCP( int iThreadCount ) : m_iThreadCount(iThreadCount)
{
	m_vecThread.clear();
	m_vecThread.reserve( iThreadCount );
}

IOCP::~IOCP()
{
}

void IOCP::Init()
{
	m_IOCPHandle = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, m_iThreadCount );

	for( int i = 0; i < m_iThreadCount; ++i )
	{
		thread* tr = new thread( Worker );
		m_vecThread[i] = tr;
	}
}

void IOCP::Worker()
{
}
