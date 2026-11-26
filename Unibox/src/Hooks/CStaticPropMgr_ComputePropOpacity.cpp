#include "../SDK/SDK.h"


MAKE_HOOK(CStaticPropMgr_ComputePropOpacity, S::CStaticPropMgr_ComputePropOpacity(), void,
	void* rcx, CStaticProp* pProp)
{
	DEBUG_RETURN(CStaticPropMgr_ComputePropOpacity, rcx, pProp);

#ifndef TEXTMODE
	if (Vars::Visuals::World::NoPropFade.Value && pProp && !SDK::CleanScreenshot())
	{
		pProp->m_Alpha = 255;
		return;
	}

	CALL_ORIGINAL(rcx, pProp);
#endif
}
