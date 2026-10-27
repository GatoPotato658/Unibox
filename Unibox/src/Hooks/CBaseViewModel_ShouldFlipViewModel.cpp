#ifndef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CBaseViewModel_ShouldFlipViewModel, "client.dll", "40 57 48 83 EC ? 8B 91 ? ? ? ? 85 D2", 0x0);

MAKE_HOOK(CBaseViewModel_ShouldFlipViewModel, S::CBaseViewModel_ShouldFlipViewModel(), bool,
	void* rcx)
{
	DEBUG_RETURN(CBaseViewModel_ShouldFlipViewModel, rcx);

	return G::FlipViewmodels = CALL_ORIGINAL(rcx);
}
#endif
