#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include <mutex>


static inline int ColorToInt(Color_t col)
{
	return col.r << 16 | col.g << 8 | col.b;
}

namespace
{
	std::recursive_mutex sAttribHookMutex;

	bool IsValidHookEntity(void* pEntity)
	{
		__try
		{
			if (!pEntity)
				return false;

			if (!I::ClientEntityList)
				return false;
			const auto& hEntity = reinterpret_cast<CBaseEntity*>(pEntity)->GetRefEHandle();
			return hEntity.IsValid()
				&& reinterpret_cast<void*>(I::ClientEntityList->GetClientEntityFromHandle(hEntity)) == pEntity;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

static int AttribHookIntOriginal(void* pOriginal, int value, const char* name, void* econent, void* buffer, bool isGlobalConstString)
{
	__try
	{
		return reinterpret_cast<int(__fastcall*)(int, const char*, void*, void*, bool)>(pOriginal)(value, name, econent, buffer, isGlobalConstString);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return value;
	}
}

MAKE_HOOK(CAttributeManager_AttribHookInt, S::CAttributeManager_AttribHookInt(), int,
	int value, const char* name, void* econent, void* buffer, bool isGlobalConstString)
{
	DEBUG_RETURN(CAttributeManager_AttribHookInt, value, name, econent, buffer, isGlobalConstString);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFPlayer_FireEvent_AttribHookValue_Call();

	if (dwRetAddr == dwDesired && name && FNV1A::Hash32(name) == FNV1A::Hash32Const("halloween_footstep_type"))
	{
		if (Vars::Visuals::Effects::SpellFootsteps.Value && !SDK::CleanScreenshot() && IsValidHookEntity(econent)
			&& reinterpret_cast<CBaseEntity*>(econent)->entindex() == I::EngineClient->GetLocalPlayer())
		{
			switch (Vars::Visuals::Effects::SpellFootsteps.Value)
			{
			case Vars::Visuals::Effects::SpellFootstepsEnum::Color: return ColorToInt(Vars::Colors::SpellFootstep.Value);
			case Vars::Visuals::Effects::SpellFootstepsEnum::Team: return 1;
			case Vars::Visuals::Effects::SpellFootstepsEnum::Halloween: return 2;
			}
		}

		return value;
	}

	if (!name || !IsValidHookEntity(econent))
		return value;

	std::lock_guard<std::recursive_mutex> lock(sAttribHookMutex);
	return AttribHookIntOriginal(Hook.As<void*>(), value, name, econent, buffer, isGlobalConstString);
}
#endif
