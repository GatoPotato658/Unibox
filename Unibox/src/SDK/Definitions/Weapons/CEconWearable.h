#pragma once
#include "CEconEntity.h"

class CEconWearable : public CEconEntity
{
public:
	VIRTUAL(Spawn, void, 29, this);
	VIRTUAL(ValidateModelIndex, void, 45, this);
	VIRTUAL_ARGS(InitializeAsClientEntity, bool, 82, (const char* pszModelName, int iRenderGroup), this, pszModelName, iRenderGroup);
	VIRTUAL_ARGS(Equip, void, 230, (CBasePlayer* pOwner), this, pOwner);
	VIRTUAL_ARGS(UnEquip, void, 231, (CBasePlayer* pOwner), this, pOwner);

	inline void ReleaseClientEntity()
	{
		U::Memory.CallVirtual<1, void>(uintptr_t(this) + 0x10);
	}

	static inline size_t GetClassSize()
	{
		static const size_t uSize = static_cast<size_t>(S::CTFWearable_LastFieldOffset.ReadAt<int>(18) + 4);
		return uSize;
	}
};
