#include "../SDK/SDK.h"

#include "../Features/Players/PlayerUtils.h"


#ifndef TEXTMODE
enum EAvatarSize
{
	k_EAvatarSize32x32 = 0,
	k_EAvatarSize64x64 = 1,
	k_EAvatarSize184x184 = 2,
};

static int s_iPlayerIndex;

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

MAKE_HOOK(CAvatarImage_SetAvatarSteamID, S::CAvatarImage_SetAvatarSteamID(), bool,
	void* rcx, CSteamID steamIDUser, EAvatarSize avatarSize)
{
	DEBUG_RETURN(CAvatarImage_SetAvatarSteamID, rcx, steamIDUser, avatarSize);

	if (!(F::PlayerUtils.GetNameType(steamIDUser.GetAccountID()) & NameTypeEnum::Privacy))
		return CALL_ORIGINAL(rcx, steamIDUser, avatarSize);

	return CALL_ORIGINAL(rcx, CSteamID(), avatarSize);
}

MAKE_HOOK(CAvatarImagePanel_SetPlayer, S::CAvatarImagePanel_SetPlayer(), void,
	void* rcx, CSteamID steamIDForPlayer, EAvatarSize avatarSize)
{
	DEBUG_RETURN(CAvatarImagePanel_SetPlayer, rcx, steamIDForPlayer, avatarSize);

	if (!(F::PlayerUtils.GetNameType(steamIDForPlayer.GetAccountID()) & NameTypeEnum::Privacy))
		return CALL_ORIGINAL(rcx, steamIDForPlayer, avatarSize);

	CALL_ORIGINAL(rcx, CSteamID(), avatarSize);
}
#endif

MAKE_HOOK(CTFClientScoreBoardDialog_UpdatePlayerAvatar, S::CTFClientScoreBoardDialog_UpdatePlayerAvatar(), void,
	void* rcx, int playerIndex, KeyValues* kv)
{
	DEBUG_RETURN(CTFClientScoreBoardDialog_UpdatePlayerAvatar, rcx, playerIndex, kv);

#ifndef TEXTMODE
	s_iPlayerIndex = playerIndex;

	if (!(F::PlayerUtils.GetNameType(playerIndex) & NameTypeEnum::Privacy))
		CALL_ORIGINAL(rcx, playerIndex, kv);
#endif
}

MAKE_HOOK(CTFMatchSummary_UpdatePlayerAvatar, S::CTFMatchSummary_UpdatePlayerAvatar(), void,
	void* rcx, int playerIndex, KeyValues* kv)
{
	DEBUG_RETURN(CTFMatchSummary_UpdatePlayerAvatar, rcx, playerIndex, kv);

#ifndef TEXTMODE
	if (!(F::PlayerUtils.GetNameType(playerIndex) & NameTypeEnum::Privacy))
		CALL_ORIGINAL(rcx, playerIndex, kv);
#endif
}

MAKE_HOOK(CTFHudMannVsMachineScoreboard_UpdatePlayerAvatar, S::CTFHudMannVsMachineScoreboard_UpdatePlayerAvatar(), void,
	void* rcx, int playerIndex, KeyValues* kv)
{
	DEBUG_RETURN(CTFHudMannVsMachineScoreboard_UpdatePlayerAvatar, rcx, playerIndex, kv);

#ifndef TEXTMODE
	if (!(F::PlayerUtils.GetNameType(playerIndex) & NameTypeEnum::Privacy))
		CALL_ORIGINAL(rcx, playerIndex, kv);
#endif
}

MAKE_HOOK(CTFHudMatchStatus_UpdatePlayerAvatar, S::CTFHudMatchStatus_UpdatePlayerAvatar(), void,
	void* rcx, int playerIndex, KeyValues* kv)
{
	DEBUG_RETURN(CTFHudMatchStatus_UpdatePlayerAvatar, rcx, playerIndex, kv);

#ifndef TEXTMODE
	if (!(F::PlayerUtils.GetNameType(playerIndex) & NameTypeEnum::Privacy))
		CALL_ORIGINAL(rcx, playerIndex, kv);
#endif
}

MAKE_HOOK(SectionedListPanel_SetItemFgColor, S::SectionedListPanel_SetItemFgColor(), void,
	void* rcx, int itemID, Color_t color)
{
	DEBUG_RETURN(SectionedListPanel_SetItemFgColor, rcx, itemID, color);

#ifndef TEXTMODE
	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFClientScoreBoardDialog_UpdatePlayerList_SetItemFgColor_Call();

	if (dwDesired == dwRetAddr && Vars::Visuals::UI::ScoreboardColors.Value && !SDK::CleanScreenshot())
		SetScoreboardColor(s_iPlayerIndex, color);

	CALL_ORIGINAL(rcx, itemID, color);
#endif
}
