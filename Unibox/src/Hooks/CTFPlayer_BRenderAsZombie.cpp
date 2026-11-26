#ifndef TEXTMODE
#include "../SDK/SDK.h"


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