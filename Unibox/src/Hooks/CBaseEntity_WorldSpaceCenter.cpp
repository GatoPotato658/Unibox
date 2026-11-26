#include "../SDK/SDK.h"


MAKE_HOOK(CBaseEntity_WorldSpaceCenter, S::CBaseEntity_WorldSpaceCenter(), const Vec3&,
	void* rcx)
{
	DEBUG_RETURN(CBaseEntity_WorldSpaceCenter, rcx);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CDamageAccountPanel_DisplayDamageFeedback_WorldSpaceCenter_Call();

	if (dwRetAddr == dwDesired && Vars::Visuals::Effects::DrawDamageNumbersThroughWalls.Value && !SDK::CleanScreenshot())
		return S::MainViewOrigin.Call<const Vec3&>();

	return CALL_ORIGINAL(rcx);
}