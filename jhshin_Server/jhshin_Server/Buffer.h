#pragma once

#include "RSDefine.h"


enum class PacketType : uint16
{
	PacketType_NULL = 0,
	PacketType_Recv,
	PacketType_Send,
};

struct PacketID
{
	PacketType _type;
	uint16 _size;
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

class SendBuffer : public enable_shared_from_this<SendBuffer>
{
public:
	SendBuffer() {}
	~SendBuffer() {}

private:

};