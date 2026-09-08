#include "ServiceManager.h"
#include "ConfigManager.h"
#include "ListenManager.h"

int main()
{
	SocketUtill::Initialize();

	ServiceManager* serviceManager = ServiceManager::This();

	serviceManager->Initalize( 8, 1, 128 );
	serviceManager->Start();

	serviceManager->Join();
}
