#include "NavBotCore.h"

#include "Hazards/Hazards.h"
#include "NavAreaUtils.h"
#include "NavEngine/NavEngine.h"
#include "NavBotJobs/NavBotJobs.h"
#include "NavRuntime.h"
#include "NavEngine/Controllers/MVMController/MVMController.h"
#include "../FollowBot/FollowBot.h"
#include "../CritHack/CritHack.h"
#include "../Misc/Misc.h"
#include "../PacketManip/FakeLag/FakeLag.h"
#include "../Ticks/Ticks.h"
#include "../ImGui/IndicatorPanel.h"


void CNavBotCore::UpdateSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	static Timer tSlotTimer{};
	if (!tSlotTimer.Run(0.2f))
		return;

	// Prioritize reloading
	int iReloadSlot = F::NavBotReload.m_iLastReloadSlot = F::NavBotReload.GetReloadWeaponSlot(pLocal, tClosestEnemy);

	if (F::NavBotEngineer.IsEngieMode(pLocal))
	{
		int iSwitch = 0;
		switch (F::NavBotEngineer.m_eTaskStage)
		{
			// We are currently building something
		case EngineerTaskStageEnum::BuildSentry:
		case EngineerTaskStageEnum::BuildDispenser:
			if (F::NavBotEngineer.m_tCurrentBuildingSpot.m_flCost != FLT_MAX && F::NavBotEngineer.m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= 500.f)
			{
				if (pLocal->m_bCarryingObject())
				{
					auto pWeapon = pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>();
					if (pWeapon && pWeapon->GetSlot() != 3)
						F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
				}
				return;
			}
			break;
			// We are currently upgrading/repairing something
		case EngineerTaskStageEnum::SmackSentry:
			iSwitch = F::NavBotEngineer.m_flDistToSentry <= 300.f;
			break;
		case EngineerTaskStageEnum::SmackDispenser:
			iSwitch = F::NavBotEngineer.m_flDistToDispenser <= 500.f;
			break;
		default:
			break;
		}

		if (iSwitch)
		{
			if (iSwitch == 1)
			{
				if (F::BotUtils.m_iCurrentSlot < SLOT_MELEE)
					F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
			}
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

void CNavBotCore::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::NavBot::Enabled.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value ||
		!pLocal->IsAlive() || F::NavEngine.m_eCurrentPriority == PriorityListEnum::Followbot || F::FollowBot.m_bActive || !F::NavEngine.IsReady())
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

	if (Vars::Debug::Info.Value)
	{
		for (const auto& segment : F::BotUtils.m_vWalkableSegments)
		{
			G::LineStorage.push_back({ { segment.first, segment.second }, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 0, 255, 0, 255 } });
		}

		if (F::BotUtils.m_vPredictedJumpPos.Length() > 0.f)
		{
			G::LineStorage.push_back({ { pLocal->GetAbsOrigin(), F::BotUtils.m_vPredictedJumpPos }, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 255, 255, 0, 255 } });
			G::SphereStorage.push_back({ F::BotUtils.m_vJumpPeakPos, 5.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
			G::SphereStorage.push_back({ F::BotUtils.m_vPredictedJumpPos, 5.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 0, 0, 255, 255 }, { 0, 0, 0, 0 } });
		}
	}

	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::StayNear)
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
	{
		ResetRuntimeState(pCmd);
		return;
	}

	if (!pWeapon)
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

	// Update our current nav area
	if (!F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin()))
	{
		// This should never happen.
		// In case it did then theres something wrong with nav engine
		ResetRuntimeState(pCmd);
		return;
	}

	// Recharge doubletap every n seconds
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

	// Not used
	// RefreshSniperSpots();
	m_tJobSystem.RefreshSharedState(pLocal);

	m_tSelectedConfig = NavBotConfig::Select(pLocal, pWeapon);

	UpdateSlot(pLocal, F::BotUtils.m_tClosestEnemy);
	F::Hazards.Update(pLocal);

	if (F::MVMController.IsActive() && F::MVMController.Run(pCmd, pLocal, pWeapon))
	{
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		UpdateRunReloadInput(pCmd, false);
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
		bool bIsPathing = F::NavEngine.IsPathing();
		if (!bIsPathing)
		{
			// If we have a job but no path, we consider it idle (stuck or waiting for gods agreement to move lol)
		}
		else
		{
			m_tIdleTimer.Update();
			m_tAntiStuckTimer.Update();
		}

		// Force crithack in dangerous conditions
		// TODO:
		// Maybe add some logic to it (more logic)
		CTFPlayer* pPlayer = nullptr;
		switch (F::NavEngine.m_eCurrentPriority)
		{
		case PriorityListEnum::StayNear:
			pPlayer = I::ClientEntityList->GetClientEntity(F::NavBotStayNear.m_iStayNearTargetIdx)->As<CTFPlayer>();
			if (pPlayer)
				F::CritHack.m_bForce = !pPlayer->IsDormant() && pPlayer->m_iHealth() >= pWeapon->GetDamage();
			break;
		case PriorityListEnum::MeleeAttack:
		case PriorityListEnum::GetHealth:
		case PriorityListEnum::EscapeDanger:
			pPlayer = I::ClientEntityList->GetClientEntity(F::BotUtils.m_tClosestEnemy.m_iEntIdx)->As<CTFPlayer>();
			F::CritHack.m_bForce = pPlayer && !pPlayer->IsDormant() && pPlayer->m_iHealth() >= pWeapon->GetDamage();
			break;
		default:
			F::CritHack.m_bForce = false;
			break;
		}
	}
	else if (F::NavEngine.IsReady() && !F::NavEngine.IsSetupTime())
	{
		float flIdleTime = SDK::PlatFloatTime() - m_tIdleTimer.GetLastUpdate();
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
	switch (F::NavEngine.m_eCurrentPriority)
	{
	case PriorityListEnum::Patrol:
	{
		auto s_job = F::NavBotRoam.m_bDefending ? std::wstring(L"Defend") : std::wstring(L"Patrol");
		if (F::NavBotRoam.m_bDefending && !F::NavBotCapture.m_sCaptureStatus.empty())
		{
			s_job += L" (";
			s_job += F::NavBotCapture.m_sCaptureStatus;
			s_job += L')';
		}
		return s_job;
	}
	case PriorityListEnum::LowPrioGetHealth:
		return L"Get health (Low-Prio)";
	case PriorityListEnum::StayNear:
		return std::format(L"Stalk enemy ({})", F::NavBotStayNear.m_sFollowTargetName.data());
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
		auto s_job = std::wstring(L"Capture");
		if (!F::NavBotCapture.m_sCaptureStatus.empty())
		{
			s_job += L" (";
			s_job += F::NavBotCapture.m_sCaptureStatus;
			s_job += L')';
		}
		return s_job;
	}
	case PriorityListEnum::MeleeAttack:
		return L"Melee";
	case PriorityListEnum::Engineer:
	{
		std::wstring s_job = L"Engineer (";
		switch (F::NavBotEngineer.m_eTaskStage)
		{
		case EngineerTaskStageEnum::BuildSentry:
			s_job += L"Build sentry";
			break;
		case EngineerTaskStageEnum::BuildDispenser:
			s_job += L"Build dispenser";
			break;
		case EngineerTaskStageEnum::SmackSentry:
			s_job += L"Smack sentry";
			break;
		case EngineerTaskStageEnum::SmackDispenser:
			s_job += L"Smack dispenser";
			break;
		default:
			s_job += L"None";
			break;
		}
		s_job += L')';
		return s_job;
	}
	case PriorityListEnum::GetHealth:
		return L"Get health";
	case PriorityListEnum::EscapeSpawn:
		return L"Escape spawn";
	case PriorityListEnum::EscapeDanger:
		return L"Escape danger";
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
	default:
		return L"None";
	}
}

void CNavBotCore::Draw(CTFPlayer* pLocal)
{
	if (!pLocal || !(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) || !pLocal->IsAlive())
		return;

	const bool b_is_ready = F::NavEngine.IsReady();
	if (!Vars::Debug::Info.Value && !b_is_ready)
		return;

	int x = Vars::Menu::NavBotDisplay.Value.x;
	int y = Vars::Menu::NavBotDisplay.Value.y + 8;
	const auto& f_font = H::Fonts.GetFont(FONT_INDICATORS);
	const int n_tall = f_font.m_nTall + H::Draw.Scale(1);
	ImDrawList* p_draw_list = ImGui::GetForegroundDrawList();

	EAlign e_align = ALIGN_TOP;
	if (x <= 100 + H::Draw.Scale(50, Scale_Round))
	{
		x -= H::Draw.Scale(42, Scale_Round);
		e_align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
	{
		x += H::Draw.Scale(42, Scale_Round);
		e_align = ALIGN_TOPRIGHT;
	}

	const auto& t_color = F::NavEngine.IsPathing() ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	const auto& t_ready_color = b_is_ready ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	int i_in_spawn = -1;
	int i_area_flags = -1;
	if (F::NavEngine.IsNavMeshLoaded())
	{
		if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
		{
			i_area_flags = pLocalArea->m_iTFAttributeFlags;
			i_in_spawn = i_area_flags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED);
		}
	}

	const auto s_job = BuildJobLabel();
	DrawIndicatorText(
		p_draw_list,
		x,
		y,
		t_color,
		Vars::Menu::Theme::Background.Value,
		e_align,
		std::format("Job: {} {}", SDK::ConvertWideToUTF8(s_job), F::CritHack.m_bForce ? "(Crithack on)" : ""));

	if (F::NavEngine.IsPathing())
	{
		auto p_crumbs = F::NavEngine.GetCrumbs();
		const float fl_dist = pLocal->GetAbsOrigin().DistTo(F::NavEngine.m_vLastDestination);
		DrawIndicatorText(p_draw_list, x, y += n_tall, t_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Nodes: {} (Dist: {:.0f})", p_crumbs->size(), fl_dist));
	}

	const float fl_idle_time = SDK::PlatFloatTime() - F::NavBotCore.m_tIdleTimer.GetLastUpdate();
	if (fl_idle_time > 2.0f && F::NavEngine.IsPathing())
		DrawIndicatorText(p_draw_list, x, y += n_tall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, e_align, std::format("Stuck: {:.1f}s", fl_idle_time));

	if (!F::NavEngine.IsPathing() && !F::NavEngine.m_sLastFailureReason.empty())
		DrawIndicatorText(p_draw_list, x, y += n_tall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, e_align, std::format("Failed: {}", F::NavEngine.m_sLastFailureReason));

	if (!Vars::Debug::Info.Value)
		return;

	DrawIndicatorText(p_draw_list, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Is ready: {}", std::to_string(b_is_ready)));
	DrawIndicatorText(p_draw_list, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Priority: {}", static_cast<int>(F::NavEngine.m_eCurrentPriority)));
	DrawIndicatorText(p_draw_list, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("In spawn: {}", std::to_string(i_in_spawn)));
	DrawIndicatorText(p_draw_list, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Area flags: {}", std::to_string(i_area_flags)));

	if (F::NavEngine.IsNavMeshLoaded())
	{
		DrawIndicatorText(p_draw_list, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Map: {}", F::NavEngine.GetNavFilePath()));
		if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
			DrawIndicatorText(p_draw_list, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Area ID: {}", pLocalArea->m_uId));
		DrawIndicatorText(p_draw_list, x, y += n_tall, t_ready_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Total areas: {}", F::NavEngine.GetNavFile()->m_vAreas.size()));
	}

	if (F::NavEngine.IsPathing() || F::NavEngine.m_vLastDestination.Length() > 0.f)
	{
		const auto& v_dest = F::NavEngine.m_vLastDestination;
		DrawIndicatorText(p_draw_list, x, y += n_tall, t_color, Vars::Menu::Theme::Background.Value, e_align, std::format("Dest: {:.0f}, {:.0f}, {:.0f}", v_dest.x, v_dest.y, v_dest.z));
	}

	const bool b_is_idle = F::NavEngine.m_eCurrentPriority == PriorityListEnum::None || !F::NavEngine.IsPathing();
	DrawIndicatorText(
		p_draw_list,
		x,
		y += n_tall,
		b_is_idle ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value,
		Vars::Menu::Theme::Background.Value,
		e_align,
		std::format("Idle: {} ({:.1f}s)", b_is_idle ? "Yes" : "No", std::max(0.f, fl_idle_time)));
}

void CNavBotCore::DrawDangerOverlay(CTFPlayer* pLocal)
{
	if (!pLocal || !(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) || !pLocal->IsAlive() || !Vars::Debug::Info.Value)
		return;

	if (!Vars::Misc::Movement::NavBot::DangerOverlay.Value)
		return;

	int i_drawn = 0;
	const float fl_max_dist = Vars::Misc::Movement::NavBot::DangerOverlayMaxDist.Value;
	const float fl_max_dist_sqr = fl_max_dist * fl_max_dist;
	for (const auto& [p_area, t_data] : F::Hazards.GetHazardMap())
	{
		if (!F::NavEngine.GetNavMap() || !F::NavEngine.GetNavMap()->IsAreaValid(p_area) || t_data.m_flCost <= 0.f)
			continue;

		if (p_area->m_vCenter.DistToSqr(pLocal->GetAbsOrigin()) > fl_max_dist_sqr)
			continue;

		Color_t t_overlay_color = Color_t(255, 200, 0, 80);
		if (t_data.m_flCost >= HAZARD_COST_STICKY)
			t_overlay_color = Color_t(255, 50, 50, 90);
		else if (t_data.m_flCost >= HAZARD_COST_ENEMY_NORMAL)
			t_overlay_color = Color_t(255, 140, 0, 90);

		G::SphereStorage.push_back({ p_area->m_vCenter, 24.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, t_overlay_color, Color_t(), true });

		if (++i_drawn >= 64)
			break;
	}
}
