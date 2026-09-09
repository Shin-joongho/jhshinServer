#include "SessionData.h"

void SessionData::SetNetAddr( sockaddr_in& RemoteSockAddr )
{
	m_NetAddress.SetSockAddr_In( RemoteSockAddr );
}

bool SessionData::Recv( int transferByte )
{
	int divideByte = transferByte;
	SessionDataRef thisSession = shared_from_this();

	// 한번에 여러번 올 수도 있음..!
	while( m_Recv.GetRecvBuffer().DivideBuffer( divideByte ) )
	{
		divideByte = 0;
		const uint16 bodySize = m_Recv.GetRecvBuffer().GetPacketID()._size;
		PacketType packetType = m_Recv.GetRecvBuffer().GetPacketID()._type;
		const int iPacketSize = bodySize + PacketID_SIZE;
		const char* body = m_Recv.GetRecvBuffer().GetReadData();

		if( false == PacketHandler::Dispatch( packetType, thisSession, body, bodySize ) )
		{
			return false;
		}

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

void SessionData::InsertSendQueue( SendChunk sendChunk )
{
	bool check = false;
	{
		lock_guard<mutex> lg( m_SendLock );
		m_SendQueue.push( sendChunk );

		if( false == m_SendFlag )
		{
			check = true;
			m_SendFlag = true;
		}
	}

	if( check )
	{
		Send();
	}
	
}

bool SessionData::Send()
{
	m_Send.Clear();

	{
		lock_guard<mutex> lg( m_SendLock );
		while( true )
		{
			if( false == m_SendQueue.empty() )
			{
				m_Send.AddSendChunk( m_SendQueue.front() );
				m_SendQueue.pop();
			}
			else
			{
				break;
			}
		}
	}

	{
		lock_guard<mutex> lg( m_SendLock );
		if( m_Send.Empty() )
		{
			m_SendFlag = false;
			return true;
		}
	}

	m_Send.SetSession( shared_from_this() );

	DWORD bytes = 0;
	int Result = WSASend( m_Socket, m_Send.GetWSABUFs(), m_Send.GetWSABUFSize(), &bytes, 0, static_cast<LPWSAOVERLAPPED>( &m_Send ), NULL);
	if( Result == SOCKET_ERROR )
	{
		DWORD error = WSAGetLastError();
		if( error != ERROR_IO_PENDING )
		{
			m_Send.Clear();
			{
				lock_guard<mutex> lg( m_SendLock );
				m_SendFlag = false;
			}
			
			return false;
		}
	}

	
	return true;
}

void SessionData::CheckSendComplete()
{
	bool check = false;
	{
		lock_guard<mutex> lg( m_SendLock );
		if( m_SendQueue.empty() )
		{
			m_SendFlag = false;
		}
		else
		{
			check = true;
		}
	}

	if( check )
	{
		Send();
	}

}

void SessionData::Reset()
{
	m_Recv.Initalize();
	m_Socket = INVALID_SOCKET;
	m_NetAddress.Clear();
	m_SendFlag = false;
	m_RoomID = -1;
}
