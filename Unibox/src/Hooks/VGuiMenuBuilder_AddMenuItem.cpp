#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Players/PlayerUtils.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/Output/Output.h"


static int s_iPlayerIndex;
static uint32_t s_uAccountID;
static const char* s_sPlayerName;

MAKE_HOOK(CPlayerResource_IsFakePlayer, S::CPlayerResource_IsFakePlayer(), bool,
	void* rcx, int index)
{
	DEBUG_RETURN(CPlayerResource_IsFakePlayer, rcx, index);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFClientScoreBoardDialog_OnScoreBoardMouseRightRelease_IsFakePlayer_Call();

	if (dwRetAddr == dwDesired && Vars::Visuals::UI::ScoreboardUtility.Value)
		s_iPlayerIndex = index;

	return CALL_ORIGINAL(rcx, index);
}

MAKE_HOOK(VGuiMenuBuilder_AddMenuItem, S::VGuiMenuBuilder_AddMenuItem(), void*,
	void* rcx, const char* pszButtonText, const char* pszCommand, const char* pszCategoryName)
{
	DEBUG_RETURN(VGuiMenuBuilder_AddMenuItem, rcx, pszButtonText, pszCommand, pszCategoryName);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired1 = S::CTFClientScoreBoardDialog_OnScoreBoardMouseRightRelease_AddMenuItem_CallProfile();
	const auto dwDesired2 = S::CTFClientScoreBoardDialog_OnScoreBoardMouseRightRelease_AddMenuItem_CallSpectate();

	if (dwRetAddr == dwDesired1 && Vars::Visuals::UI::ScoreboardUtility.Value && !SDK::AntiObs())
	{
		if (auto pResource = H::Entities.GetResource(); pResource && pResource->m_bValid(s_iPlayerIndex))
		{
			auto pReturn = CALL_ORIGINAL(rcx, pszButtonText, pszCommand, pszCategoryName);

			s_uAccountID = pResource->m_iAccountID(s_iPlayerIndex);
			s_sPlayerName = pResource->GetName(s_iPlayerIndex);

			CALL_ORIGINAL(rcx, "History", "history", "profile");
			CALL_ORIGINAL(rcx, I::EngineClient->GetPlayerForUserID(F::Spectate.GetTarget(true)) == s_iPlayerIndex ? "Unspectate" : "Spectate", "specplayer", "profile");

			CALL_ORIGINAL(rcx, std::format("Tags for {}", s_sPlayerName).c_str(), "listtags", "tags");
			for (auto it = F::PlayerUtils.m_vTags.begin(); it != F::PlayerUtils.m_vTags.end(); it++)
			{
				int iID = std::distance(F::PlayerUtils.m_vTags.begin(), it);
				auto& tTag = *it;
				if (!tTag.m_bAssignable)
					continue;

				bool bHasTag = F::PlayerUtils.HasTag(s_uAccountID, iID);
				CALL_ORIGINAL(rcx, std::format("{} {}", bHasTag ? "Remove" : "Add", tTag.m_sName).c_str(), std::format("modifytag{}", iID).c_str(), "tags");
			}

			return pReturn;
		}
	}

	if (dwRetAddr == dwDesired2 && Vars::Visuals::UI::ScoreboardUtility.Value && !SDK::AntiObs())
		return nullptr;

	return CALL_ORIGINAL(rcx, pszButtonText, pszCommand, pszCategoryName);
}

MAKE_HOOK(CTFClientScoreBoardDialog_OnCommand, S::CTFClientScoreBoardDialog_OnCommand(), void,
	void* rcx, const char* command)
{
	DEBUG_RETURN(CTFClientScoreBoardDialog_OnCommand, rcx, command);

	if (!Vars::Visuals::UI::ScoreboardUtility.Value || !command)
		return CALL_ORIGINAL(rcx, command);

	auto uHash = FNV1A::Hash32(command);
	switch (uHash)
	{
	case FNV1A::Hash32Const("history"):
		I::SteamFriends->ActivateGameOverlayToWebPage(std::format("https://steamhistory.net/id/{}", CSteamID(s_uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64()).c_str());
		break;
	case FNV1A::Hash32Const("listtags"):
		F::Output.TagsOnJoin(s_sPlayerName, s_uAccountID);
		break;
	case FNV1A::Hash32Const("specplayer"):
		if (auto pResource = H::Entities.GetResource(); pResource && Vars::Visuals::UI::ScoreboardUtility.Value)
		{
			F::Spectate.SetTarget(pResource->m_iUserID(s_iPlayerIndex));
			break;
		}
		[[fallthrough]];
	default:
		if (strstr(command, "modifytag"))
		{
			try
			{
				std::string sTag = command;
				sTag = sTag.replace(0, strlen("modifytag"), "");
				int iID = std::stoi(sTag);
				if (!F::PlayerUtils.HasTag(s_uAccountID, iID))
					F::PlayerUtils.AddTag(s_uAccountID, iID, true, s_sPlayerName);
				else
					F::PlayerUtils.RemoveTag(s_uAccountID, iID, true, s_sPlayerName);
			}
			catch (...) {}
		}
		CALL_ORIGINAL(rcx, command);
	}
}
#endif