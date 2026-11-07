#pragma once
#include "CBaseEntity.h"

class CClient_Precipitation : public CBaseEntity
{
public:
	NETVAR(m_nPrecipType, int, "CPrecipitation", "m_nPrecipType");
};