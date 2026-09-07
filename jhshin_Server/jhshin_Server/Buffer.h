#pragma once

#include "RSDefine.h"


enum class PacketType : uint16
{
	PacketType_NULL = 0,
	PacketType_Server,
	PacketType_Client,
};

struct PacketID
{
	PacketType _type;
	// 헤더 크기를 뺀 실제 데이터 크기
	uint16 _size;
	
	PacketID() 
	{
		_type = PacketType::PacketType_NULL;
		_size = 0;
	}
	PacketID( PacketType type, uint16 size )
	{
		_type = type;
		_size = size;
	}
};

const uint16 PacketID_SIZE = sizeof( PacketID );

class RecvBuffer
{
public:
	RecvBuffer() 
	{
		m_buffer.clear();
		m_writePos = 0;
		m_readPos = 0;
		m_PacketHeader._type = PacketType::PacketType_NULL;
		m_PacketHeader._size = 0;
	}
	~RecvBuffer()
	{
		m_buffer.clear();
	}

	void Initalize( int bufferSize );

	char* GetReadBuffer();
	char* GetReadData();

	char* GetWriteBuffer();
	uint16 GetWriteBufferSize();

	uint16 GetBufferSize();


	bool DivideBuffer( int transferbyte );
	void SetReadPos( int transferbyte );

	bool IsCheckData();
	void Clear();

	PacketID& GetPacketID() { return m_PacketHeader; }

private:
	vector<char> m_buffer;
	uint16 m_writePos;
	uint16 m_readPos;
	PacketID m_PacketHeader;
};

// Send는 세션별이 아니라 Room이나 Map기준으로 보냄
class SendBuffer : public enable_shared_from_this<SendBuffer>
{
public:
	SendBuffer() {}
	~SendBuffer() {}

	tuple<int, int> CopyBuffer( const char* copyData, int sendSize );
	int GetPointer() { return m_pointer; }
	int GetChunkSize() { return sizeof( m_chunk ); }
	bool IsCopy( int sendSize );

	char* GetSendBuffer( int pointer ) { return &m_chunk[pointer]; }
	void Reset()
	{
		memset( m_chunk, 0, sizeof( m_chunk ) );
		m_pointer = 0;
	}

private:
	char m_chunk[PACKET_SIZE * 10] = {};
	int m_pointer = 0;
};