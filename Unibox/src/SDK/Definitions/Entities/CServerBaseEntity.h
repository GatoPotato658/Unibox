#pragma once
#include "../Interfaces.h"
#include "../Definitions.h"
#include "../Engine/IServerEntity.h"


class CServerBaseEntity : public IServerEntity
{
public:
	OFFSET_SIG(m_iTeamNum, int, CServerBaseEntity_m_iTeamNumOffset, int, 0);
	SIGNATURE(GetClassName, const char*, CBaseEntity, this);
};