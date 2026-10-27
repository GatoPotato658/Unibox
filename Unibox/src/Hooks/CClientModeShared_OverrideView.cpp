#include "../SDK/SDK.h"

#include "../Features/Visuals/Visuals.h"

MAKE_HOOK(CClientModeShared_OverrideView, U::Memory.GetVirtual(I::ClientModeShared, 16), void,
	void* rcx, CViewSetup* pView)
{
	DEBUG_RETURN(CClientModeShared_OverrideView, rcx, pView);

#ifndef TEXTMODE
	CALL_ORIGINAL(rcx, pView);

	if (auto pLocal = H::Entities.GetLocal(); pLocal && pView)
	{
		F::Visuals.FOV(pLocal, pView);
		F::Visuals.ThirdPerson(pLocal, pView);
	}
#endif
}
