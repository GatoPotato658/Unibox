#pragma once
#include "CBaseAnimating.h"

class CEconEntity : public CBaseAnimating
{
public:
	NETVAR(m_iItemDefinitionIndex, int, "CEconEntity", "m_iItemDefinitionIndex");
	NETVAR(m_iEntityQuality, int, "CEconEntity", "m_iEntityQuality");

	VIRTUAL(UpdateAttachmentModels, void, 213, this);
};