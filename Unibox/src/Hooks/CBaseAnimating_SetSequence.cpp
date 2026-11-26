#include "../SDK/SDK.h"


MAKE_HOOK(CBaseAnimating_SetSequence, S::CBaseAnimating_SetSequence(), void,
	void* rcx, int nSequence)
{
	DEBUG_RETURN(CBaseAnimating_SetSequence, rcx, nSequence);

	auto pEntity = reinterpret_cast<CBaseAnimating*>(rcx);
	if (pEntity->m_nSequence() != nSequence && !pEntity->m_bSequenceLoops())
		pEntity->m_flCycle() = 0.f; // set on the server but not client

	CALL_ORIGINAL(rcx, nSequence);
}
