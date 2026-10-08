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
#include "../ImGui/IndicatorPanel.h"

void CNavBotCore::UpdateSlot(CTFPlayer* pLocal, const ClosestEnemy_t& tClosestEnemy)
{
	static Timer tSlotTimer{};
	if (!tSlotTimer.Run(0.2f))
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
	if (!Vars::Misc::Movement::NavBot::Enabled.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value ||
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

	if (Vars::Debug::Info.Value && F::BotUtils.m_vPredictedJumpPos.Length() > 0.f)
	{
		const float flExpire = I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f;
		G::LineStorage.push_back({ { pLocal->GetAbsOrigin(), F::BotUtils.m_vPredictedJumpPos }, flExpire, { 255, 255, 0, 255 } });
		G::SphereStorage.push_back({ F::BotUtils.m_vJumpPeakPos, 5.f, 10, 10, flExpire, { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
		G::SphereStorage.push_back({ F::BotUtils.m_vPredictedJumpPos, 5.f, 10, 10, flExpire, { 0, 0, 255, 255 }, { 0, 0, 0, 0 } });
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

		static Timer tDropCarriedObjectTimer{};
		if (tDropCarriedObjectTimer.Run(0.5f))
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

	if (!F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin()))
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

	static Timer tDoubletapRecharge{};
	if (Vars::Misc::Movement::NavBot::RechargeDT.Value && IsWeaponValidForDT(pWeapon))
	{
		if (!F::Ticks.m_bRechargeQueue &&
			(Vars::Misc::Movement::NavBot::RechargeDT.Value != Vars::Misc::Movement::NavBot::RechargeDTEnum::WaitForFL || !Vars::Fakelag::Fakelag.Value || !F::FakeLag.m_iGoal) &&
			G::Attacking != 1 &&
			(F::Ticks.m_iShiftedTicks < F::Ticks.m_iShiftedGoal) && tDoubletapRecharge.Check(Vars::Misc::Movement::NavBot::RechargeDTDelay.Value))
			F::Ticks.m_bRechargeQueue = true;
		else if (F::Ticks.m_iShiftedTicks >= F::Ticks.m_iShiftedGoal)
			tDoubletapRecharge.Update();
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
	m_bHoldingRunReload = false;
	m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
}

static std::wstring BuildJobLabel()
{
	const auto AppendStatus = [](std::wstring& sLabel, const std::wstring& sStatus)
		{
			if (sStatus.empty())
				return;

			sLabel += L" (";
			sLabel += sStatus;
			sLabel += L')';
		};

	switch (F::NavEngine.m_eCurrentPriority)
	{
	case PriorityListEnum::Patrol:
	{
		if (!F::NavBotRoam.m_bDefending)
			return L"Patrol";

		std::wstring sLabel = L"Defend";
		AppendStatus(sLabel, F::NavBotCapture.m_sCaptureStatus);
		return sLabel;
	}
	case PriorityListEnum::LowPrioGetHealth:
		return L"Get health (Low-Prio)";
	case PriorityListEnum::StayNear:
		return std::format(L"Stalk enemy ({})", F::NavBotStayNear.m_sFollowTargetName);
	case PriorityListEnum::RunReload:
		return L"Run reload";
	case PriorityListEnum::RunSafeReload:
		return L"Run safe reload";
	case PriorityListEnum::SnipeSentry:
		return L"Snipe sentry";
	case PriorityListEnum::GetAmmo:
		return L"Get ammo";
	case PriorityListEnum::Capture:
	{
		std::wstring sLabel = L"Capture";
		AppendStatus(sLabel, F::NavBotCapture.m_sCaptureStatus);
		return sLabel;
	}
	case PriorityListEnum::MeleeAttack:
		return L"Melee";
	case PriorityListEnum::Engineer:
	{
		switch (F::NavBotEngineer.m_eTaskStage)
		{
		case EngineerTaskStageEnum::BuildSentry:
			return L"Engineer (Build sentry)";
		case EngineerTaskStageEnum::BuildDispenser:
			return L"Engineer (Build dispenser)";
		case EngineerTaskStageEnum::SmackSentry:
			return L"Engineer (Smack sentry)";
		case EngineerTaskStageEnum::SmackDispenser:
			return L"Engineer (Smack dispenser)";
		default:
			return L"Engineer (None)";
		}
	}
	case PriorityListEnum::GetHealth:
		return L"Get health";
	case PriorityListEnum::EscapeSpawn:
		return L"Escape spawn";
	case PriorityListEnum::EscapeDanger:
	{
		std::wstring sLabel = L"Escape danger";
		AppendStatus(sLabel, F::NavBotDanger.m_sDangerStatus);
		return sLabel;
	}
	case PriorityListEnum::Followbot:
		return L"FollowBot";
	case PriorityListEnum::MVMTank:
		return L"MvM tank";
	case PriorityListEnum::MVMCombat:
		return L"MvM combat";
	case PriorityListEnum::MVMMoney:
		return L"MvM money";
	case PriorityListEnum::MVMFrontline:
		return L"MvM frontline";
	case PriorityListEnum::MVMSniper:
		return L"MvM sniper";
	case PriorityListEnum::BuyBot:
		return L"Buy bot";
	default:
		return L"None";
	}
}

void CNavBotCore::CacheDrawInfo(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) || !pLocal || !pLocal->IsAlive())
	{
		m_tDrawCache.Set({});
		return;
	}

	const bool bIsReady = F::NavEngine.IsReady();
	if (!Vars::Debug::Info.Value && !bIsReady)
	{
		m_tDrawCache.Set({});
		return;
	}

	const auto& tColor = F::NavEngine.IsPathing() ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	const auto& tReadyColor = bIsReady ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	int iInSpawn = -1;
	int iAreaFlags = -1;
	if (F::NavEngine.IsNavMeshLoaded())
	{
		if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
		{
			iAreaFlags = pLocalArea->m_iTFAttributeFlags;
			iInSpawn = iAreaFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED);
		}
	}

	std::vector<NavIndicatorLine_t> vCachedLines = {};
	vCachedLines.push_back({ std::format("Job: {} {}", SDK::ConvertWideToUTF8(BuildJobLabel()), F::CritHack.m_bForce ? "(Crithack on)" : ""), tColor });

	if (F::NavEngine.IsPathing())
	{
		const float flDist = pLocal->GetAbsOrigin().DistTo(F::NavEngine.m_vLastDestination);
		vCachedLines.push_back({ std::format("Nodes: {} (Dist: {:.0f})", F::NavEngine.GetCrumbs()->size(), flDist), tColor });
	}

	const float flIdleTime = SDK::PlatFloatTime() - m_tIdleTimer.GetLastUpdate();
	if (flIdleTime > 2.0f && F::NavEngine.IsPathing())
		vCachedLines.push_back({ std::format("Stuck: {:.1f}s", flIdleTime), Vars::Menu::Theme::Active.Value });

	if (!F::NavEngine.IsPathing() && !F::NavEngine.m_sLastFailureReason.empty())
		vCachedLines.push_back({ std::format("Failed: {}", F::NavEngine.m_sLastFailureReason), Vars::Menu::Theme::Active.Value });

	if (Vars::Debug::Info.Value)
	{
		vCachedLines.push_back({ std::format("Is ready: {}", static_cast<int>(bIsReady)), tReadyColor });
		vCachedLines.push_back({ std::format("Priority: {}", static_cast<int>(F::NavEngine.m_eCurrentPriority)), tReadyColor });
		vCachedLines.push_back({ std::format("In spawn: {}", iInSpawn), tReadyColor });
		vCachedLines.push_back({ std::format("Area flags: {}", iAreaFlags), tReadyColor });

		if (F::NavEngine.IsNavMeshLoaded())
		{
			vCachedLines.push_back({ std::format("Map: {}", F::NavEngine.GetNavFilePath()), tReadyColor });
			if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
				vCachedLines.push_back({ std::format("Area ID: {}", pLocalArea->m_uId), tReadyColor });
			vCachedLines.push_back({ std::format("Total areas: {}", F::NavEngine.GetNavFile()->m_vAreas.size()), tReadyColor });
		}

		if (F::NavEngine.IsPathing() || F::NavEngine.m_vLastDestination.Length() > 0.f)
		{
			const auto& vDest = F::NavEngine.m_vLastDestination;
			vCachedLines.push_back({ std::format("Dest: {:.0f}, {:.0f}, {:.0f}", vDest.x, vDest.y, vDest.z), tColor });
		}

		const bool bIsIdle = F::NavEngine.m_eCurrentPriority == PriorityListEnum::None || !F::NavEngine.IsPathing();
		vCachedLines.push_back({ std::format("Idle: {} ({:.1f}s)", bIsIdle ? "Yes" : "No", std::max(0.f, flIdleTime)), bIsIdle ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value });
	}

	m_tDrawCache.Set(std::move(vCachedLines));
}

void CNavBotCore::Draw()
{
	const std::vector<NavIndicatorLine_t> vCachedLines = m_tDrawCache.Get();
	if (vCachedLines.empty())
		return;

	int x = Vars::Menu::NavBotDisplay.Value.x;
	const int y = Vars::Menu::NavBotDisplay.Value.y + 8;
	const auto& tFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = tFont.m_nTall + H::Draw.Scale(1);
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	EAlign eAlign = ALIGN_TOP;
	if (x <= 100 + H::Draw.Scale(50, Scale_Round))
	{
		x -= H::Draw.Scale(42, Scale_Round);
		eAlign = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
	{
		x += H::Draw.Scale(42, Scale_Round);
		eAlign = ALIGN_TOPRIGHT;
	}

	for (size_t i = 0; i < vCachedLines.size(); i++)
	{
		const int iY = y + static_cast<int>(i) * nTall;
		DrawIndicatorText(pDrawList, x, iY, vCachedLines[i].m_tColor, Vars::Menu::Theme::Background.Value, eAlign, vCachedLines[i].m_sText);
	}
}

void CNavBotCore::DrawDangerOverlay(CTFPlayer* pLocal)
{
	if (!pLocal || !(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) || !pLocal->IsAlive() || !Vars::Debug::Info.Value)
		return;

	if (!Vars::Misc::Movement::NavBot::DangerOverlay.Value)
		return;

	auto pNavMap = F::NavEngine.GetNavMap();
	if (!pNavMap)
		return;

	int iDrawn = 0;
	const float flMaxDist = Vars::Misc::Movement::NavBot::DangerOverlayMaxDist.Value;
	const float flMaxDistSqr = flMaxDist * flMaxDist;
	for (const auto& [pArea, tData] : F::Hazards.GetHazardMap())
	{
		if (!pNavMap->IsAreaValid(pArea) || tData.m_flCost <= 0.f)
			continue;

		if (pArea->m_vCenter.DistToSqr(pLocal->GetAbsOrigin()) > flMaxDistSqr)
			continue;

		Color_t tOverlayColor = Color_t(255, 200, 0, 80);
		if (tData.m_flCost >= HAZARD_COST_STICKY)
			tOverlayColor = Color_t(255, 50, 50, 90);
		else if (tData.m_flCost >= HAZARD_COST_ENEMY_NORMAL)
			tOverlayColor = Color_t(255, 140, 0, 90);

		G::SphereStorage.push_back({ pArea->m_vCenter, 24.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, tOverlayColor, Color_t(), true });

		if (++iDrawn >= 64)
			break;
	}
}
