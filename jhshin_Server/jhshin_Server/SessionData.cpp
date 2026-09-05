#include "SessionData.h"

#include "IOCP.h"

void SessionData::SetNetAddr( sockaddr_in& RemoteSockAddr )
{
	m_NetAddress.SetSockAddr_In( RemoteSockAddr );
}

bool SessionData::RecvStart()
{
	DWORD flag = 0;
	m_Recv.Clear();
	int Result = WSARecv( m_Socket, &m_Recv.GetWSABUF(), 1, NULL, &flag, static_cast<LPWSAOVERLAPPED>( &m_Recv ) , NULL);
	if( Result == SOCKET_ERROR )
	{
		DWORD error = WSAGetLastError();
		if( error != ERROR_IO_PENDING )
		{
			// Á¾·á
			return false;
		}
	}

	return true;
}

void SessionData::Reset()
{
	m_Recv.Initalize( this );
	m_Socket = INVALID_SOCKET;
	m_NetAddress.Clear();
}
