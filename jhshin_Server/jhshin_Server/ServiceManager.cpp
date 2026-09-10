#include "ServiceManager.h"

#include "ListenManager.h"
#include "SessionManager.h"

// 각 호출하는 스레드에서 마지막으로 사용한 SendBuffer를 가지고 요청오면 해당 버퍼에서 청크를 꺼내 사용
// 각 TLS에 접근하는건 해당 스레드 뿐이니 락 필요없고
// 풀에서 가져올때만 내부적인 락 사용
thread_local SendBufferRef LSendBuffer = nullptr;

bool ServiceManager::Initialize( int ServiceThreadCount, int ListenThreadCount, int AcceptCount )
{
	bool Result = false;
	ListenManager* listenManager = ListenManager::This();
	SessionManager::This()->Initialize( 10000 );
	m_UserSession.clear();
	m_iocp.Init( ServiceThreadCount );

	listenManager->Initialize( ListenThreadCount );

	Result = listenManager->Listen();
	if( Result )
	{
		Result = listenManager->Accept( AcceptCount );
	}

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
		// sleep_for 대신 종료 이벤트를 기다린다.
		// 타임아웃이면 평소대로 한 주기가 지난 것이고,
		// 시그널이면 종료 요청이라 최대 intervalSec 을 기다리지 않고 즉시 깨어난다.
		const DWORD waitResult = WaitForSingleObject( self->m_MonitorStop, intervalSec * 1000 );

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

		// 종료 요청이었어도 위에서 한 줄을 찍고 나간다.
		// 그 마지막 줄이 종료 직후의 세션/청크 잔량이라 드레인 진단에 쓸 수 있다.
		if( WAIT_OBJECT_0 == waitResult )
		{
			break;
		}
	}
}

void ServiceManager::StartMonitor( int intervalSec )
{
	if( 0 >= intervalSec )
	{
		return;
	}

	// 수동 리셋. StopMonitor 를 두 번 불러도 상태가 흔들리지 않는다.
	m_MonitorStop = CreateEvent( nullptr, TRUE, FALSE, nullptr );
	if( nullptr == m_MonitorStop )
	{
		cout << "[Error] CreateEvent - Monitor" << endl;
		return;
	}

	m_MonitorThread = thread( MonitorLoop, this, intervalSec );
}

void ServiceManager::StopMonitor()
{
	if( nullptr == m_MonitorStop )
	{
		return;
	}

	SetEvent( m_MonitorStop );

	if( m_MonitorThread.joinable() )
	{
		m_MonitorThread.join();
	}

	CloseHandle( m_MonitorStop );
	m_MonitorStop = nullptr;
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

// 종료 시퀀스.
// 커널이 OVERLAPPED 와 버퍼를 참조하는 동안에는 아무것도 해제할 수 없다.
// 커널이 놓는 시점은 완료 통지가 도착할 때이고, 그걸 꺼내려면 워커가 살아 있어야 한다.
// 그래서 워커가 제일 마지막에 죽는다 - 아래 순서는 전부 이 제약에서 나온다.
void ServiceManager::Shutdown()
{
	cout << "[Shutdown] 시작" << endl;

	// 유입 차단
	ListenManager::This()->Shutdown();
	SessionManager::This()->ClearWaitQueue();

	// 룸 정지
	RoomManager::This()->Join();

	// 세션 소켓 닫기
	vector<SessionDataRef> sessions;
	{
		lock_guard<mutex> lockGuard( m_Lock );
		sessions.reserve( m_UserSession.size() );
		for( auto& userSession : m_UserSession )
		{
			sessions.push_back( userSession.second );
		}
	}

	cout << "[Shutdown] 세션 " << sessions.size() << " 개 닫는 중" << endl;

	for( auto& session : sessions )
	{
		CloseSession( session );
	}

	// 워커 종료 신호
	m_iocp.Stop();
	ListenManager::This()->GetIOCP().Stop();

	// 워커 회수
	Join();

	// 모니터 회수
	StopMonitor();

	cout << "[Shutdown] 완료" << endl;
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
