#include "CEntityMapData.h"
#include "../Interfaces.h"

bool CEntityMapData::GetFirstKey(char* keyName, char* Value)
{
    return S::CEntityMapData_GetFirstKey.Call<bool>(this, keyName, Value);
}

bool CEntityMapData::GetNextKey(char* keyName, char* Value)
{
    return S::CEntityMapData_GetNextKey.Call<bool>(this, keyName, Value);
}