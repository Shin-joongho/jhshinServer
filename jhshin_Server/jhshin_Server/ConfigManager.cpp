#include "ConfigManager.h"

ConfigManager::ConfigManager()
    : m_wPath( L"" ), m_ServerPort( 0 )
{
}

bool ConfigManager::Init( std::wstring& wfilePath )
{
    wchar_t wPath[MAX_PATH];
    GetModuleFileNameW( nullptr, wPath, MAX_PATH );
    wchar_t* point = wcsrchr( wPath, L'\\' );
    if( point )
    {
        wcscpy_s( point + 1, MAX_PATH - ( point + 1 - wPath ), wfilePath.c_str() );
    }
    m_wPath = wPath;

    ServerData();

    return false;
}

bool ConfigManager::ServerData()
{

    int ServerPort = GetPrivateProfileIntW( L"Server", L"port", 0, m_wPath.c_str() );
    if( 0 == ServerPort )
    {
        return false;
    }

    m_ServerPort = ServerPort;

    int AcceptCount = GetPrivateProfileIntW( L"Server", L"acceptCount", 0, m_wPath.c_str() );
    if( 0 == AcceptCount )
    {
        return false;
    }

    m_Acceptcount = AcceptCount;

    return true;
}
