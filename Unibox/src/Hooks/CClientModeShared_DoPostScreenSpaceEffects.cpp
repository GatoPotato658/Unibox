#include "../SDK/SDK.h"

#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/Visuals/Glow/Glow.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/NavBot/NavRender.h"
#include "../Features/Spectate/Spectate.h"


MAKE_HOOK(CClientModeShared_DoPostScreenSpaceEffects, U::Memory.GetVirtual(I::ClientModeShared, 39), bool,
	void* rcx, const CViewSetup* pSetup)
{
	DEBUG_RETURN(CClientModeShared_DoPostScreenSpaceEffects, rcx, pSetup);

#ifdef TEXTMODE
	return false;
#else
	if (SDK::CleanScreenshot() || G::Unload || F::CameraWindow.m_bDrawing)
		return CALL_ORIGINAL(rcx, pSetup);

	auto pLocal = H::Entities.GetLocal();
	F::Visuals.ProjectileTrace(pLocal, H::Entities.GetWeapon());
	F::Visuals.SpyCamera(pLocal);

	F::NavRender.RenderWorld();
	F::Visuals.Triggers(pLocal);
	F::Visuals.DrawEffects();
	if (I::EngineVGui->IsGameUIVisible() || !F::Materials.m_bLoaded)
		return CALL_ORIGINAL(rcx, pSetup);

	F::Chams.RenderMain();
	F::Glow.RenderFirst();
	return CALL_ORIGINAL(rcx, pSetup);
#endif
}

MAKE_HOOK(CViewRender_DrawViewModels, S::CViewRender_DrawViewModels(), void,
	void* rcx, const CViewSetup& viewRender, bool drawViewmodel)
{
	DEBUG_RETURN(CViewRender_DrawViewModels, rcx, viewRender, drawViewmodel);

#ifndef TEXTMODE
	CALL_ORIGINAL(rcx, viewRender, F::Spectate.HasTarget() && !I::EngineClient->IsHLTV() ? false : drawViewmodel);
	if (SDK::CleanScreenshot() || F::CameraWindow.m_bDrawing || I::EngineVGui->IsGameUIVisible() || !F::Materials.m_bLoaded)
		return;

	F::Glow.RenderSecond();
#endif
}