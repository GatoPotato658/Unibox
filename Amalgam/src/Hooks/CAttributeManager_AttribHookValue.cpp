#ifndef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CAttributeManager_AttribHookInt, "client.dll", "4C 8B DC 49 89 5B ? 49 89 6B ? 49 89 73 ? 57 41 54 41 55 41 56 41 57 48 83 EC ? 48 8B 3D ? ? ? ? 4C 8D 35", 0x0);
MAKE_SIGNATURE(CTFPlayer_FireEvent_AttribHookValue_Call, "client.dll", "8B F8 83 BE", 0x0);

static inline int ColorToInt(Color_t col)
{
    return col.r << 16 | col.g << 8 | col.b;
}

MAKE_HOOK(CAttributeManager_AttribHookInt, S::CAttributeManager_AttribHookInt(), int,
	int value, const char* name, void* econent, void* buffer, bool isGlobalConstString)
{
	DEBUG_RETURN(CAttributeManager_AttribHookInt, value, name, econent, buffer, isGlobalConstString);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFPlayer_FireEvent_AttribHookValue_Call();

	CTFPlayer* pLocal = H::Entities.GetLocal();
	const bool bLocalPlayer = pLocal && econent == pLocal && pLocal->IsAlive() && !pLocal->IsDormant()
		&& I::EngineClient->IsConnected() && I::EngineClient->IsInGame();

	if (dwRetAddr == dwDesired && Vars::Visuals::Effects::SpellFootsteps.Value && bLocalPlayer
		&& name && FNV1A::Hash32(name) == FNV1A::Hash32Const("halloween_footstep_type"))
	{
		switch (Vars::Visuals::Effects::SpellFootsteps.Value)
		{
		case Vars::Visuals::Effects::SpellFootstepsEnum::Color: return ColorToInt(Vars::Colors::SpellFootstep.Value);
		case Vars::Visuals::Effects::SpellFootstepsEnum::Team: return 1;
		case Vars::Visuals::Effects::SpellFootstepsEnum::Halloween: return 2;
		}
	}

	return CALL_ORIGINAL(value, name, econent, buffer, isGlobalConstString);
}
#endif
