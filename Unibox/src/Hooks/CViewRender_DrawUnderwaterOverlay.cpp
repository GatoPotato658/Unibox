#include "../SDK/SDK.h"

MAKE_SIGNATURE(CViewRender_DrawUnderwaterOverlay, "client.dll", "4C 8B DC 41 56 48 81 EC ? ? ? ? 4C 8B B1", 0x0);

MAKE_HOOK(CViewRender_DrawUnderwaterOverlay, S::CViewRender_DrawUnderwaterOverlay(), void,
	void* rcx)
{
	DEBUG_RETURN(CViewRender_DrawUnderwaterOverlay, rcx);

#ifndef TEXTMODE
	if (!Vars::Visuals::Removals::ScreenOverlays.Value || SDK::CleanScreenshot())
		CALL_ORIGINAL(rcx);
#endif
}
