#include "../SDK/SDK.h"

#include "../Features/Players/PlayerUtils.h"

MAKE_SIGNATURE(CAvatarImage_SetAvatarSteamID, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 66 C7 41", 0x0);
MAKE_SIGNATURE(CAvatarImagePanel_SetPlayer, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F9 33 F6 48 8B 89 ? ? ? ? 48 8B DA", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_UpdatePlayerAvatar, "client.dll", "4D 85 C0 0F 84 ? ? ? ? 53 41 54 41 57", 0x0);
MAKE_SIGNATURE(CTFMatchSummary_UpdatePlayerAvatar, "client.dll", "40 55 41 57 48 83 EC ? 48 8B E9 4D 8B F8", 0x0);
MAKE_SIGNATURE(CTFHudMannVsMachineScoreboard_UpdatePlayerAvatar, "client.dll", "4D 85 C0 0F 84 ? ? ? ? 55 41 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 4D 8B F8 48 8B E9 48 83 78 ? ? 0F 84 ? ? ? ? 48 83 78 ? ? 0F 84 ? ? ? ? 48 8B 0D ? ? ? ? 4C 8D 44 24 ? 48 8B 01 FF 50 ? 84 C0 0F 84 ? ? ? ? 83 7C 24 ? ? 0F 84 ? ? ? ? 48 89 9C 24 ? ? ? ? 48 89 B4 24 ? ? ? ? 48 89 BC 24 ? ? ? ? 4C 89 A4 24 ? ? ? ? 4C 89 B4 24 ? ? ? ? E8 ? ? ? ? 8B 4C 24 ? 48 8D B5 ? ? ? ? 0F B7 7E ? 41 BC ? ? ? ? 88 84 24 ? ? ? ? 8B 84 24 ? ? ? ? 25 ? ? ? ? 89 8C 24 ? ? ? ? 0D ? ? ? ? 89 84 24 ? ? ? ? 48 8B 9C 24 ? ? ? ? 48 89 5C 24 ? 66 41 3B FC 74 ? 66 66 0F 1F 84 00 ? ? ? ? 48 8B 56 ? 0F B7 C7 48 83 C2 ? 48 8D 0C 80 4C 8D 34 8D ? ? ? ? 49 03 D6 48 8D 4C 24 ? FF 16 84 C0 74 ? 66 41 3B FC 74 ? 48 8B 7E ? 41 0F B7 3C 3E EB ? 48 8B 4E ? 48 8D 54 24 ? 48 83 C1 ? 49 03 CE FF 16 84 C0 74 ? 66 41 3B FC 74 ? 48 8B 7E ? 42 0F B7 7C 37 ? 66 41 3B FC 75 ? 66 41 3B FC 75 ? B9 ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 E8 ? ? ? ? 48 8B F8 EB ? 33 FF 45 33 C0 48 8B D3 48 8B CF E8 ? ? ? ? BA ? ? ? ? 48 8B CF 44 8B C2 E8 ? ? ? ? C6 47", 0x0);
MAKE_SIGNATURE(CTFHudMatchStatus_UpdatePlayerAvatar, "client.dll", "4D 85 C0 0F 84 ? ? ? ? 55 41 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 4D 8B F8 48 8B E9 48 83 78 ? ? 0F 84 ? ? ? ? 48 83 78 ? ? 0F 84 ? ? ? ? 48 8B 0D ? ? ? ? 4C 8D 44 24 ? 48 8B 01 FF 50 ? 84 C0 0F 84 ? ? ? ? 83 7C 24 ? ? 0F 84 ? ? ? ? 48 89 9C 24 ? ? ? ? 48 89 B4 24 ? ? ? ? 48 89 BC 24 ? ? ? ? 4C 89 A4 24 ? ? ? ? 4C 89 B4 24 ? ? ? ? E8 ? ? ? ? 8B 4C 24 ? 48 8D B5 ? ? ? ? 0F B7 7E ? 41 BC ? ? ? ? 88 84 24 ? ? ? ? 8B 84 24 ? ? ? ? 25 ? ? ? ? 89 8C 24 ? ? ? ? 0D ? ? ? ? 89 84 24 ? ? ? ? 48 8B 9C 24 ? ? ? ? 48 89 5C 24 ? 66 41 3B FC 74 ? 66 66 0F 1F 84 00 ? ? ? ? 48 8B 56 ? 0F B7 C7 48 83 C2 ? 48 8D 0C 80 4C 8D 34 8D ? ? ? ? 49 03 D6 48 8D 4C 24 ? FF 16 84 C0 74 ? 66 41 3B FC 74 ? 48 8B 7E ? 41 0F B7 3C 3E EB ? 48 8B 4E ? 48 8D 54 24 ? 48 83 C1 ? 49 03 CE FF 16 84 C0 74 ? 66 41 3B FC 74 ? 48 8B 7E ? 42 0F B7 7C 37 ? 66 41 3B FC 75 ? 66 41 3B FC 75 ? B9 ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 E8 ? ? ? ? 48 8B F8 EB ? 33 FF 45 33 C0 48 8B D3 48 8B CF E8 ? ? ? ? BA ? ? ? ? 48 8B CF 44 8B C2 E8 ? ? ? ? 48 8B 8D", 0x0);
MAKE_SIGNATURE(SectionedListPanel_SetItemFgColor, "client.dll", "40 53 48 83 EC ? 48 8B D9 3B 91 ? ? ? ? 73 ? 3B 91 ? ? ? ? 7F ? 48 8B 89 ? ? ? ? 48 89 7C 24 ? 8B FA 48 03 FF 39 54 F9 ? 75 ? 39 54 F9 ? 75 ? 48 8B 0C F9 41 8B D0 48 8B 01 FF 90 ? ? ? ? 48 8B 83 ? ? ? ? B2 ? 48 8B 0C F8 48 8B 01 FF 90 ? ? ? ? 48 8B 83 ? ? ? ? 45 33 C0", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_UpdatePlayerList_SetItemFgColor_Call, "client.dll", "49 8B 04 24 8B D5 C7 44 24", 0x0);

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
