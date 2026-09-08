#include "ServiceManager.h"
#include "ConfigManager.h"
#include "ListenManager.h"

int main()
{
	SocketUtill::Initialize();

	ServiceManager* serviceManager = ServiceManager::This();

	serviceManager->Initalize( 8, 1, 128 );
	serviceManager->Start();

	serviceManager->StartMonitor( 2 );   // 2초마다 CPU / 접속 / 풀 상태 출력

	serviceManager->Join();
}
