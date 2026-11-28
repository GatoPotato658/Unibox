#ifndef TEXTMODE
#include "../SDK/SDK.h"
#include "../Features/Visuals/UltraToaster/UltraToaster.h"


static bool s_bDrawingProps = false;

MAKE_HOOK(CStaticPropMgr_DrawStaticProps, S::CStaticPropMgr_DrawStaticProps(), void,
	void* rcx, IClientRenderable** pProps, int count, bool bShadowDepth, bool drawVCollideWireframe)
{
	DEBUG_RETURN(CStaticPropMgr_DrawStaticProps, rcx, pProps, count, bShadowDepth, drawVCollideWireframe);

	if (bShadowDepth && !drawVCollideWireframe && Vars::Visuals::UltraToaster::PropShadows.Value && UltraToaster::IsActive())
		return;

	s_bDrawingProps = true;
	CALL_ORIGINAL(rcx, pProps, count, bShadowDepth, drawVCollideWireframe);
	s_bDrawingProps = false;
}

MAKE_HOOK(CStudioRender_SetColorModulation, U::Memory.GetVirtual(I::StudioRender, 27), void,
	void* rcx, const float* pColor)
{
	DEBUG_RETURN(CStudioRender_SetColorModulation, rcx, pColor);

	if (!s_bDrawingProps || !(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Prop) || SDK::CleanScreenshot())
		return CALL_ORIGINAL(rcx, pColor);

	float flColor[3] = {
		Vars::Colors::PropModulation.Value.r / 255.f,
		Vars::Colors::PropModulation.Value.g / 255.f,
		Vars::Colors::PropModulation.Value.b / 255.f
	};
	CALL_ORIGINAL(rcx, flColor);
}

MAKE_HOOK(CStudioRender_SetAlphaModulation, U::Memory.GetVirtual(I::StudioRender, 28), void,
	void* rcx, float flAlpha)
{
	DEBUG_RETURN(CStudioRender_SetAlphaModulation, rcx, flAlpha);

	if (!s_bDrawingProps || !(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Prop) || SDK::CleanScreenshot())
		return CALL_ORIGINAL(rcx, flAlpha);

	CALL_ORIGINAL(rcx, Vars::Colors::PropModulation.Value.a / 255.f * flAlpha);
}
#endif
