#include "ServiceManager.h"
#include "ConfigManager.h"
#include "ListenManager.h"

int main()
{
	SocketUtill::Initialize();

	ServiceManager* serviceManager = ServiceManager::This();

	serviceManager->Initalize( 10, 1, 10 );
	serviceManager->Start();

	serviceManager->Join();
}
