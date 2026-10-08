#include "../SDK/SDK.h"

#include "../Features/Visuals/CameraWindow/CameraWindow.h"

MAKE_SIGNATURE(CClientShadowMgr_PreRender, "client.dll", "4C 8B DC 49 89 5B ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC ? 48 8B 05 ? ? ? ? 48 8D 35", 0x0);

MAKE_HOOK(CClientShadowMgr_PreRender, S::CClientShadowMgr_PreRender(), void,
	void* rcx)
{
	DEBUG_RETURN(CClientShadowMgr_PreRender, rcx);

	if (F::CameraWindow.m_bDrawing)
		return;

	CALL_ORIGINAL(rcx);
}
