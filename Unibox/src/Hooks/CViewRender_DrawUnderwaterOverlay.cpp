#include "../SDK/SDK.h"


MAKE_HOOK(CViewRender_DrawUnderwaterOverlay, S::CViewRender_DrawUnderwaterOverlay(), void,
	void* rcx)
{
	DEBUG_RETURN(CViewRender_DrawUnderwaterOverlay, rcx);

#ifndef TEXTMODE
	if (!Vars::Visuals::Removals::ScreenOverlays.Value || SDK::CleanScreenshot())
		CALL_ORIGINAL(rcx);
#endif
}
