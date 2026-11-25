#include "../SDK/SDK.h"

MAKE_SIGNATURE(CClientState_GetClientInterpAmount, "engine.dll", "48 83 EC ? 48 8B 0D ? ? ? ? 48 85 C9 75", 0x0);

MAKE_HOOK(CClientState_GetClientInterpAmount, S::CClientState_GetClientInterpAmount(), float,
	CClientState* rcx)
{
	DEBUG_RETURN(CClientState_GetClientInterpAmount, rcx);

	const float flLerp = CALL_ORIGINAL(rcx);
	G::Lerp = flLerp;
	return flLerp;
}