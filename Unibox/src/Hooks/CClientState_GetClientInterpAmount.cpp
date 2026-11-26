#include "../SDK/SDK.h"


MAKE_HOOK(CClientState_GetClientInterpAmount, S::CClientState_GetClientInterpAmount(), float,
	CClientState* rcx)
{
	DEBUG_RETURN(CClientState_GetClientInterpAmount, rcx);

	const float flLerp = CALL_ORIGINAL(rcx);
	G::Lerp = flLerp;
	return flLerp;
}