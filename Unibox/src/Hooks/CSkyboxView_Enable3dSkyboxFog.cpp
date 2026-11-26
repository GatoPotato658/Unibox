#include "../SDK/SDK.h"


MAKE_HOOK(CSkyboxView_Enable3dSkyboxFog, S::CSkyboxView_Enable3dSkyboxFog(), void,
	void* rcx)
{
	DEBUG_RETURN(CSkyboxView_Enable3dSkyboxFog, rcx);

#ifndef TEXTMODE
	if (!(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Fog) || SDK::CleanScreenshot())
		return CALL_ORIGINAL(rcx);

	CALL_ORIGINAL(rcx);
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
