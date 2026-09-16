#include "NavBotJobs.h"
#include "../Objectives.h"

inline bool IsBuildSpotFailed(const std::vector<Vector>& vFailedSpots, const Vector& vPos)
{
	for (const auto& vFailed : vFailedSpots)
	{
		if (vFailed.DistTo(vPos) < 72.f)
			return true;
	}

	return false;
}

static bool CanBuildAtPosition(CTFPlayer* pLocal, const Vector& vPos)
{
	CGameTrace trace;
	CTraceFilterNavigation filter(pLocal);
	const Vector vMins(-20.f, -20.f, 0.f);
	const Vector vMaxs(20.f, 20.f, 48.f);

	SDK::TraceHull(vPos + Vector(0, 0, 5), vPos + Vector(0, 0, 5), vMins, vMaxs, MASK_PLAYERSOLID, &filter, &trace);
	if (trace.DidHit())
		return false;

	SDK::Trace(vPos + Vector(0, 0, 10), vPos - Vector(0, 0, 10), MASK_PLAYERSOLID, &filter, &trace);
	return trace.DidHit();
}

bool CNavBotEngineer::BuildingNeedsToBeSmacked(CBaseObject* pBuilding)
{
	if (!pBuilding || pBuilding->m_bPlacing())
		return false;

	if (pBuilding->m_iUpgradeLevel() != 3 || pBuilding->m_iHealth() <= pBuilding->m_iMaxHealth() / 1.25f)
		return true;

	if (pBuilding->GetClassID() == ETFClassID::CObjectSentrygun)
		return pBuilding->As<CObjectSentrygun>()->m_iAmmoShells() <= pBuilding->As<CObjectSentrygun>()->MaxAmmoShells() / 2;

	return false;
}

bool CNavBotEngineer::NavToSentrySpot(Vector vLocalOrigin)
{
	static Timer tWaitUntilPathSentryTimer;

	if (!tWaitUntilPathSentryTimer.Run(0.3f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer;

	if (m_pMySentryGun && !m_pMySentryGun->m_bPlacing())
	{
		if (m_flDistToSentry <= 100.f)
			return true;

		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer ||
			F::NavEngine.NavTo(m_pMySentryGun->GetAbsOrigin(), PriorityListEnum::Engineer))
			return true;
	}

	if (m_vBuildingSpots.empty())
		return false;

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
		return true;

	for (auto& tSpot : m_vBuildingSpots)
	{
		if (IsBuildSpotFailed(m_vFailedSpots, tSpot.m_vPos))
			continue;
		if (m_tCurrentBuildingSpot.m_flCost != FLT_MAX && tSpot.m_vPos.DistTo(m_tCurrentBuildingSpot.m_vPos) < 8.f)
			continue;

		if (F::NavEngine.NavTo(tSpot.m_vPos, PriorityListEnum::Engineer))
		{
			m_tCurrentBuildingSpot = tSpot;
			m_flBuildYaw = 0.0f;
			return true;
		}
	}
	return false;
}

bool CNavBotEngineer::BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bDispenser)
{
	m_eTaskStage = bDispenser ? EngineerTaskStageEnum::BuildDispenser : EngineerTaskStageEnum::BuildSentry;

	if (m_flBuildYaw >= 360.0f || m_iBuildAttempts > 20)
	{
		m_vFailedSpots.push_back(m_tCurrentBuildingSpot.m_vPos);
		m_tCurrentBuildingSpot = {};
		m_iBuildAttempts = 0;
		m_flBuildYaw = 0.0f;
		return false;
	}

	int iRequiredMetal = (bDispenser || G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger) ? 100 : 130;
	if (pLocal->m_iMetalCount() < iRequiredMetal)
		return F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced);

	if (bDispenser && m_pMySentryGun && !m_pMySentryGun->m_bPlacing())
	{
		const Vector vSentry = m_pMySentryGun->GetAbsOrigin();
		if (m_tCurrentBuildingSpot.m_flCost == FLT_MAX || m_tCurrentBuildingSpot.m_vPos.DistTo(vSentry) < 48.f)
		{
			static const float aOff[][2] = { {96.f, 0.f}, {-96.f, 0.f}, {0.f, 96.f}, {0.f, -96.f}, {72.f, 72.f}, {72.f, -72.f}, {-72.f, 72.f}, {-72.f, -72.f} };
			for (const auto& tOff : aOff)
			{
				const Vector vPos = vSentry + Vector(tOff[0], tOff[1], 0.f);
				if (IsBuildSpotFailed(m_vFailedSpots, vPos) || !CanBuildAtPosition(pLocal, vPos))
					continue;
				m_tCurrentBuildingSpot = { vSentry.DistTo(vPos), vPos };
				break;
			}
		}
	}

	const float flPlaceDist = bDispenser ? 140.f : 200.f;
	if (m_tCurrentBuildingSpot.m_flCost != FLT_MAX && m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= flPlaceDist)
	{

		if (tClosestEnemy.m_flDist < 500.f && tClosestEnemy.m_pPlayer && tClosestEnemy.m_pPlayer->IsAlive() && !pLocal->m_bCarryingObject())
			return false;

		static Timer tRotationTimer;
		pCmd->viewangles.x = 20.0f;
		pCmd->viewangles.y = m_flBuildYaw;
		I::EngineClient->SetViewAngles(pCmd->viewangles);

		if (tRotationTimer.Run(0.3f))
			m_flBuildYaw += 45.0f;

		static Timer tAttemptTimer;
		if (tAttemptTimer.Run(0.3f))
			m_iBuildAttempts++;

		if (!pLocal->m_bCarryingObject())
		{
			static Timer command_timer;
			if (command_timer.Run(0.5f))
				I::EngineClient->ClientCmd_Unrestricted(std::format("build {}", bDispenser ? 0 : 2).c_str());
		}

		pCmd->buttons |= IN_ATTACK;
		pCmd->forwardmove = 20.0f;
		if (pCmd->sidemove == 0.0f)
			pCmd->sidemove = 1.0f;
		return true;
	}

	return NavToSentrySpot(pLocal->GetAbsOrigin());
}

bool CNavBotEngineer::SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	m_iBuildAttempts = 0;
	if (!pLocal->m_iMetalCount())
		return F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced);

	m_eTaskStage = pBuilding->GetClassID() == ETFClassID::CObjectDispenser ? EngineerTaskStageEnum::SmackDispenser : EngineerTaskStageEnum::SmackSentry;

	if (pBuilding->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) <= 100.f && F::BotUtils.m_iCurrentSlot == SLOT_MELEE)
	{
		if (G::Attacking == 1)
		{
			pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), pBuilding->GetCenter());
			I::EngineClient->SetViewAngles(pCmd->viewangles);
		}
		else
			pCmd->buttons |= IN_ATTACK;
	}
	else if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::Engineer)
		return F::NavEngine.NavTo(pBuilding->GetAbsOrigin(), PriorityListEnum::Engineer);

	return true;
}

bool CNavBotEngineer::GetFocusPoint(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bDefensive, FocusPoint_t& tOut)
{
	int iLocalIdx = pLocal->entindex();
	int iLocalTeam = pLocal->m_iTeamNum();
	int iEnemyTeam = (iLocalTeam == TF_TEAM_RED) ? TF_TEAM_BLUE : TF_TEAM_RED;
	Vector vLocalOrigin = pLocal->GetAbsOrigin();

	bool bSet = false;
	FocusPoint_t tFocus{ bDefensive, false, I::GlobalVars->curtime };
	if (bDefensive)
	{
		if (F::FlagController.GetSpawnPosition(iLocalTeam, tFocus.m_vPos)
			|| F::CPController.GetClosestControlPoint(vLocalOrigin, iEnemyTeam, tFocus.m_vPos))
			bSet = true;
		else if (auto pPayload = F::PLController.GetClosestPayload(vLocalOrigin, iEnemyTeam))
		{
			bSet = true;
			tFocus.m_bBack = true;
			tFocus.m_vPos = pPayload->GetAbsOrigin();
		}
		else if (tClosestEnemy.m_iEntIdx)
		{
			bSet = true;
			tFocus.m_vPos = tClosestEnemy.m_vOrigin;
			tFocus.m_bBack = true;
		}
	}
	else if (F::CPController.GetClosestControlPoint(vLocalOrigin, iLocalTeam, tFocus.m_vPos))
		bSet = true;
	else if (auto pPayload = F::PLController.GetClosestPayload(vLocalOrigin, iLocalTeam))
	{
		bSet = true;
		tFocus.m_vPos = pPayload->GetAbsOrigin();
	}
	else if (tClosestEnemy.m_iEntIdx)
	{
		bSet = true;
		tFocus.m_vPos = tClosestEnemy.m_vOrigin;
	}

	if (!bSet)
		return false;

	tFocus.m_pArea = F::NavEngine.FindClosestNavArea(tFocus.m_vPos, false);
	if (!tFocus.m_pArea)
		return false;

	auto vTeammates = H::Entities.GetGroup(EntityEnum::PlayerTeam);
	if (tFocus.m_bBack && vTeammates.size() - 1 > 0)
	{
		std::vector<std::pair<CTFPlayer*, float>> vTeammatesSorted;
		for (auto pTeammate : vTeammates)
		{
			int iTeammateIdx = pTeammate->entindex();
			if (iTeammateIdx == iLocalIdx)
				continue;

			Vector vOrigin;
			if (!F::BotUtils.GetDormantOrigin(iTeammateIdx, &vOrigin))
				continue;

			float flDist = vOrigin.DistTo(tFocus.m_vPos);
			if (flDist > 1200.f)
				continue;

			vTeammatesSorted.emplace_back(pTeammate->As<CTFPlayer>(), flDist);
		}

		if (!vTeammatesSorted.size())
		{
			tOut = tFocus;
			return true;
		}

		std::sort(vTeammatesSorted.begin(), vTeammatesSorted.end(), [](const std::pair<CTFPlayer*, float>& a, const std::pair<CTFPlayer*, float>& b) -> bool
			{
				return a.second < b.second;
			});

		CNavArea* pNewFocusArea = nullptr;
		Vector vFocus;
		int iTeammatesOnFocusPoint = 0;
		for (auto pTeammate : vTeammatesSorted | std::views::keys)
		{
			Vector vNewFocus = vFocus + pTeammate->GetAbsOrigin();

			auto pTempFocusArea = F::NavEngine.FindClosestNavArea(vNewFocus / (iTeammatesOnFocusPoint + 1));
			if (!pTempFocusArea || F::NavEngine.GetPathCost(tFocus.m_pArea, pTempFocusArea) > 4000.f)
				continue;

			vFocus = vNewFocus;
			pNewFocusArea = pTempFocusArea;
			iTeammatesOnFocusPoint++;
		}
		if (iTeammatesOnFocusPoint)
		{
			tFocus.m_pArea = pNewFocusArea;
			tFocus.m_vPos = vFocus / iTeammatesOnFocusPoint;
		}
	}

	tOut = tFocus;
	return true;
}

void CNavBotEngineer::RefreshBuildingSpots(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bForce)
{
	if (!IsEngieMode(pLocal))
		return;

	bool bHasGunslinger = G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger;
	static Timer tRefreshBuildingSpotsTimer;
	if (bForce || tRefreshBuildingSpotsTimer.Run(bHasGunslinger ? 1.f : 5.f))
	{
		m_vBuildingSpots.clear();

		FocusPoint_t tFocus;
		if (!GetFocusPoint(pLocal, tClosestEnemy, !bHasGunslinger, tFocus))
			return;

		m_tCurrentFocusPoint = tFocus;

		std::vector<CTFPlayer*> vEnemies;
		if (tFocus.m_bDefensive)
		{
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
			{
				auto pPlayer = pEntity->As<CTFPlayer>();
				if (pPlayer->IsDormant() || !pPlayer->IsAlive())
					continue;
				vEnemies.push_back(pPlayer);
			}
		}

		auto pNavFile = F::NavEngine.GetNavFile();
		if (!pNavFile)
			return;
		for (auto& tArea : pNavFile->m_vAreas)
		{
			if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT))
				continue;

			if (tArea.m_vCenter.DistTo(tFocus.m_vPos) > 2000.f)
				continue;

			if (tArea.IsBlocked(pLocal->m_iTeamNum()))
				continue;

			auto AddSpot = [&](CNavArea* pArea, const Vector& vPos, float flBonus = 0.f)
				{
					if (IsBuildSpotFailed(m_vFailedSpots, vPos))
						return;

					if (tFocus.m_pArea && F::NavEngine.GetPathCost(tFocus.m_pArea, pArea) > 4000.f)
						return;

					if (!CanBuildAtPosition(pLocal, vPos))
						return;

					float flDistToFocus = vPos.DistTo(tFocus.m_vPos);
					float flCost = flDistToFocus - flBonus;

					if (flDistToFocus > 2500.f)
						flCost += (flDistToFocus - 2500.f) * 2.f;

					for (auto pEnemy : vEnemies)
					{
						if (pEnemy->GetAbsOrigin().DistTo(vPos) < 600.f)
						{
							flCost += 2000.f;
							break;
						}
					}

					if (tArea.m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT)
						flCost -= 200.f;
					if (tArea.m_iTFAttributeFlags & TF_NAV_CONTROL_POINT)
						flCost -= 150.f;

					m_vBuildingSpots.emplace_back(flCost, vPos);
				};

			const bool bSentrySpot = (tArea.m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT) != 0;
			const bool bNearObjective = tArea.m_vCenter.DistTo(tFocus.m_vPos) < 1400.f;
			if (bSentrySpot || bNearObjective)
				AddSpot(&tArea, tArea.m_vCenter, bSentrySpot ? 250.f : 0.f);

			for (auto& tHidingSpot : tArea.m_vHidingSpots)
			{
				if (tHidingSpot.HasGoodCover())
					AddSpot(&tArea, tHidingSpot.m_vPos, 80.f);
			}
		}

		std::sort(m_vBuildingSpots.begin(), m_vBuildingSpots.end(),
			[](const BuildingSpot_t& a, const BuildingSpot_t& b) -> bool
			{
				return a.m_flCost < b.m_flCost;
			});
		if (m_vBuildingSpots.size() > 48)
			m_vBuildingSpots.resize(48);
	}
}

void CNavBotEngineer::Render()
{
	if (!Vars::Misc::Movement::NavEngine::Draw.Value)
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive() || pLocal->m_iClass() != TF_CLASS_ENGINEER)
		return;

	if (!m_vBuildingSpots.empty())
	{
		for (auto& tSpot : m_vBuildingSpots)
		{
			bool bIsCurrent = (tSpot.m_vPos == m_tCurrentBuildingSpot.m_vPos);
			Color_t color = bIsCurrent ? Color_t(0, 255, 0, 255) : Color_t(255, 255, 255, 100);

			H::Draw.RenderWireframeBox(tSpot.m_vPos, Vector(-30, -30, 0), Vector(30, 30, 66), Vector(0, 0, 0), color, false);
			if (bIsCurrent)
				H::Draw.RenderBox(tSpot.m_vPos, Vector(-30, -30, 0), Vector(30, 30, 66), Vector(0, 0, 0), Color_t(0, 255, 0, 50), false);
		}
		H::Draw.RenderWireframeSphere(m_tCurrentFocusPoint.m_vPos, 10.f, 36, 36, Color_t(255, 255, 0, 255));
	}
}

void CNavBotEngineer::RefreshLocalBuildings(CTFPlayer* pLocal)
{
	if (IsEngieMode(pLocal))
	{
		m_pMySentryGun = pLocal->GetObjectOfType(OBJ_SENTRYGUN)->As<CObjectSentrygun>();
		m_pMyDispenser = pLocal->GetObjectOfType(OBJ_DISPENSER)->As<CObjectDispenser>();
		m_flDistToSentry = m_pMySentryGun ? m_pMySentryGun->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) : FLT_MAX;
		m_flDistToDispenser = m_pMyDispenser ? m_pMyDispenser->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) : FLT_MAX;
	}
}

void CNavBotEngineer::Reset()
{
	m_pMySentryGun = nullptr;
	m_pMyDispenser = nullptr;
	m_flDistToSentry = FLT_MAX;
	m_flDistToDispenser = FLT_MAX;
	m_iBuildAttempts = 0;
	m_flBuildYaw = 0.0f;
	m_vBuildingSpots.clear();
	m_vFailedSpots.clear();
	m_tCurrentBuildingSpot = {};
	m_tCurrentFocusPoint = {};
	m_eTaskStage = EngineerTaskStageEnum::None;
}

bool CNavBotEngineer::IsEngieMode(CTFPlayer* pLocal)
{
	return Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::AutoEngie &&
		pLocal && pLocal->IsAlive() && pLocal->m_iClass() == TF_CLASS_ENGINEER;
}

bool CNavBotEngineer::Run(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy)
{
	if (!IsEngieMode(pLocal))
	{
		m_eTaskStage = EngineerTaskStageEnum::None;
		return false;
	}

	bool bHasGunslinger = G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger;
	static Timer tBuildingCheckTimer;
	if (tBuildingCheckTimer.Run(10.f))
	{
		if (!m_pMySentryGun || !m_pMyDispenser || (m_tCurrentFocusPoint.flTime != FLT_MAX && m_tCurrentFocusPoint.flTime + 60.f < I::GlobalVars->curtime))
		{
			RefreshBuildingSpots(pLocal, tClosestEnemy, true);

			if (m_vBuildingSpots.size())
			{
				auto tBuildSpot = m_vBuildingSpots.front();

				bool bDestroySentry = m_pMySentryGun && !m_pMySentryGun->m_bPlacing() && m_pMySentryGun->GetAbsOrigin().DistTo(tBuildSpot.m_vPos) >= 3500.f;
				bool bDestroyDispenser = m_pMyDispenser && !m_pMyDispenser->m_bPlacing() && m_pMyDispenser->GetAbsOrigin().DistTo(tBuildSpot.m_vPos) >= 3500.f;
				if (bDestroySentry || bDestroyDispenser)
				{
					I::EngineClient->ClientCmd_Unrestricted("destroy 2");
					I::EngineClient->ClientCmd_Unrestricted("destroy 0");
					m_eTaskStage = EngineerTaskStageEnum::None;
					return true;
				}
			}
		}
	}

	if (m_pMySentryGun && !m_pMySentryGun->m_bPlacing())
	{
		if (bHasGunslinger)
		{

			if (m_flDistToSentry >= 1800.f)
				I::EngineClient->ClientCmd_Unrestricted("destroy 2");

			m_eTaskStage = EngineerTaskStageEnum::None;
			return false;
		}
		else
		{

			if (BuildingNeedsToBeSmacked(m_pMySentryGun))
				return SmackBuilding(pCmd, pLocal, m_pMySentryGun);
			else
			{
				if (m_pMyDispenser && !m_pMyDispenser->m_bPlacing())
				{

					if (BuildingNeedsToBeSmacked(m_pMyDispenser))
						return SmackBuilding(pCmd, pLocal, m_pMyDispenser);

					m_eTaskStage = EngineerTaskStageEnum::None;
					return false;
				}
				else
				{

					return BuildBuilding(pCmd, pLocal, tClosestEnemy, true);
				}
			}
		}
	}

	return BuildBuilding(pCmd, pLocal, tClosestEnemy, false);
}
