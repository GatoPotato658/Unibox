#pragma once
#include "CBaseEntity.h"

class CTFRobotDestruction_Robot : public CBaseEntity
{
public:
	NETVAR(m_iHealth, int, "CTFRobotDestruction_Robot", "m_iHealth");
	NETVAR(m_iMaxHealth, int, "CTFRobotDestruction_Robot", "m_iMaxHealth");
};
