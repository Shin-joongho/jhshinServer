#include "ServiceManager.h"
#include "ListenManager.h"
#include "SessionManager.h"
#include "PacketHandler.h"
#include "RoomManager.h"

// 종료 신호
static HANDLE g_ShutdownEvent = nullptr;

// 신호만 남기고 즉시 반환하고, 실제 작업은 main 스레드가 한다.
static BOOL WINAPI ConsoleHandler( DWORD ctrlType )
{
	switch( ctrlType )
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		cout << "[Shutdown] 종료 요청" << endl;
		SetEvent( g_ShutdownEvent );
		return TRUE;
	}

	return FALSE;
}

int main()
{
	// Ctrl+C
	g_ShutdownEvent = CreateEvent( nullptr, TRUE, FALSE, nullptr );
	if( nullptr == g_ShutdownEvent )
	{
		cout << "[Error] CreateEvent" << endl;
		return 1;
	}

	SetConsoleCtrlHandler( ConsoleHandler, TRUE );

	SocketUtil::Initialize();

	ServiceManager* serviceManager = ServiceManager::Create();
	ListenManager* listenManager = ListenManager::Create();
	SessionManager* sessionManager = SessionManager::Create();
	RoomManager* roomManager = RoomManager::Create();
	if( PacketHandler::Init() )
	{
		serviceManager->Initialize( 8, 1, 128 );
		roomManager->Initialize( 5 );

		serviceManager->Start();

		serviceManager->StartMonitor( 2 );

		cout << "[Server] 실행 중. Ctrl+C 로 종료." << endl;

		// 대기 지점.
		WaitForSingleObject( g_ShutdownEvent, INFINITE );

		serviceManager->Shutdown();
	}


	roomManager->Release();
	serviceManager->Release();
	listenManager->Release();
	sessionManager->Release();

	SocketUtil::CleanUp();

	CloseHandle( g_ShutdownEvent );
	g_ShutdownEvent = nullptr;

	cout << "[Server] 종료 완료" << endl;
}
