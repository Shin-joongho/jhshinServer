#include "ServiceManager.h"
#include "ConfigManager.h"
#include "ListenManager.h"
#include "SessionManager.h"
#include "PacketHandler.h"
#include "RoomManager.h"

int main()
{
	SocketUtill::Initialize();

	ServiceManager* serviceManager = ServiceManager::Create();
	ListenManager* listenManager = ListenManager::Create();
	SessionManager* sessionManager = SessionManager::Create();
	RoomManager* roomManager = RoomManager::Create();
	if( PacketHandler::Init() )
	{
		serviceManager->Initalize( 8, 1, 128 );
		roomManager->Initalize( 5 );

		serviceManager->Start();

		serviceManager->StartMonitor( 2 );

		serviceManager->Join();
	}

	serviceManager->Release();
	listenManager->Release();
	sessionManager->Release();
	roomManager->Release();

	SocketUtill::CleanUp();
}
