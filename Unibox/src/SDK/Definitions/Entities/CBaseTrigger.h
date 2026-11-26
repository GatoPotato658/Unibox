#pragma once
#include "CBaseToggle.h"


class CBaseTrigger : public CBaseToggle
{
public:
	SIGNATURE_ARGS(PointIsWithin, bool, CBaseTrigger, (const Vector& vPoint), this, std::ref(vPoint));
};