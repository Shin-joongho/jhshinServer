#include "SessionData.h"

void SessionData::SetNetAddr( sockaddr_in& RemoteSockAddr )
{
	m_NetAddress.SetSockAddr_In( RemoteSockAddr );
}

bool SessionData::Recv( int transferByte )
{
	int divideByte = transferByte;

	// 한번에 여러번 올 수도 있음..!
	while( m_Recv.GetRecvBuffer().DivideBuffer( divideByte ) )
	{
		divideByte = 0;
		int iPacketSize = m_Recv.GetRecvBuffer().GetPacketID()._size + PacketID_SIZE;

		// 버퍼 복사해서 따로 사용할 예정

		//

		m_Recv.GetRecvBuffer().SetReadPos( iPacketSize );
	}

	return RecvStart();
}

bool SessionData::RecvStart()
{
	DWORD flag = 0;

	m_Recv.SetSession( shared_from_this() );

	m_Recv.GetWSABUF().buf = m_Recv.GetRecvBuffer().GetWriteBuffer();
	m_Recv.GetWSABUF().len = m_Recv.GetRecvBuffer().GetWriteBufferSize();

	int Result = WSARecv( m_Socket, &m_Recv.GetWSABUF(), 1, NULL, &flag, static_cast<LPWSAOVERLAPPED>( &m_Recv ) , NULL);
	if( Result == SOCKET_ERROR )
	{
		DWORD error = WSAGetLastError();
		if( error != ERROR_IO_PENDING )
		{
			// 종료
			m_Recv.SetSession( nullptr );
			return false;
		}
	}

	return true;
}

void SessionData::Reset()
{
	m_Recv.Initalize();
	m_Socket = INVALID_SOCKET;
	m_NetAddress.Clear();
}
