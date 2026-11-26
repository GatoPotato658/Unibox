#pragma once
#include "../Entities/CBaseAnimating.h"


class CEconEntity : public CBaseAnimating
{
public:
	NETVAR(m_iItemDefinitionIndex, int, "CEconEntity", "m_iItemDefinitionIndex");
	NETVAR(m_iEntityQuality, int, "CEconEntity", "m_iEntityQuality");
	NETVAR(m_iAccountID, uint32_t, "CEconEntity", "m_iAccountID");

	OFFSET_SIG(m_bClientside, bool, CEconEntity_ClientsideOffset, int, 18);
	OFFSET_SIG(m_hAttachment, CHandle<CBaseAnimating>, CEconEntity_AttachmentOffset, int, 16);

	inline void* GetAttributeContainerItem()
	{
		static const int iHasAttributesOffset = S::CEconEntity_IHasAttributesOffset.ReadAt<int>(27);
		static const int iAttrContainerItemOffset = S::CAttributeContainer_ItemOffset.ReadAt<int>(25);
		auto pAttributeManager = U::Memory.CallVirtual<1, void*>(uintptr_t(this) + iHasAttributesOffset);
		return reinterpret_cast<void*>(uintptr_t(pAttributeManager) + iAttrContainerItemOffset);
	}

	VIRTUAL(UpdateAttachmentModels, void, 213, this);
};
