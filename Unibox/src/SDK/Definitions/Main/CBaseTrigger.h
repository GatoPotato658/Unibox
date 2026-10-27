#pragma once
#include "CBaseToggle.h"

MAKE_SIGNATURE( CBaseTrigger_PointIsWithin, "server.dll", "48 81 EC ? ? ? ? F3 0F 10 02 4C 8D 89", 0x0 );

class CBaseTrigger : public CBaseToggle
{
public:
	SIGNATURE_ARGS(PointIsWithin, bool, CBaseTrigger, (const Vector& vPoint), this, std::ref(vPoint));
};