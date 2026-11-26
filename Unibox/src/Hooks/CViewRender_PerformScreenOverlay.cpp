#include "../SDK/SDK.h"


MAKE_HOOK(CViewRender_PerformScreenOverlay, S::CViewRender_PerformScreenOverlay(), void,
	void* rcx, int x, int y, int w, int h)
{
	DEBUG_RETURN(CViewRender_PerformScreenOverlay, rcx, x, y, w, h);

#ifndef TEXTMODE
	if (!Vars::Visuals::Removals::ScreenOverlays.Value || SDK::CleanScreenshot())
		CALL_ORIGINAL(rcx, x, y, w, h);
#endif
}
