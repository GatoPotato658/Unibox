#include "Events.h"

#include "../../Core/Core.h"
#include "../../Features/Aimbot/AutoHeal/AutoHeal.h"
#include "../../Features/Backtrack/Backtrack.h"
#include "../../Features/CheatDetection/CheatDetection.h"
#include "../../Features/CritHack/CritHack.h"
#include "../../Features/Misc/Misc.h"
#include "../../Features/PacketManip/AntiAim/AntiAim.h"
#include "../../Features/Output/Output.h"
#include "../../Features/Resolver/Resolver.h"
#include "../../Features/Visuals/Visuals.h"
#include "../../Features/Killstreak/Killstreak.h"
#include "../../Features/NavBot/NavEngine/NavEngine.h"
#include "../../Features/NavBot/NavBotJobs/NavBotJobs.h"
#ifdef TEXTMODE
#include "../../Features/Misc/NamedPipe/NamedPipe.h"
#endif


bool CEventListener::Initialize()
{
	std::vector<const char*> vEvents = { 
		"client_beginconnect", "client_connected", "client_disconnect", "game_newmap", "teamplay_round_start", "scorestats_accumulated_update", "mvm_reset_stats", "mvm_wave_complete", "player_connect_client", "player_spawn", "player_changeclass", "player_hurt", "player_death", "vote_cast", "vote_maps_changed", "item_pickup", "revive_player_notify"
	};

	for (auto szEvent : vEvents)
	{
		I::GameEventManager->AddListener(this, szEvent, false);

		if (!I::GameEventManager->FindListener(this, szEvent))
		{
			U::Core.AppendFailText(std::format("Failed to add listener: {}", szEvent).c_str());
			m_bFailed = true;
		}
	}

	return !m_bFailed;
}

void CEventListener::Unload()
{
	I::GameEventManager->RemoveListener(this);
}

void CEventListener::FireGameEvent(IGameEvent* pEvent)
{
	if (!pEvent || G::Unload)
		return;

	static bool bAutoAbandonedMannUp = false;

	auto pLocal = H::Entities.GetLocal();
	auto uHash = FNV1A::Hash32(pEvent->GetName());

	F::Output.Event(pEvent, uHash, pLocal);
	if (I::EngineClient->IsPlayingDemo())
		return;

	F::CritHack.Event(pEvent, uHash, pLocal);
	F::AutoHeal.Event(pEvent, uHash);
	F::Misc.Event(pEvent, uHash);
#ifndef TEXTMODE
	F::Visuals.Event(pEvent, uHash);
#else
	F::NamedPipe.Event(pEvent, uHash);
#endif
	switch (uHash)
	{
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("game_newmap"):
	case FNV1A::Hash32Const("teamplay_round_start"):
	case FNV1A::Hash32Const("mvm_reset_stats"):
	{
		bAutoAbandonedMannUp = false;
		return;
	}
	case FNV1A::Hash32Const("mvm_wave_complete"):
	{
		if (!Vars::Misc::MannVsMachine::AutoAbandonMannUp.Value || bAutoAbandonedMannUp || !I::TFGCClientSystem)
			return;

		auto pGameRules = I::TFGameRules();
		if (!pGameRules || !pGameRules->m_bPlayingMannVsMachine() || pGameRules->GetCurrentMatchGroup() != k_eTFMatchGroup_MvM_MannUp)
			return;

		auto pObjectiveResource = H::Entities.GetObjectiveResource();
		if (!pObjectiveResource)
			return;

		int iWave = pObjectiveResource->m_nMannVsMachineWaveCount();
		int iMaxWave = pObjectiveResource->m_nMannVsMachineMaxWaveCount();
		int iCompletedWave = pObjectiveResource->m_bMannVsMachineBetweenWaves() && iWave > 1 ? iWave - 1 : iWave;
		if (iMaxWave <= 1 || iCompletedWave != iMaxWave - 1)
			return;

		bAutoAbandonedMannUp = true;
		I::TFGCClientSystem->AbandonCurrentMatch();
		return;
	}
	case FNV1A::Hash32Const("player_hurt"):
	{
		F::Resolver.PlayerHurt(pEvent);
		F::CheatDetection.ReportDamage(pEvent);
		return;
	}
	case FNV1A::Hash32Const("player_spawn"):
	{
		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid")) != I::EngineClient->GetLocalPlayer())
			return;

		F::Backtrack.SetLerp();
#ifndef TEXTMODE
		F::Killstreak.PlayerSpawn(pEvent);
#endif
		F::NavEngine.CancelPath();
		F::NavBotDanger.ResetSpawn();
		return;
	}
	case FNV1A::Hash32Const("revive_player_notify"):
	{
		if (!Vars::Misc::MannVsMachine::InstantRevive.Value || pEvent->GetInt("entindex") != I::EngineClient->GetLocalPlayer())
			return;

		KeyValues* kv = new KeyValues("MVM_Revive_Response");
		kv->SetBool("accepted", true);
		I::EngineClient->ServerCmdKeyValues(kv);
	}
	}
}
