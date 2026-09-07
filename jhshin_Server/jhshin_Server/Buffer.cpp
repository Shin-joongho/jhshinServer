#include "Buffer.h"

void RecvBuffer::Initalize( int bufferSize )
{
    m_buffer.resize( bufferSize, 0 );
}

char* RecvBuffer::GetReadBuffer()
{
    return &m_buffer[m_readPos];
}

char* RecvBuffer::GetReadData()
{
    // 실제 데이터
    return &m_buffer[m_readPos + PacketID_SIZE];
}

char* RecvBuffer::GetWriteBuffer()
{
    return &m_buffer[m_writePos];
}

uint16 RecvBuffer::GetWriteBufferSize()
{
    return (uint16)m_buffer.size() - m_writePos;
}

uint16 RecvBuffer::GetBufferSize()
{
    return m_writePos - m_readPos;
}

bool RecvBuffer::DivideBuffer( int transferbyte )
{
    bool IsSuccess = true;

    m_writePos += transferbyte;
    if( GetBufferSize() >= PacketID_SIZE )
    {
        PacketID* packetID = reinterpret_cast<PacketID*>( GetReadBuffer() );
        m_PacketHeader = *packetID;
    }
    
    // 더 받아야하는지 확인 필요
    if( GetBufferSize() < PacketID_SIZE || m_PacketHeader._size > GetBufferSize() - PacketID_SIZE )
    {
        Clear();
        IsSuccess = false;
    }

    return IsSuccess;

}

void RecvBuffer::SetReadPos( int transferbyte )
{
    // readPos가 writePos를 넘으면 안됨
    if( transferbyte <= 0 || m_readPos + transferbyte > m_writePos )
    {
        return;
    }

    m_readPos += transferbyte;
    Clear();
}

bool RecvBuffer::IsCheckData()
{
    return m_readPos == m_writePos ? false : true;
}

void RecvBuffer::Clear()
{
    bool IsClear = false;
    // 만약 버퍼 사이즈를 초과해서 보내는 거라면 해당 패킷 포기
    if( m_PacketHeader._size > m_buffer.size() - PacketID_SIZE )
    {
        IsClear = true;
    }
    else
    {
        if( IsCheckData() )
        {
            // 데이터가 있으면 연속으로 더 받을 수 있게 앞으로 밀기
            memmove( &m_buffer[0], &m_buffer[m_readPos], GetBufferSize() );
            m_writePos -= m_readPos;
            m_readPos = 0;
            IsClear = false;
        }
        else
        {
            IsClear = true;
        }
    }

    if( IsClear )
    {
        // 데이터가 없으면 0으로 초기화
        m_readPos = 0;
        m_writePos = 0;
        m_PacketHeader._size = 0;
        m_PacketHeader._type = PacketType::PacketType_NULL;
    }
}

tuple<int, int> SendBuffer::CopyBuffer( const char* copyData, int sendSize )
{
    if( IsCopy( PacketID_SIZE + sendSize ) )
    {
        int ReturnPointer = m_pointer;

        // 패킷 헤더먼저 추가
        PacketID pi( PacketType::PacketType_Server, sendSize );
        int piSize = sizeof( pi );

        memcpy( &m_chunk[m_pointer], &pi, piSize );
        m_pointer += piSize;
        memcpy( &m_chunk[m_pointer], copyData, sendSize );
        m_pointer += sendSize;
        
        return make_tuple( ReturnPointer, sendSize + piSize );
    }

    return make_tuple( -1, 0 );
}

bool SendBuffer::IsCopy( int sendSize )
{
    return GetChunkSize() < m_pointer + sendSize ? false : true;
}
