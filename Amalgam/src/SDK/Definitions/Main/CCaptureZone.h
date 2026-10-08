#pragma once
#include "CBaseEntity.h"

class CCaptureZone : public CBaseEntity
{
public:
	NETVAR(m_bDisabled, bool, "CCaptureZone", "m_bDisabled");
};
