#include "../SDK/SDK.h"

#include "../Features/Visuals/CameraWindow/CameraWindow.h"


MAKE_HOOK(CClientShadowMgr_PreRender, S::CClientShadowMgr_PreRender(), void,
	void* rcx)
{
	DEBUG_RETURN(CClientShadowMgr_PreRender, rcx);

	if (F::CameraWindow.m_bDrawing)
		return;

	CALL_ORIGINAL(rcx);
}
