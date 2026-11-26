#include "../SDK/SDK.h"


MAKE_HOOK(DoEnginePostProcessing, S::DoEnginePostProcessing(), void,
	int x, int y, int w, int h, bool bFlashlightIsOn, bool bPostVGui)
{
	DEBUG_RETURN(DoEnginePostProcessing, x, y, w, h, bFlashlightIsOn, bPostVGui);

#ifndef TEXTMODE
	if (!Vars::Visuals::Removals::PostProcessing.Value || SDK::CleanScreenshot())
		CALL_ORIGINAL(x, y, w, h, bFlashlightIsOn, bPostVGui);
#endif
}
