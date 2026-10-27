#include "../SDK/SDK.h"

MAKE_SIGNATURE(CRendering3dView_EnableWorldFog, "client.dll", "40 53 48 83 EC ? 48 8B 0D ? ? ? ? 48 89 74 24", 0x0);

MAKE_HOOK(CRendering3dView_EnableWorldFog, S::CRendering3dView_EnableWorldFog(), void)
{
	DEBUG_RETURN(CRendering3dView_EnableWorldFog);

#ifndef TEXTMODE
	if (!(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Fog) || SDK::CleanScreenshot())
		return CALL_ORIGINAL();

	CALL_ORIGINAL();
	if (auto pRenderContext = I::MaterialSystem->GetRenderContext())
	{
		if (Vars::Colors::FogModulation.Value.a)
		{
			pRenderContext->FogColor3ub(Vars::Colors::FogModulation.Value.r, Vars::Colors::FogModulation.Value.g, Vars::Colors::FogModulation.Value.b);

			float flRatio = 255.f / Vars::Colors::FogModulation.Value.a;
			float flStart, flEnd; pRenderContext->GetFogDistances(&flStart, &flEnd, nullptr);
			pRenderContext->FogStart(flStart * flRatio);
			pRenderContext->FogEnd(flEnd * flRatio);
		}
		else
			pRenderContext->FogMode(MATERIAL_FOG_NONE);
	}
#endif
}
