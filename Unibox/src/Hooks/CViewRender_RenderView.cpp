#include "../SDK/SDK.h"

#include "../Features/Visuals/CameraWindow/CameraWindow.h"

MAKE_HOOK(CViewRender_RenderView, U::Memory.GetVirtual(I::ViewRender, 6), void,
	void* rcx, const CViewSetup& view, ClearFlags_t nClearFlags, RenderViewInfo_t whatToDraw)
{
	DEBUG_RETURN(CViewRender_RenderView, rcx, view, nClearFlags, whatToDraw);

#ifndef TEXTMODE
	CALL_ORIGINAL(rcx, view, nClearFlags, whatToDraw);
	if (SDK::CleanScreenshot() || G::Unload)
		return;

	F::CameraWindow.RenderView(rcx, view);
#endif
}
