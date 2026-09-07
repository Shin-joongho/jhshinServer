#pragma once

#include "RSDefine.h"
#include "SingletonTemplate.h"

class ConfigManager : public SingleT< ConfigManager >
{
public:
	bool Init( std::wstring& wfilePath );

	bool ServerData();


	int GetServerPort() { return m_ServerPort; }
	int GetAcceptCount() { return m_Acceptcount; }

private:
	std::wstring m_wPath;
	int m_ServerPort = 0;
	int m_Acceptcount = 0;
};