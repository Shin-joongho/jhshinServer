#include "ServiceManager.h"

#include "ListenManager.h"
#include "SessionManager.h"

#include <chrono>
#include <cstdio>
#include <Windows.h>

// 각 호출하는 스레드에서 마지막으로 사용한 SendBuffer를 가지고 요청오면 해당 버퍼에서 청크를 꺼내 사용
// 각 TLS에 접근하는건 해당 스레드 뿐이니 락 필요없고
// 풀에서 가져올때만 내부적인 락 사용
thread_local SendBufferRef LSendBuffer;

bool ServiceManager::Initalize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount )
{
	bool Result = false;
	ListenManager* listenManager = ListenManager::This();
	SessionManager::This()->Initalize( 10000 );
	m_UserSession.clear();
	m_iocp.Init( ServiceThreadCount );

	listenManager->Initalize( ListenThreadCount );

	Result = listenManager->Listen();
	if( Result )
	{
		Result = listenManager->Accept( AcceptCount );
	}

	// 테스트용 고정치
	m_SendBuffer.InitObjectPool( 1024 );

	return Result;
}

void ServiceManager::Start()
{
	m_iocp.Start();
}

// 이 프로세스가 지금까지 쓴 CPU 시간(커널+유저)을 100ns 단위로 돌려준다.
static ULONGLONG GetProcessCpu100ns()
{
	FILETIME createTime = {};
	FILETIME exitTime = {};
	FILETIME kernelTime = {};
	FILETIME userTime = {};

	if( FALSE == GetProcessTimes( GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime ) )
	{
		return 0;
	}

	ULARGE_INTEGER k = {};
	ULARGE_INTEGER u = {};
	k.LowPart = kernelTime.dwLowDateTime;
	k.HighPart = kernelTime.dwHighDateTime;
	u.LowPart = userTime.dwLowDateTime;
	u.HighPart = userTime.dwHighDateTime;

	return k.QuadPart + u.QuadPart;
}

void ServiceManager::MonitorLoop( ServiceManager* self, int intervalSec )
{
	const unsigned int cores = thread::hardware_concurrency();

	ULONGLONG prevCpu = GetProcessCpu100ns();
	auto      prevAt = chrono::steady_clock::now();

	while( true )
	{
		this_thread::sleep_for( chrono::seconds( intervalSec ) );

		const ULONGLONG nowCpu = GetProcessCpu100ns();
		const auto      nowAt = chrono::steady_clock::now();

		const double wallSec = chrono::duration<double>( nowAt - prevAt ).count();
		const double cpuSec = ( nowCpu - prevCpu ) / 10000000.0;   // 100ns -> 초
		const double usedCores = ( wallSec > 0.0 ) ? cpuSec / wallSec : 0.0;

		prevCpu = nowCpu;
		prevAt = nowAt;

		size_t connCount = 0;
		{
			lock_guard<mutex> lockGuard( self->m_Lock );
			connCount = self->m_UserSession.size();
		}

		const int sessionFree = SessionManager::This()->GetSessionCount();
		const int sessionMax = SessionManager::This()->GetSessionMaxCount();
		const int chunkFree = self->m_SendBuffer.GetFreeCount();
		const int chunkMax = self->m_SendBuffer.GetMaxCount();

		// 콘솔 코드페이지에 의존하지 않도록 출력은 ASCII 로 둔다.
		printf( "[Monitor] cpu %5.2f/%u cores (%4.1f%%) | conn %5zu | session %d/%d | chunk %d/%d\n",
				usedCores,
				cores,
				( cores > 0 ) ? ( usedCores / cores * 100.0 ) : 0.0,
				connCount,
				sessionMax - sessionFree, sessionMax,
				chunkMax - chunkFree, chunkMax );

		// 파이프나 파일로 리다이렉트되면 블록 버퍼링이라
		// 프로세스가 죽을 때 버퍼가 통째로 사라진다.
		fflush( stdout );
	}
}

void ServiceManager::StartMonitor( int intervalSec )
{
	if( 0 >= intervalSec )
	{
		return;
	}

	// 종료 경로가 아직 없으므로 detach 한다.
	// graceful shutdown 을 넣을 때 종료 플래그와 join 으로 바꿔야 한다.
	thread monitor( MonitorLoop, this, intervalSec );
	monitor.detach();
}

void ServiceManager::AddIOCP( SessionData* session )
{
	if( nullptr == session )
	{
		return;
	}

	m_iocp.AddIOCP( session->GetSocket() );

	InsertUserSession( session->shared_from_this() );
}

bool ServiceManager::InsertUserSession( SessionDataRef session )
{
	bool Result = false;

	if( session )
	{
		lock_guard<mutex> lockGuard( m_Lock );
		Result = m_UserSession.insert( make_pair( session->GetSocket(), session ) ).second;
	}

	return Result;
}

void ServiceManager::EraseUserSession( SessionDataRef session )
{
	if( session )
	{
		lock_guard<mutex> lockGuard( m_Lock );
		m_UserSession.erase( session->GetSocket() );
	}
}

void ServiceManager::Join()
{
	m_iocp.Join();
	ListenManager::This()->GetIOCP().Join();
}


void ServiceManager::CloseSession( SessionDataRef session )
{
	if( session )
	{
		closesocket( session->GetSocket() );
		EraseUserSession( session );
	}
}

tuple<SendChunk, bool>  ServiceManager::MakeSendPacket( PacketType packetType, const char* sendData, const int sendSize )
{
	SendChunk sendChunk;

	if( nullptr == LSendBuffer )
	{
		LSendBuffer = GetSendBuffer();
		if( nullptr == LSendBuffer )
		{
			// 풀 부족
			return make_tuple( sendChunk, false );
		}
	}

	tuple<int, int> tp = LSendBuffer->CopyBuffer( packetType, sendData, sendSize );
	int bufferPointer = get<0>( tp );
	int totalSize = get<1>( tp );
	if( bufferPointer == -1 )
	{
		// 현재 청크에 자리가 없다. 새 청크를 받아 재시도.
		LSendBuffer = GetSendBuffer();
		if( nullptr == LSendBuffer )
		{
			// 풀 부족
			return make_tuple( sendChunk, false );
		}

		tp = LSendBuffer->CopyBuffer( packetType, sendData, sendSize );
		bufferPointer = get<0>( tp );
		totalSize = get<1>( tp );

		if( bufferPointer == -1 )
		{
			// 빈 청크에도 안 들어간다 = 청크 크기를 넘는 패킷
			return make_tuple( sendChunk, false );
		}
	}

	sendChunk.Set( LSendBuffer, LSendBuffer->GetSendBuffer( bufferPointer ), totalSize );

	return make_tuple( sendChunk, true );
}

SendBufferRef ServiceManager::GetSendBuffer()
{
	SendBuffer* sendBuffer = m_SendBuffer.Pop();
	if( sendBuffer )
	{
		return SendBufferRef( sendBuffer, [this]( SendBuffer* psendBuffer ) { this->m_SendBuffer.Push( psendBuffer ); } );
	}
	else
	{
		return nullptr;
	}
}
