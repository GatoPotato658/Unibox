#ifndef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CTFPlayer_BRenderAsZombie, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 B9 ? ? ? ? E8 ? ? ? ? 84 C0", 0x0);
MAKE_SIGNATURE(CTFRagdoll_CreateTFRagdoll_BRenderAsZombie_Call, "client.dll", "84 C0 74 ? C6 87 ? ? ? ? ? 48 8B 06", 0x0);

MAKE_HOOK(CTFPlayer_BRenderAsZombie, S::CTFPlayer_BRenderAsZombie(), bool,
	void* rcx, bool bWeaponsCheck)
{
	DEBUG_RETURN(CTFPlayer_BRenderAsZombie, rcx, bWeaponsCheck);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFRagdoll_CreateTFRagdoll_BRenderAsZombie_Call();

	if (dwRetAddr == dwDesired && Vars::Visuals::Removals::Gibs.Value && !SDK::CleanScreenshot())
		return true;

	return CALL_ORIGINAL(rcx, bWeaponsCheck);
}
#endif