#pragma once

#include "RSDefine.h"

class ConfigManager
{
public:
	static ConfigManager* This()
	{
		if( nullptr == m_ConfigManger )
		{
			m_ConfigManger = new ConfigManager();
		}

		return m_ConfigManger;
	}

	ConfigManager();
	~ConfigManager() {}

	bool Init( std::wstring& wfilePath );

	bool ServerData();


	int GetServerPort() { return m_ServerPort; }
	int GetAcceptCount() { return m_Acceptcount; }
private:
	inline static ConfigManager* m_ConfigManger = nullptr;
	std::wstring m_wPath;

	int m_ServerPort;
	int m_Acceptcount;
};