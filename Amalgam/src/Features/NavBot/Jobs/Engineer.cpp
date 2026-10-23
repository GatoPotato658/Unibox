#include "NavBotJobs.h"
#include "../Objectives.h"

#include <algorithm>

constexpr float kFailedSpotMemory = 60.f;
constexpr float kFailedSpotRadius = 72.f;

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

bool CNavBotEngineer::IsBuildSpotFailed(const Vector& vPos) const
{
	const float flCurTime = I::GlobalVars->curtime;
	return std::any_of(m_vFailedSpots.begin(), m_vFailedSpots.end(), [&](const FailedSpot_t& tFailed)
		{
			return tFailed.m_flExpire > flCurTime && tFailed.m_vPos.DistTo(vPos) < kFailedSpotRadius;
		});
}

void CNavBotEngineer::MarkSpotFailed(const Vector& vPos)
{
	m_vFailedSpots.push_back({ vPos, I::GlobalVars->curtime + kFailedSpotMemory });
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

bool CNavBotEngineer::NavToBuildingSpot()
{
	if (!m_tWaitUntilPath.Run(0.3f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer;

	if (m_vBuildingSpots.empty())
		return false;

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
		return true;

	for (auto& tSpot : m_vBuildingSpots)
	{
		if (IsBuildSpotFailed(tSpot.m_vPos))
			continue;
		if (m_tCurrentBuildingSpot.m_flCost != FLT_MAX && tSpot.m_vPos.DistTo(m_tCurrentBuildingSpot.m_vPos) < 8.f)
			continue;

		if (F::NavEngine.NavTo(tSpot.m_vPos, PriorityListEnum::Engineer))
		{
			m_tCurrentBuildingSpot = tSpot;
			m_flBuildYaw = 0.f;
			return true;
		}
	}
	return false;
}

bool CNavBotEngineer::BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bDispenser)
{
	m_eTaskStage = bDispenser ? EngineerTaskStageEnum::BuildDispenser : EngineerTaskStageEnum::BuildSentry;

	if (m_flBuildYaw >= 360.f || m_iBuildAttempts > 20)
	{
		if (m_tCurrentBuildingSpot.m_flCost != FLT_MAX)
			MarkSpotFailed(m_tCurrentBuildingSpot.m_vPos);
		m_tCurrentBuildingSpot = {};
		m_iBuildAttempts = 0;
		m_flBuildYaw = 0.f;
		return false;
	}

	const int iRequiredMetal = (bDispenser || G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger) ? 100 : 130;
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
				if (IsBuildSpotFailed(vPos) || !CanBuildAtPosition(pLocal, vPos))
					continue;
				m_tCurrentBuildingSpot = { vSentry.DistTo(vPos), vPos };
				break;
			}
		}
	}

	const bool bHasSpot = m_tCurrentBuildingSpot.m_flCost != FLT_MAX;
	const float flPlaceDist = bDispenser ? 140.f : 200.f;
	if (bHasSpot && m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= flPlaceDist)
	{
		if (tClosestEnemy.m_flDist < 500.f && tClosestEnemy.m_pPlayer && tClosestEnemy.m_pPlayer->IsAlive() && !pLocal->m_bCarryingObject())
			return false;

		pCmd->viewangles.x = 20.f;
		pCmd->viewangles.y = m_flBuildYaw;
		I::EngineClient->SetViewAngles(pCmd->viewangles);

		if (m_tBuildRotation.Run(0.3f))
			m_flBuildYaw += 45.f;

		if (m_tBuildAttempt.Run(0.3f))
			m_iBuildAttempts++;

		if (!pLocal->m_bCarryingObject())
		{
			if (m_tBuildCommand.Run(0.5f))
				I::EngineClient->ClientCmd_Unrestricted(std::format("build {}", bDispenser ? 0 : 2).c_str());
		}

		pCmd->buttons |= IN_ATTACK;
		pCmd->forwardmove = 20.f;
		if (pCmd->sidemove == 0.f)
			pCmd->sidemove = 1.f;
		return true;
	}

	if (bDispenser && bHasSpot)
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer && F::NavEngine.IsPathing())
			return true;

		if (F::NavEngine.NavTo(m_tCurrentBuildingSpot.m_vPos, PriorityListEnum::Engineer))
			return true;

		if (F::NavEngine.IsPriorityAllowed(PriorityListEnum::Engineer))
		{
			MarkSpotFailed(m_tCurrentBuildingSpot.m_vPos);
			m_tCurrentBuildingSpot = {};
		}
		return false;
	}

	return NavToBuildingSpot();
}

bool CNavBotEngineer::SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	m_iBuildAttempts = 0;
	if (!pLocal->m_iMetalCount())
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
			F::NavEngine.CancelPath();
		return F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced);
	}

	m_eTaskStage = pBuilding->GetClassID() == ETFClassID::CObjectDispenser ? EngineerTaskStageEnum::SmackDispenser : EngineerTaskStageEnum::SmackSentry;

	if (pBuilding->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) <= 100.f && F::BotUtils.m_iCurrentSlot == SLOT_MELEE)
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
			F::NavEngine.CancelPath();
		F::NavEngine.m_eCurrentPriority = PriorityListEnum::Engineer;

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
	const int iLocalIdx = pLocal->entindex();
	const int iLocalTeam = pLocal->m_iTeamNum();
	const int iEnemyTeam = iLocalTeam == TF_TEAM_RED ? TF_TEAM_BLUE : TF_TEAM_RED;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

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
	if (tFocus.m_bBack && vTeammates.size() > 1)
	{
		std::vector<std::pair<CTFPlayer*, float>> vTeammatesSorted;
		for (auto pTeammate : vTeammates)
		{
			const int iTeammateIdx = pTeammate->entindex();
			if (iTeammateIdx == iLocalIdx)
				continue;

			Vector vOrigin;
			if (!F::BotUtils.GetDormantOrigin(iTeammateIdx, &vOrigin))
				continue;

			const float flDist = vOrigin.DistTo(tFocus.m_vPos);
			if (flDist > 1200.f)
				continue;

			vTeammatesSorted.emplace_back(pTeammate->As<CTFPlayer>(), flDist);
		}

		if (vTeammatesSorted.empty())
		{
			tOut = tFocus;
			return true;
		}

		std::sort(vTeammatesSorted.begin(), vTeammatesSorted.end(), [](const std::pair<CTFPlayer*, float>& a, const std::pair<CTFPlayer*, float>& b) -> bool
			{
				return a.second < b.second;
			});

		std::vector<float> vFocusCost;
		const bool bHasFocusCost = F::NavEngine.GetPathCostField(tFocus.m_pArea, vFocusCost, 4000.f);

		CNavArea* pNewFocusArea = nullptr;
		Vector vFocus;
		int iTeammatesOnFocusPoint = 0;
		for (auto pTeammate : vTeammatesSorted | std::views::keys)
		{
			Vector vNewFocus = vFocus + pTeammate->GetAbsOrigin();

			auto pTempFocusArea = F::NavEngine.FindClosestNavArea(vNewFocus / static_cast<float>(iTeammatesOnFocusPoint + 1), false);
			if (!pTempFocusArea || (bHasFocusCost && F::NavEngine.GetFieldCost(vFocusCost, pTempFocusArea) > 4000.f))
				continue;

			vFocus = vNewFocus;
			pNewFocusArea = pTempFocusArea;
			iTeammatesOnFocusPoint++;
		}
		if (iTeammatesOnFocusPoint)
		{
			tFocus.m_pArea = pNewFocusArea;
			tFocus.m_vPos = vFocus / static_cast<float>(iTeammatesOnFocusPoint);
		}
	}

	tOut = tFocus;
	return true;
}

void CNavBotEngineer::RefreshBuildingSpots(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bForce)
{
	if (!IsEngieMode(pLocal))
		return;

	const bool bHasGunslinger = G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger;
	if (!bForce && !m_tRefreshBuildingSpots.Run(bHasGunslinger ? 1.f : 5.f))
		return;

	m_vBuildingSpots.clear();
	std::erase_if(m_vFailedSpots, [](const FailedSpot_t& tFailed) { return tFailed.m_flExpire <= I::GlobalVars->curtime; });

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

	auto pNavMap = F::NavEngine.GetNavMap();
	if (!pNavMap)
		return;

	std::vector<float> vFocusCost;
	const bool bHasFocusCost = tFocus.m_pArea && F::NavEngine.GetPathCostField(tFocus.m_pArea, vFocusCost, 4000.f);

	struct BuildCandidate_t
	{
		Vector m_vPos = {};
		float m_flCost = 0.f;
	};
	std::vector<BuildCandidate_t> vCandidates;

	std::vector<CNavArea*> vAreas;
	pNavMap->CollectAreasAround(tFocus.m_vPos, 2000.f, vAreas);
	for (CNavArea* pArea : vAreas)
	{
		if (!pArea)
			continue;

		if (pArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT))
			continue;

		if (pArea->m_vCenter.DistTo(tFocus.m_vPos) > 2000.f)
			continue;

		if (pArea->IsBlocked(pLocal->m_iTeamNum()))
			continue;

		auto AddSpot = [&](const Vector& vPos, float flBonus = 0.f)
			{
				if (IsBuildSpotFailed(vPos))
					return;

				if (tFocus.m_pArea && (!bHasFocusCost || F::NavEngine.GetFieldCost(vFocusCost, pArea) > 4000.f))
					return;

				const float flDistToFocus = vPos.DistTo(tFocus.m_vPos);
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

				if (pArea->m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT)
					flCost -= 200.f;
				if (pArea->m_iTFAttributeFlags & TF_NAV_CONTROL_POINT)
					flCost -= 150.f;

				vCandidates.push_back({ vPos, flCost });
			};

		const bool bSentrySpot = (pArea->m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT) != 0;
		const bool bNearObjective = pArea->m_vCenter.DistTo(tFocus.m_vPos) < 1400.f;
		if (bSentrySpot || bNearObjective)
			AddSpot(pArea->m_vCenter, bSentrySpot ? 250.f : 0.f);

		for (auto& tHidingSpot : pArea->m_vHidingSpots)
		{
			if (tHidingSpot.HasGoodCover())
				AddSpot(tHidingSpot.m_vPos, 80.f);
		}
	}

	std::sort(vCandidates.begin(), vCandidates.end(),
		[](const BuildCandidate_t& a, const BuildCandidate_t& b) -> bool
		{
			return a.m_flCost < b.m_flCost;
		});

	const size_t nTraceCandidates = std::min<size_t>(vCandidates.size(), 96);
	for (size_t i = 0; i < nTraceCandidates && m_vBuildingSpots.size() < 48; ++i)
	{
		const auto& tCandidate = vCandidates[i];
		if (CanBuildAtPosition(pLocal, tCandidate.m_vPos))
			m_vBuildingSpots.emplace_back(tCandidate.m_flCost, tCandidate.m_vPos);
	}
}

void CNavBotEngineer::Render()
{
	if (!Vars::Misc::Movement::NavEngine::Draw.Value)
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive() || pLocal->m_iClass() != TF_CLASS_ENGINEER)
		return;

	if (m_vBuildingSpots.empty())
		return;

	for (auto& tSpot : m_vBuildingSpots)
	{
		const bool bIsCurrent = tSpot.m_vPos == m_tCurrentBuildingSpot.m_vPos;
		const Color_t tColor = bIsCurrent ? Color_t(0, 255, 0, 255) : Color_t(255, 255, 255, 100);

		H::Draw.RenderWireframeBox(tSpot.m_vPos, Vector(-30, -30, 0), Vector(30, 30, 66), Vector(0, 0, 0), tColor, false);
		if (bIsCurrent)
			H::Draw.RenderBox(tSpot.m_vPos, Vector(-30, -30, 0), Vector(30, 30, 66), Vector(0, 0, 0), Color_t(0, 255, 0, 50), false);
	}
	H::Draw.RenderWireframeSphere(m_tCurrentFocusPoint.m_vPos, 10.f, 36, 36, Color_t(255, 255, 0, 255));
}

void CNavBotEngineer::RefreshLocalBuildings(CTFPlayer* pLocal)
{
	if (!IsEngieMode(pLocal))
	{
		m_pMySentryGun = nullptr;
		m_pMyDispenser = nullptr;
		m_flDistToSentry = FLT_MAX;
		m_flDistToDispenser = FLT_MAX;
		m_eTaskStage = EngineerTaskStageEnum::None;
		return;
	}

	auto pSentry = pLocal->GetObjectOfType(OBJ_SENTRYGUN);
	auto pDispenser = pLocal->GetObjectOfType(OBJ_DISPENSER);
	m_pMySentryGun = pSentry ? pSentry->As<CObjectSentrygun>() : nullptr;
	m_pMyDispenser = pDispenser ? pDispenser->As<CObjectDispenser>() : nullptr;
	m_flDistToSentry = m_pMySentryGun ? m_pMySentryGun->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) : FLT_MAX;
	m_flDistToDispenser = m_pMyDispenser ? m_pMyDispenser->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) : FLT_MAX;
}

void CNavBotEngineer::Reset()
{
	m_pMySentryGun = nullptr;
	m_pMyDispenser = nullptr;
	m_flDistToSentry = FLT_MAX;
	m_flDistToDispenser = FLT_MAX;
	m_iBuildAttempts = 0;
	m_flBuildYaw = 0.f;
	m_vBuildingSpots.clear();
	m_vFailedSpots.clear();
	m_tCurrentBuildingSpot = {};
	m_tCurrentFocusPoint = {};
	m_eTaskStage = EngineerTaskStageEnum::None;
	m_tWaitUntilPath = Timer();
	m_tBuildRotation = Timer();
	m_tBuildAttempt = Timer();
	m_tBuildCommand = Timer();
	m_tRefreshBuildingSpots = Timer();
	m_tBuildingCheck = Timer();
}

bool CNavBotEngineer::IsEngieMode(CTFPlayer* pLocal)
{
	return NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::AutoEngie) &&
		pLocal && pLocal->IsAlive() && pLocal->m_iClass() == TF_CLASS_ENGINEER;
}

bool CNavBotEngineer::Run(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy)
{
	if (!IsEngieMode(pLocal))
	{
		m_eTaskStage = EngineerTaskStageEnum::None;
		return false;
	}

	const bool bHasGunslinger = G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger;
	if (m_tBuildingCheck.Run(10.f))
	{
		if (!m_pMySentryGun || !m_pMyDispenser || (m_tCurrentFocusPoint.m_flTime != FLT_MAX && m_tCurrentFocusPoint.m_flTime + 60.f < I::GlobalVars->curtime))
		{
			RefreshBuildingSpots(pLocal, tClosestEnemy, true);

			if (!m_vBuildingSpots.empty())
			{
				const Vector vBestSpot = m_vBuildingSpots.front().m_vPos;

				const bool bDestroySentry = m_pMySentryGun && !m_pMySentryGun->m_bPlacing() && m_pMySentryGun->GetAbsOrigin().DistTo(vBestSpot) >= 3500.f;
				const bool bDestroyDispenser = m_pMyDispenser && !m_pMyDispenser->m_bPlacing() && m_pMyDispenser->GetAbsOrigin().DistTo(vBestSpot) >= 3500.f;
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

		if (BuildingNeedsToBeSmacked(m_pMySentryGun))
			return SmackBuilding(pCmd, pLocal, m_pMySentryGun);

		if (m_pMyDispenser && !m_pMyDispenser->m_bPlacing())
		{
			if (BuildingNeedsToBeSmacked(m_pMyDispenser))
				return SmackBuilding(pCmd, pLocal, m_pMyDispenser);

			m_eTaskStage = EngineerTaskStageEnum::None;
			return false;
		}

		return BuildBuilding(pCmd, pLocal, tClosestEnemy, true);
	}

	return BuildBuilding(pCmd, pLocal, tClosestEnemy, false);
}
