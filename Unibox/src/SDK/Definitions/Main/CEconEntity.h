#pragma once
#include "CBaseAnimating.h"

class CEconEntity : public CBaseAnimating
{
public:
	NETVAR(m_iItemDefinitionIndex, int, "CEconEntity", "m_iItemDefinitionIndex");
	NETVAR(m_iEntityQuality, int, "CEconEntity", "m_iEntityQuality");
	NETVAR(m_iAccountID, uint32_t, "CEconEntity", "m_iAccountID");

	VIRTUAL(UpdateAttachmentModels, void, 213, this);
};