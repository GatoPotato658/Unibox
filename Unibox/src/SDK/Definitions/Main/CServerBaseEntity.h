#pragma once
#include "../Interfaces.h"
#include "../Definitions.h"
#include "IServerEntity.h"

MAKE_SIGNATURE(CBaseEntity_GetClassName, "server.dll", "48 8B 91 ? ? ? ? 48 8D 05 ? ? ? ? 48 85 D2 48 0F 45 C2 C3 CC CC CC CC CC CC CC CC CC CC 8B 81", 0x0);

class CServerBaseEntity : public IServerEntity
{
public:
	OFFSET(m_iTeamNum, int, 772);
	SIGNATURE(GetClassName, const char*, CBaseEntity, this);
};