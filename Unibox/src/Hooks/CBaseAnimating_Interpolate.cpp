#include "../SDK/SDK.h"


MAKE_HOOK(CBaseAnimating_Interpolate, S::CBaseAnimating_Interpolate(), bool,
	void* rcx, float currentTime)
{
	DEBUG_RETURN(CBaseAnimating_Interpolate, rcx, currentTime);

	if (!G::Unload && Vars::Visuals::Removals::Interpolation.Value && rcx != H::Entities.GetLocal())
		return true;

	return CALL_ORIGINAL(rcx, currentTime);
}