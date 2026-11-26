#include "../SDK/SDK.h"
#ifndef TEXTMODE
#include "../Features/Players/PlayerUtils.h"

#endif
#ifndef TEXTMODE

static int s_iPlayerIndex;
static void* s_pTeamStatus;

static inline void SetScoreboardColor(int iIndex, Color_t& tColor)
{
	if (iIndex <= 0 || iIndex > MAX_PLAYERS || iIndex > I::EngineClient->GetMaxClients())
		return;

	if (iIndex == I::EngineClient->GetLocalPlayer())
		tColor = Vars::Colors::Local.Value;
	else if (H::Entities.IsFriend(iIndex))
		tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)].m_tColor;
	else if (H::Entities.InParty(iIndex))
		tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)].m_tColor;
	else if (auto pTag = F::PlayerUtils.GetSignificantTag(iIndex))
		tColor = pTag->m_tColor;
	else
		return;

	auto pResource = H::Entities.GetResource();
	if (pResource && !pResource->m_bAlive(iIndex))
		tColor = tColor.Lerp({ 127, 127, 127, tColor.a }, 0.5f);
}

MAKE_HOOK(CTFPlayerPanel_GetTeam, S::CTFPlayerPanel_GetTeam(), int,
	void* rcx)
{
	DEBUG_RETURN(CTFPlayerPanel_GetTeam, rcx);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFTeamStatusPlayerPanel_Update_GetTeam_Call();

	int iReturn = CALL_ORIGINAL(rcx);

	if (auto pResource = H::Entities.GetResource(); dwRetAddr == dwDesired && pResource)
	{
		static const int iPlayerIndexOffset = S::CTFTeamStatusPlayerPanel_Update_GetTeam_Call.ReadAt<int>(2);
		static const int iHealthBarOffset = S::CTFTeamStatusPlayerPanel_Update_Offsets.ReadAt<int>(3);
		static const int iLastHealthOffset = S::CTFTeamStatusPlayerPanel_Update_Offsets.ReadAt<int>(36);

		s_iPlayerIndex = *reinterpret_cast<int*>(uintptr_t(rcx) + iPlayerIndexOffset);

		int iLocalTeam = pResource->m_iTeam(I::EngineClient->GetLocalPlayer());

		if (Vars::Visuals::UI::RevealScoreboard.Value && !SDK::CleanScreenshot())
			iReturn = iLocalTeam;

		if (auto pHealthBar = *reinterpret_cast<void**>(uintptr_t(rcx) + iHealthBarOffset);
			pHealthBar && U::Memory.CallVirtual<34, bool>(pHealthBar) != (iReturn == iLocalTeam))
			*reinterpret_cast<int*>(uintptr_t(rcx) + iLastHealthOffset) = -1;
	}

	return iReturn;
}
#endif
MAKE_HOOK(vgui_Panel_SetBgColor, S::vgui_Panel_SetBgColor(), void,
	void* rcx, Color_t color)
{
	DEBUG_RETURN(CTFPlayerPanel_GetTeam, rcx, color);

#ifndef TEXTMODE
	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFTeamStatusPlayerPanel_Update_SetBgColor_Call();

	if (dwRetAddr == dwDesired && Vars::Visuals::UI::ScoreboardColors.Value && !SDK::CleanScreenshot())
		SetScoreboardColor(s_iPlayerIndex, color);

	CALL_ORIGINAL(rcx, color);
#endif
}
#ifndef TEXTMODE
MAKE_HOOK(CTFTeamStatus_OnTick, S::CTFTeamStatus_OnTick(), void,
	void* rcx)
{
	DEBUG_RETURN(CTFTeamStatus_OnTick, rcx);

	s_pTeamStatus = rcx;

	CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CVGui_RunFrame, S::CVGui_RunFrame(), void,
	void* rcx)
{
	DEBUG_RETURN(CVGui_RunFrame, rcx);

	if (!s_pTeamStatus)
		return CALL_ORIGINAL(rcx);

	static bool bStaticMod = false;
	const bool bLastMod = bStaticMod;
	const bool bCurrMod = bStaticMod = (Vars::Visuals::UI::RevealScoreboard.Value || Vars::Visuals::UI::ScoreboardColors.Value) && !SDK::CleanScreenshot();

	if (bCurrMod != bLastMod)
		S::CTFTeamStatus_OnTick.Call<void>(s_pTeamStatus);

	CALL_ORIGINAL(rcx);
}
#endif