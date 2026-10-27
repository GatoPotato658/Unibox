#include "CEntityMapData.h"
#include "../Interfaces.h"
MAKE_SIGNATURE(CEntityMapData_GetFirstKey, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 81 EC ? ? ? ? 48 8B 31", 0x0);
MAKE_SIGNATURE(CEntityMapData_GetNextKey, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 81 EC ? ? ? ? 48 8B 71", 0x0);

bool CEntityMapData::GetFirstKey(char* keyName, char* Value)
{
    return S::CEntityMapData_GetFirstKey.Call<bool>(this, keyName, Value);
}

bool CEntityMapData::GetNextKey(char* keyName, char* Value)
{
    return S::CEntityMapData_GetNextKey.Call<bool>(this, keyName, Value);
}