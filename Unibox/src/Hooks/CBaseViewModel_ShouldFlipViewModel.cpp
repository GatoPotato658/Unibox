#ifndef TEXTMODE
#include "../SDK/SDK.h"


MAKE_HOOK(CBaseViewModel_ShouldFlipViewModel, S::CBaseViewModel_ShouldFlipViewModel(), bool,
	void* rcx)
{
	DEBUG_RETURN(CBaseViewModel_ShouldFlipViewModel, rcx);

	return G::FlipViewmodels = CALL_ORIGINAL(rcx);
}
#endif
