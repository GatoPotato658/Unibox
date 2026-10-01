#include "../SDK/SDK.h"

#include "../Features/Visuals/Visuals.h"

MAKE_HOOK(CClientModeShared_ShouldDrawViewModel, U::Memory.GetVirtual(I::ClientModeShared, 24), bool,
	void* rcx)
{
	DEBUG_RETURN(CClientModeShared_ShouldDrawViewModel, rcx);

#ifdef TEXTMODE
	return false;
#else
	if (Vars::Visuals::UI::ZoomFieldOfView.Value && !SDK::CleanScreenshot())
	{
		auto pLocal = H::Entities.GetLocal();
		if (pLocal && pLocal->InCond(TF_COND_ZOOMED))
			return true;
	}

	return CALL_ORIGINAL(rcx);
#endif
}
