#include "ServiceManager.h"
#include "ConfigManager.h"
#include "ListenManager.h"
#include "SessionManager.h"
#include "PacketHandler.h"

int main()
{
	SocketUtill::Initialize();

	ServiceManager* serviceManager = ServiceManager::Create();
	ListenManager* listenManager = ListenManager::Create();
	SessionManager* sessionManager = SessionManager::Create();

	if( PacketHandler::Init() )
	{
		serviceManager->Initalize( 8, 1, 128 );
		serviceManager->Start();

		serviceManager->StartMonitor( 2 );

		serviceManager->Join();
	}

	serviceManager->Release();
	listenManager->Release();
	sessionManager->Release();

}
