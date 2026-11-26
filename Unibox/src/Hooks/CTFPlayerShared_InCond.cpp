#include "../SDK/SDK.h"


MAKE_HOOK(CTFPlayerShared_InCond, S::CTFPlayerShared_InCond(), bool,
	void* rcx, ETFCond nCond)
{
	DEBUG_RETURN(CTFPlayerShared_InCond, rcx, nCond);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwZoomPlayer = S::CTFPlayer_ShouldDraw_InCond_Call();
	const auto dwZoomWearable = S::CTFWearable_ShouldDraw_InCond_Call();
	const auto dwZoomHudScope = S::CHudScope_ShouldDraw_InCond_Call();
	const auto dwTaunt = S::CTFPlayer_CreateMove_InCondTaunt_Call();
	const auto dwKart1 = S::CTFPlayer_CreateMove_InCondKart_Call();
	const auto dwKart2 = S::CTFInput_ApplyMouse_InCond_Call();

	auto pLocal = H::Entities.GetLocal();
	auto pShared = pLocal ? pLocal->m_Shared() : nullptr;

	switch (nCond)
	{
	case TF_COND_ZOOMED:
		if (dwRetAddr == dwZoomPlayer || dwRetAddr == dwZoomWearable || dwRetAddr == dwZoomHudScope && Vars::Visuals::Removals::Scope.Value && !SDK::CleanScreenshot())
			return false;
		break;
	case TF_COND_DISGUISED:
		if (rcx == G::IgnoreDisguiseShared)
			return false;
		if (Vars::Visuals::Removals::Disguises.Value && pShared != rcx && !SDK::CleanScreenshot())
			return false;
		break;
	case TF_COND_TAUNTING:
		if (dwRetAddr == dwTaunt && Vars::Misc::Automation::TauntControl.Value)
			return false;
		if (Vars::Visuals::Removals::Taunts.Value && pShared != rcx && !SDK::CleanScreenshot())
			return false;
		break;
	case TF_COND_HALLOWEEN_KART:
		if ((dwRetAddr == dwKart1 || dwRetAddr == dwKart2) && Vars::Misc::Automation::KartControl.Value)
			return false;
		break;
	case TF_COND_FREEZE_INPUT:
		if (!CALL_ORIGINAL(rcx, TF_COND_HALLOWEEN_KART) || Vars::Misc::Automation::KartControl.Value)
			return false;
	}

	return CALL_ORIGINAL(rcx, nCond);
}