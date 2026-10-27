#include "NavBotCore.h"
#include "Hazards.h"
#include "Jobs/NavBotJobs.h"
#include "NavEngine.h"
#include "BotUtils.h"
#include "Objectives.h"
#include "../FollowBot/FollowBot.h"
#include "../CritHack/CritHack.h"
#include "../Misc/Misc.h"
#include "../PacketManip/FakeLag/FakeLag.h"
#include "../Ticks/Ticks.h"

void CNavBotCore::UpdateSlot(CTFPlayer* pLocal, const ClosestEnemy_t& tClosestEnemy)
{
	if (!m_tSlotTimer.Run(0.2f))
		return;

	const int iReloadSlot = F::NavBotReload.m_iLastReloadSlot = F::NavBotReload.GetReloadWeaponSlot(pLocal, tClosestEnemy);

	if (F::NavBotEngineer.IsEngieMode(pLocal))
	{
		bool bSmack = false;
		switch (F::NavBotEngineer.m_eTaskStage)
		{
		case EngineerTaskStageEnum::BuildSentry:
		case EngineerTaskStageEnum::BuildDispenser:
			if (F::NavBotEngineer.m_tCurrentBuildingSpot.m_flCost != FLT_MAX && F::NavBotEngineer.m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= 500.f)
			{
				if (pLocal->m_bCarryingObject())
				{
					auto pActiveWeapon = pLocal->m_hActiveWeapon().Get();
					if (pActiveWeapon && pActiveWeapon->As<CTFWeaponBase>()->GetSlot() != SLOT_PDA1)
						F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
				}
				return;
			}
			break;
		case EngineerTaskStageEnum::SmackSentry:
			bSmack = F::NavBotEngineer.m_flDistToSentry <= 300.f;
			break;
		case EngineerTaskStageEnum::SmackDispenser:
			bSmack = F::NavBotEngineer.m_flDistToDispenser <= 500.f;
			break;
		case EngineerTaskStageEnum::HelpBuilding:
			bSmack = F::NavBotEngineer.m_flDistToHelpTarget <= 300.f;
			break;
		default:
			break;
		}

		if (bSmack)
		{
			if (F::BotUtils.m_iCurrentSlot < SLOT_MELEE)
				F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
			return;
		}
	}

	const int iDesiredSlot = iReloadSlot != -1 ? iReloadSlot : Vars::Misc::Movement::BotUtils::WeaponSlot.Value ? F::BotUtils.m_iBestSlot : -1;
	if (F::BotUtils.m_iCurrentSlot != iDesiredSlot)
		F::BotUtils.SetSlot(pLocal, iDesiredSlot);
}

void CNavBotCore::UpdateRunReloadInput(CUserCmd* pCmd, bool bShouldHold)
{
	if (!pCmd)
	{
		m_bHoldingRunReload = bShouldHold;
		return;
	}

	if (bShouldHold)
		pCmd->buttons |= IN_RELOAD;
	else if (m_bHoldingRunReload)
		pCmd->buttons &= ~IN_RELOAD;

	m_bHoldingRunReload = bShouldHold;
}

void CNavBotCore::ResetRuntimeState(CUserCmd* pCmd)
{
	F::NavBotStayNear.m_iStayNearTargetIdx = -1;
	F::NavBotReload.m_iLastReloadSlot = -1;
	F::CritHack.m_bForce = false;
	m_tIdleTimer.Update();
	m_tAntiStuckTimer.Update();
	UpdateRunReloadInput(pCmd, false);
}

void CNavBotCore::ResetBusy(CUserCmd* pCmd)
{
	m_tIdleTimer.Update();
	m_tAntiStuckTimer.Update();
	UpdateRunReloadInput(pCmd, false);
}

static bool IsWeaponValidForDT(CTFWeaponBase* pWeapon)
{
	if (!pWeapon || F::BotUtils.m_iCurrentSlot == SLOT_MELEE)
		return false;

	auto iWepID = pWeapon->GetWeaponID();
	if (iWepID == TF_WEAPON_SNIPERRIFLE || iWepID == TF_WEAPON_SNIPERRIFLE_CLASSIC || iWepID == TF_WEAPON_SNIPERRIFLE_DECAP)
		return false;

	return SDK::WeaponDoesNotUseAmmo(pWeapon, false);
}

static bool IsIdleNudgeAllowed()
{
	if (NavJobUtils::IsSpawnArea(F::NavEngine.GetLocalNavArea()))
		return false;

	auto pGameRules = I::TFGameRules();
	if (!pGameRules)
		return true;

	const int iRoundState = pGameRules->m_iRoundState();
	return (iRoundState == GR_STATE_RND_RUNNING || iRoundState == GR_STATE_STALEMATE) && !pGameRules->m_bInWaitingForPlayers();
}

void CNavBotCore::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const bool bEnabled = Vars::Misc::Movement::NavBot::Enabled.Value && Vars::Misc::Movement::NavEngine::Enabled.Value;
	if (bEnabled && I::EngineClient->IsInGame())
	{
		if (!m_bWasEnabled)
			Reset();
		m_bWasEnabled = true;
	}
	else
		m_bWasEnabled = false;

	if (!bEnabled ||
		!pLocal || !pCmd || !pLocal->IsAlive() || F::NavEngine.m_eCurrentPriority == PriorityListEnum::Followbot || F::FollowBot.m_bActive || !F::NavEngine.IsReady())
	{
		ResetRuntimeState(pCmd);
		return;
	}

	if (NavRuntime::IsMovementLocked(pLocal))
	{
		if (F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();

		ResetRuntimeState(pCmd);
		return;
	}

	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::StayNear)
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap || !pWeapon)
	{
		ResetRuntimeState(pCmd);
		return;
	}

	if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
	{
		m_vStuckAngles = pCmd->viewangles;
		ResetRuntimeState(pCmd);
		return;
	}

	if (pLocal->m_iClass() == TF_CLASS_ENGINEER && pLocal->m_bCarryingObject() && !F::NavBotEngineer.IsEngieMode(pLocal))
	{
		if (F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();

		if (m_tDropCarriedObjectTimer.Run(0.5f))
		{
			I::EngineClient->ClientCmd_Unrestricted("destroy 0");
			I::EngineClient->ClientCmd_Unrestricted("destroy 1");
			I::EngineClient->ClientCmd_Unrestricted("destroy 2");
			I::EngineClient->ClientCmd_Unrestricted("destroy 3");
		}

		F::NavBotEngineer.Reset();
		ResetRuntimeState(pCmd);
		return;
	}

	const Vector vOrigin = pLocal->GetAbsOrigin();
	if (!F::NavEngine.GetLocalNavArea(vOrigin) && !F::NavEngine.GetPathStartArea(pLocal, vOrigin))
	{
		ResetRuntimeState(pCmd);
		return;
	}

	auto pGameRules = I::TFGameRules();
	if (pGameRules && pGameRules->m_bPlayingMannVsMachine())
	{
		if (F::Misc.IsBuyBotBusy())
		{
			ResetRuntimeState(pCmd);
			return;
		}

		if (NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::MVMSniper) &&
			pLocal->m_iClass() == TF_CLASS_SNIPER && F::NavBotMVMSniper.Run(pCmd, pLocal))
		{
			ResetBusy(pCmd);
			return;
		}
	}

	if (Vars::Misc::Movement::NavBot::RechargeDT.Value && IsWeaponValidForDT(pWeapon))
	{
		if (!F::Ticks.m_bRechargeQueue &&
			(Vars::Misc::Movement::NavBot::RechargeDT.Value != Vars::Misc::Movement::NavBot::RechargeDTEnum::WaitForFL || !Vars::Fakelag::Fakelag.Value || !F::FakeLag.m_iGoal) &&
			G::Attacking != 1 &&
			(F::Ticks.m_iShiftedTicks < F::Ticks.m_iShiftedGoal) && m_tDoubletapRecharge.Check(Vars::Misc::Movement::NavBot::RechargeDTDelay.Value))
			F::Ticks.m_bRechargeQueue = true;
		else if (F::Ticks.m_iShiftedTicks >= F::Ticks.m_iShiftedGoal)
			m_tDoubletapRecharge.Update();
	}

	m_tJobSystem.RefreshSharedState(pLocal);

	m_tSelectedConfig = NavBotConfig::Select(pLocal);

	UpdateSlot(pLocal, F::BotUtils.m_tClosestEnemy);
	F::Hazards.Update(pLocal);
	F::CritHack.m_bForce = false;

	if (F::MVMController.IsActive() && F::MVMController.Run(pCmd, pLocal, pWeapon))
	{
		ResetBusy(pCmd);
		F::CritHack.m_bForce = F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMTank || F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMCombat;
		return;
	}

	const auto tJobResult = m_tJobSystem.Run(pCmd, pLocal, pWeapon);

	bool bShouldHoldReload = tJobResult.m_bRunReload || tJobResult.m_bRunSafeReload;
	if (bShouldHoldReload && F::NavBotReload.m_iLastReloadSlot != -1 && F::BotUtils.m_iCurrentSlot != F::NavBotReload.m_iLastReloadSlot)
		bShouldHoldReload = false;

	UpdateRunReloadInput(pCmd, bShouldHoldReload);

	if (tJobResult.m_bHasJob)
	{
		if (F::NavEngine.IsPathing())
		{
			m_tIdleTimer.Update();
			m_tAntiStuckTimer.Update();
		}

		int iTargetIdx = -1;
		switch (F::NavEngine.m_eCurrentPriority)
		{
		case PriorityListEnum::StayNear:
			iTargetIdx = F::NavBotStayNear.m_iStayNearTargetIdx;
			break;
		case PriorityListEnum::MeleeAttack:
		case PriorityListEnum::GetHealth:
		case PriorityListEnum::EscapeDanger:
			iTargetIdx = F::BotUtils.m_tClosestEnemy.m_iEntIdx;
			break;
		default:
			break;
		}

		if (iTargetIdx != -1)
		{
			auto pEntity = I::ClientEntityList->GetClientEntity(iTargetIdx);
			auto pTarget = pEntity ? pEntity->As<CTFPlayer>() : nullptr;
			F::CritHack.m_bForce = pTarget && !pTarget->IsDormant() && pTarget->m_iHealth() >= pWeapon->GetDamage();
		}
	}
	else if (!F::NavEngine.IsSetupTime() && F::NavEngine.m_eCurrentPriority == PriorityListEnum::None && IsIdleNudgeAllowed())
	{
		const float flIdleTime = SDK::PlatFloatTime() - m_tIdleTimer.GetLastUpdate();
		if (flIdleTime > m_flNextIdleTime)
		{
			if (flIdleTime < m_flNextIdleTime + 0.5f)
			{
				pCmd->forwardmove = 450.f;

				if (m_tAntiStuckTimer.Run(m_flNextStuckAngleChange))
				{
					m_flNextStuckAngleChange = SDK::RandomFloat(0.1f, 0.3f);
					m_vStuckAngles.y += SDK::RandomFloat(-15.f, 15.f);
					Math::ClampAngles(m_vStuckAngles);
				}

				SDK::FixMovement(pCmd, m_vStuckAngles);
			}
			else
			{
				m_tIdleTimer.Update();
				m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
			}
		}
	}
	else
	{
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		m_vStuckAngles = pCmd->viewangles;
		m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
	}
}

void CNavBotCore::Reset()
{
	m_tJobSystem.Reset();
	m_tSlotTimer = Timer();
	m_tDropCarriedObjectTimer = Timer();
	m_tDoubletapRecharge = Timer();
	m_bHoldingRunReload = false;
	m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
}
