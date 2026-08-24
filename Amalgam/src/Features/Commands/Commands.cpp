#include "Commands.h"

#include "../../Core/Core.h"
#include "../ImGui/Menu/Menu.h"
#include "../NavBot/NavEngine/NavEngine.h"
#include "../Configs/Configs.h"
#include "../Players/PlayerUtils.h"
#include "../Misc/AutoItem/AutoItem.h"
#include "../Misc/Misc.h"
#include "../Visuals/Visuals.h"
#include <utility>
#include <boost/algorithm/string/replace.hpp>

#define AddCommand(sCommand, ...) \
{ \
	FNV1A::Hash32Const(sCommand), \
	[](const std::deque<const char*>& vArgs) \
		__VA_ARGS__ \
},

//struct ConVarValues_t
//{
//	int m_iValue;
//	float m_flValue;
//	const char* m_sValue;
//};
//static std::unordered_map<ConVar*, ConVarValues_t> s_mConVarValues = {};

static uint32_t s_uChatCommander = 0;

static std::unordered_map<uint32_t, CommandCallback> s_mCommands = {
	AddCommand("cat_kill",
	{
		I::EngineClient->ClientCmd_Unrestricted(SDK::RandomInt(0, 1) ? "kill" : "explode");
	})
	AddCommand("cat_party_givelead",
	{
		if (!I::TFPartyClient)
		{
			SDK::Output("cat_party_givelead", "TFPartyClient interface unavailable");
			return;
		}

		const uint32_t uSenderID = s_uChatCommander;
		s_uChatCommander = 0;
		if (!uSenderID)
		{
			SDK::Output("Usage:", "cat_party_givelead is a chat command, type it in party/in-game chat");
			return;
		}

		auto pParty = I::TFPartyClient->GetParty();
		if (!pParty || pParty->GetNumMembers() < 2)
		{
			SDK::Output("cat_party_givelead", "Local player is not in a party");
			return;
		}

		if (!I::TFPartyClient->BIsLocalPlayerLeader())
		{
			SDK::Output("cat_party_givelead", "Local player does not hold party leadership");
			return;
		}

		if (!H::Entities.InParty(uSenderID))
		{
			SDK::Output("cat_party_givelead", "Sender is not in the local player's party");
			return;
		}

		const uint64_t uSteamID64 = 0x0110000100000000ULL | uSenderID;
		if (!I::TFPartyClient->PromoteToLeader(uSteamID64))
		{
			SDK::Output("cat_party_givelead", "Failed to send promote request");
			return;
		}

		SDK::Output("cat_party_givelead", std::format("Gave party leadership to {}", uSteamID64).c_str());
	})
	AddCommand("cat_setcvar",
	{
		if (vArgs.size() < 2)
		{
			SDK::Output("Usage:\n\tcat_setcvar <cvar> <value>");
			return;
		}

		const char* sCVar = vArgs[0];
		auto pCVar = I::CVar->FindVar(sCVar);
		if (!pCVar)
		{
			SDK::Output(std::format("Could not find {}", sCVar).c_str());
			return;
		}

		std::string sValue = "";
		for (int i = 1; i < vArgs.size(); i++)
			sValue += std::format("{} ", vArgs[i]);
		sValue.pop_back();
		boost::replace_all(sValue, "\"", "");

		pCVar->SetValue(sValue.c_str());
		SDK::Output(std::format("Set {} to {}", sCVar, sValue).c_str());
	})
	AddCommand("cat_getcvar",
	{
		if (vArgs.size() != 1)
		{
			SDK::Output("Usage:\n\tcat_getcvar <cvar>");
			return;
		}

		const char* sCVar = vArgs[0];
		auto pCVar = I::CVar->FindVar(sCVar);
		if (!pCVar)
		{
			SDK::Output(std::format("Could not find {}", sCVar).c_str());
			return;
		}

		SDK::Output(std::format("Value of {} is {}", sCVar, pCVar->GetString()).c_str());
	})
	AddCommand("cat_queue",
	{
		if (!I::TFPartyClient)
			return;
		if (I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Casual_Default))
			return;
		if (I::EngineClient->IsDrawingLoadingImage())
			return;

		static bool bHasLoaded = false;
		if (!bHasLoaded)
		{
			I::TFPartyClient->LoadSavedCasualCriteria();
			bHasLoaded = true;
		}
		I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
	})
	AddCommand("cat_criteria",
	{
		if (!I::TFPartyClient)
		{
			SDK::Output("TFPartyClient interface unavailable");
			return;
		}

		I::TFPartyClient->LoadSavedCasualCriteria();
		SDK::Output("Loaded saved casual criteria.");
	})
	AddCommand("cat_abandon",
	{
		if (!I::TFGCClientSystem)
		{
			SDK::Output("TFGCClientSystem interface unavailable");
			return;
		}
		
		I::TFGCClientSystem->AbandonCurrentMatch();
		SDK::Output("Requested match abandon.");
	})
	AddCommand("cat_load", 
	{
		if (vArgs.size() != 1)
		{
			SDK::Output("Usage:\n\tcat_load <config_name>");
			return;
		}
		F::Configs.LoadConfig(vArgs[0], true);
	})
	AddCommand("cat_path_to", 
	{
		// Check if the user provided at least 3 args
		if (vArgs.size() < 3)
		{
			SDK::Output("Usage:\n\tcat_path_to <x> <y> <z>");
			return;
		}

		Vector vDest;
		try
		{	
			// Get the Vec3
			vDest = Vec3(atoi(vArgs[0]), atoi(vArgs[1]), atoi(vArgs[2]));
		}
		catch (...)
		{
			SDK::Output("Usage:\n\tcat_path_to <x> <y> <z>");
			return;
		}

		auto pLocal = H::Entities.GetLocal();
		if (!pLocal || !pLocal->IsAlive())
		{
			SDK::Output("cat_path_to", "Local player unavailable");
			return;
		}

		F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin());
		F::NavEngine.NavTo(vDest);
	})
	AddCommand("cat_cancel_path",
	{
		F::NavEngine.CancelPath();
	})
	AddCommand("cat_save_nav_mesh", 
	{
		if (auto pNavFile = F::NavEngine.GetNavFile())
			pNavFile->Write();
	})
	AddCommand("cat_refresh_triggers",
	{
		if (!SDK::RefreshTriggerStorage(true))
		{
			SDK::Output("TriggerStorage", "Refresh failed");
			return;
		}

		SDK::Output("TriggerStorage", std::format(
			"Refresh requested, triggers={} respawn_rooms={}",
			G::TriggerStorage.size(), F::NavEngine.GetRespawnRooms().size()).c_str());
	})
	AddCommand("cat_clearchat", 
	{
		I::ClientModeShared->m_pChatElement->SetText("");
	})
	AddCommand("cat_menu", 
	{
		I::MatSystemSurface->SetCursorAlwaysVisible(F::Menu.m_bIsOpen = !F::Menu.m_bIsOpen);
	})
	AddCommand("cat_unload", 
	{
		if (F::Menu.m_bIsOpen)
			I::MatSystemSurface->SetCursorAlwaysVisible(F::Menu.m_bIsOpen = false);
		U::Core.m_bUnload = G::Unload = true;
	})
	AddCommand("cat_ignore", 
	{
		if (vArgs.size() < 2)
		{
			SDK::Output("Usage:\n\tcat_ignore <id32> <tag|rage>");
			return;
		}
		
		uint32_t uFriendsID = 0;
		try
		{
			uFriendsID = std::stoul(vArgs[0]);
		}
		catch (...)
		{
			SDK::Output("Invalid ID32 format");
			return;
		}

		if (!uFriendsID)
		{
			SDK::Output("Invalid ID32");
			return;
		}

		std::string sTag = vArgs[1];
		std::string sTagLower = sTag;
		std::transform(sTagLower.begin(), sTagLower.end(), sTagLower.begin(), ::tolower);

		int iTagID = FNV1A::Hash32(sTagLower.c_str()) == FNV1A::Hash32Const("rage")
			? F::PlayerUtils.TagToIndex(CHEATER_TAG)
			: F::PlayerUtils.GetTag(sTag);
		if (iTagID == -1)
		{
			SDK::Output(std::format("Invalid tag: {}", sTag).c_str());
			return;
		}

		auto pTag = F::PlayerUtils.GetTag(iTagID);
		if (!pTag || !pTag->m_bAssignable)
		{
			SDK::Output(std::format("Tag {} is not assignable", sTag).c_str());
			return;
		}
		const char* sTagName = pTag->m_sName.c_str();

		if (F::PlayerUtils.HasTag(uFriendsID, iTagID))
		{
			SDK::Output(std::format("ID32 {} already has tag {}", uFriendsID, sTagName).c_str());
			return;
		}

		F::PlayerUtils.AddTag(uFriendsID, iTagID, true);
		SDK::Output(std::format("Added tag {} to ID32 {}", sTagName, uFriendsID).c_str());
	})
	AddCommand("cat_dump",
	{
		F::Misc.DumpProfiles();
	})
	AddCommand("cat_crash", 
	{	// if you want to time out of a server and rejoin
		switch (vArgs.empty() ? 0 : FNV1A::Hash32(vArgs.front()))
		{
		case FNV1A::Hash32Const("true"):
		case FNV1A::Hash32Const("t"):
		case FNV1A::Hash32Const("1"):
			break;
		default:
			Vars::Debug::CrashLogging.Value = false; // we are voluntarily crashing, don't give out log if we don't want one
		}
		reinterpret_cast<void(*)()>(0)();
	})
	AddCommand("cat_rent_item", 
	{	
		if (vArgs.size() != 1)
		{
			SDK::Output("Usage:\n\tcat_rent_item <item_def_index>");
			return;
		}

		item_definition_index_t iDefIdx;
		try
		{
			iDefIdx = atoi(vArgs[0]);
		}
		catch (const std::invalid_argument&)
		{
			SDK::Output("Invalid item_def_index");
			return;
		}

		F::AutoItem.Rent(iDefIdx);
	})
	AddCommand("cat_achievement_unlock", 
	{
		if (vArgs.size() > 1)
		{
			SDK::Output("Usage:\n\tcat_achievement_unlock [all|item|weapon]");
			return;
		}

		const uint32_t uMode = vArgs.empty() ? FNV1A::Hash32Const("all") : FNV1A::Hash32(vArgs[0]);
		switch (uMode)
		{
		case FNV1A::Hash32Const("all"):
		case FNV1A::Hash32Const("normal"):
			F::Misc.UnlockAchievements();
			break;
		case FNV1A::Hash32Const("item"):
		case FNV1A::Hash32Const("items"):
		case FNV1A::Hash32Const("weapon"):
		case FNV1A::Hash32Const("weapons"):
			F::Misc.UnlockItemAchievements();
			break;
		default:
			SDK::Output("Usage:\n\tcat_achievement_unlock [all|item|weapon]");
			break;
		}
	})
	AddCommand("cat_achievement_unlock_item",
	{
		F::Misc.UnlockItemAchievements();
	})
	AddCommand("cat_achievement_unlock_weapon",
	{
		F::Misc.UnlockItemAchievements();
	})
	AddCommand("cat_achievement_lock",
	{
		if (vArgs.size() > 1)
		{
			SDK::Output("Usage:\n\tcat_achievement_lock [all|item|weapon]");
			return;
		}

		const uint32_t uMode = vArgs.empty() ? FNV1A::Hash32Const("all") : FNV1A::Hash32(vArgs[0]);
		switch (uMode)
		{
		case FNV1A::Hash32Const("all"):
		case FNV1A::Hash32Const("normal"):
			F::Misc.LockAchievements();
			break;
		case FNV1A::Hash32Const("item"):
		case FNV1A::Hash32Const("items"):
		case FNV1A::Hash32Const("weapon"):
		case FNV1A::Hash32Const("weapons"):
			F::Misc.LockItemAchievements();
			break;
		default:
			SDK::Output("Usage:\n\tcat_achievement_lock [all|item|weapon]");
			break;
		}
	})
	AddCommand("cat_achievement_lock_item",
	{
		F::Misc.LockItemAchievements();
	})
	AddCommand("cat_achievement_lock_weapon",
	{
		F::Misc.LockItemAchievements();
	})
	AddCommand("cat_mvm_fix",
	{
		F::Misc.MvMFix();
	})
	AddCommand("cat_mvm_quit",
	{
		if (I::TFGCClientSystem)
			I::TFGCClientSystem->AbandonCurrentMatch();

		I::EngineClient->ClientCmd_Unrestricted("disconnect");
		SDK::Output("cat_mvm_quit", "Abandoned match and disconnected.");
	})
	AddCommand("cat_mvm_tele",
	{
		auto pLocal = H::Entities.GetLocal();
		if (!pLocal || !pLocal->IsAlive())
		{
			SDK::Output("cat_mvm_tele", "Local player unavailable");
			return;
		}

		CBaseObject* pBest = nullptr;
		float flBestDist = FLT_MAX;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
		{
			if (!pEntity || pEntity->IsDormant() || pEntity->GetClassID() != ETFClassID::CObjectTeleporter)
				continue;

			auto pTeleporter = pEntity->As<CObjectTeleporter>();
			if (pTeleporter->m_iObjectMode() != 0 || pTeleporter->m_bPlacing() || pTeleporter->m_bCarried() || pTeleporter->m_bDisabled())
				continue;

			const float flDist = pLocal->GetAbsOrigin().DistToSqr(pTeleporter->GetAbsOrigin());
			if (flDist >= flBestDist)
				continue;

			flBestDist = flDist;
			pBest = pTeleporter;
		}

		if (!pBest || !F::NavEngine.NavTo(pBest->GetAbsOrigin()))
		{
			SDK::Output("cat_mvm_tele", "No reachable teleporter entrance found");
			return;
		}

		SDK::Output("cat_mvm_tele", "Pathing to closest teleporter entrance");
	})
	AddCommand("cat_mvm_rent",
	{
		F::AutoItem.MvmRent();
	})
#ifdef DEBUG_UNI
	AddCommand("cat_uni",
	{
		F::Visuals.m_bUniDraw = true;
	})
#endif
	//AddCommand("recordcvars",
	//{
	//	for (ConCommandBase* pBase = I::CVar->GetCommands(); pBase; pBase = pBase->m_pNext)
	//	{
	//		if (pBase->IsCommand())
	//			continue;

	//		auto pCVar = reinterpret_cast<ConVar*>(pBase);
	//		s_mConVarValues[pCVar] = { pCVar->GetInt(), pCVar->GetFloat(), pCVar->GetString() };
	//	}
	//})
	//AddCommand("diffcvars",
	//{
	//	for (auto& [pCVar, tValues] : s_mConVarValues)
	//	{
	//		if (auto iOld = tValues.m_iValue, iNew = pCVar->GetInt(); iOld != iNew)
	//			SDK::Output(pCVar->GetName(), std::format("int: {} -> {}", iOld, iNew).c_str());
	//		else if (auto flOld = tValues.m_flValue, flNew = pCVar->GetFloat(); flOld != flNew)
	//			SDK::Output(pCVar->GetName(), std::format("float: {} -> {}", flOld, flNew).c_str());
	//		else if (auto sOld = tValues.m_sValue, sNew = pCVar->GetString(); sOld != sNew || FNV1A::Hash32(sOld) != FNV1A::Hash32(sNew))
	//			SDK::Output(pCVar->GetName(), std::format("string: {} -> {}", sOld, sNew).c_str());
	//	}
	//})
};

bool CCommands::Run(const char* sCmd, std::deque<const char*>& vArgs)
{
	std::string sLower = sCmd;
	std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);

	auto uHash = FNV1A::Hash32(sLower.c_str());
	if (!s_mCommands.contains(uHash))
		return false;

	s_mCommands[uHash](vArgs);
	return true;
}

static bool IsChatCommandAllowed(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("cat_mvm_fix"):
	case FNV1A::Hash32Const("cat_mvm_quit"):
	case FNV1A::Hash32Const("cat_mvm_tele"):
	case FNV1A::Hash32Const("cat_mvm_rent"):
	case FNV1A::Hash32Const("cat_party_givelead"):
		return true;
	default:
		return false;
	}
}

void CCommands::RunChat(const std::string& sMsg, uint32_t uAccountID, bool bPartyChat)
{
	if (!Vars::Misc::MannVsMachine::ChatCommands::Mode.Value || sMsg.empty())
		return;

	std::string sClean;
	for (char c : sMsg)
	{
		if (static_cast<unsigned char>(c) >= ' ') // strip color codes
			sClean += c;
	}

	const size_t uStart = sClean.find_first_not_of(' ');
	if (uStart == std::string::npos)
		return;
	sClean.erase(0, uStart);

	if (!sClean.empty() && (sClean.front() == '!' || sClean.front() == '/'))
		sClean.erase(0, 1);

	std::vector<std::string> vTokens;
	for (size_t uPos = 0; uPos < sClean.size();)
	{
		size_t uEnd = sClean.find(' ', uPos);
		if (uEnd == std::string::npos)
			uEnd = sClean.size();
		if (uEnd > uPos)
			vTokens.push_back(sClean.substr(uPos, uEnd - uPos));
		uPos = uEnd + 1;
	}

	if (vTokens.empty())
		return;

	std::string& sCmd = vTokens[0];
	std::transform(sCmd.begin(), sCmd.end(), sCmd.begin(), ::tolower);

	const uint32_t uHash = FNV1A::Hash32(sCmd.c_str());
	if (!IsChatCommandAllowed(uHash))
		return;

	const uint32_t uLocalID = H::Entities.GetLocalAccountID();
	bool bAllowed = uLocalID && uAccountID == uLocalID;
	if (!bAllowed)
	{
		switch (Vars::Misc::MannVsMachine::ChatCommands::Mode.Value)
		{
		case Vars::Misc::MannVsMachine::ChatCommands::ModeEnum::Party:
			bAllowed = bPartyChat || H::Entities.InParty(uAccountID);
			break;
		case Vars::Misc::MannVsMachine::ChatCommands::ModeEnum::Friends:
			bAllowed = H::Entities.IsFriend(uAccountID);
			break;
		case Vars::Misc::MannVsMachine::ChatCommands::ModeEnum::CustomTag:
		{
			const int iTag = Vars::Misc::MannVsMachine::ChatCommands::Tag.Value;
			bAllowed = uAccountID && F::PlayerUtils.HasTag(uAccountID, iTag);
			break;
		}
		}
	}

	if (!bAllowed)
		return;

	std::deque<const char*> vArgs;
	for (size_t i = 1; i < vTokens.size(); i++)
		vArgs.push_back(vTokens[i].c_str());

	s_uChatCommander = uAccountID;
	Run(sCmd.c_str(), vArgs);
}
