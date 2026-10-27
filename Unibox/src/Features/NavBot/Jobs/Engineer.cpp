#include "NavBotJobs.h"
#include "../Objectives.h"

#include <algorithm>

constexpr float kFailedSpotMemory = 60.f;
constexpr float kFailedSpotRadius = 72.f;

constexpr float kHelpScanInterval = 0.25f;
constexpr float kHelpMaxDistance = 1200.f;
constexpr float kHelpEnemyLocalRange = 500.f;
constexpr float kHelpEnemyBuildingRange = 600.f;
constexpr float kHelpTravelSpeed = 300.f;
constexpr float kHelpTravelMargin = 0.5f;
constexpr float kHelpReachTimeout = 20.f;
constexpr float kHelpProgressTimeout = 6.f;
constexpr float kHelpFailMemory = 30.f;
constexpr float kHelpStallMemory = 20.f;
constexpr float kWrenchReach = 64.f;
constexpr float kWrenchHull = 16.f;
constexpr float kSentryBuildTime = 10.f;
constexpr float kDispenserBuildTime = 20.f;
constexpr float kTeleporterBuildTime = 20.f;

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

static float GetBuildTime(int iObjectType)
{
	switch (iObjectType)
	{
	case OBJ_DISPENSER: return kDispenserBuildTime;
	case OBJ_TELEPORTER: return kTeleporterBuildTime;
	default: return kSentryBuildTime;
	}
}

static bool CanWrenchBuilding(CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	const Vector vStart = pLocal->GetShootPos();
	const Vector vEnd = vStart + (pBuilding->GetCenter() - vStart).Normalized() * kWrenchReach;

	CGameTrace trace;
	CTraceFilterCollideable filter(pLocal);
	filter.m_iPlayer = PLAYER_NONE;

	SDK::Trace(vStart, vEnd, MASK_SOLID, &filter, &trace);
	if (trace.DidHit())
		return trace.m_pEnt == pBuilding;

	SDK::TraceHull(vStart, vEnd, Vector(-kWrenchHull, -kWrenchHull, -kWrenchHull), Vector(kWrenchHull, kWrenchHull, kWrenchHull), MASK_SOLID, &filter, &trace);
	return trace.DidHit() && trace.m_pEnt == pBuilding;
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

bool CNavBotEngineer::SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding, bool bHelp)
{
	m_iBuildAttempts = 0;
	if (!bHelp && !pLocal->m_iMetalCount())
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
			F::NavEngine.CancelPath();
		return F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced);
	}

	m_eTaskStage = bHelp ? EngineerTaskStageEnum::HelpBuilding
		: pBuilding->GetClassID() == ETFClassID::CObjectDispenser ? EngineerTaskStageEnum::SmackDispenser : EngineerTaskStageEnum::SmackSentry;

	const bool bInRange = bHelp ? CanWrenchBuilding(pLocal, pBuilding) : pBuilding->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) <= 100.f;
	if (bInRange && F::BotUtils.m_iCurrentSlot == SLOT_MELEE)
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
			F::NavEngine.CancelPath();
		F::NavEngine.m_eCurrentPriority = PriorityListEnum::Engineer;

		if (bHelp && !m_bHelpReached)
		{
			m_bHelpReached = true;
			m_flHelpProgress = pBuilding->m_flPercentageConstructed();
			m_flHelpProgressTime = I::GlobalVars->curtime;
		}

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

bool CNavBotEngineer::IsHelpTargetFailed(int iEntIdx) const
{
	const float flCurTime = I::GlobalVars->curtime;
	return std::any_of(m_vFailedHelpTargets.begin(), m_vFailedHelpTargets.end(), [&](const FailedHelp_t& tFailed)
		{
			return tFailed.m_iEntIdx == iEntIdx && tFailed.m_flExpire > flCurTime;
		});
}

void CNavBotEngineer::MarkHelpTargetFailed(int iEntIdx, float flDuration)
{
	m_vFailedHelpTargets.push_back({ iEntIdx, I::GlobalVars->curtime + flDuration });
}

bool CNavBotEngineer::CanHelpFriendlies(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy)
{
	if (!IsEngieMode(pLocal) || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::HelpFriendlyEngineers))
		return false;

	if (G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger || pLocal->m_bCarryingObject())
		return false;

	if (!Vars::Aimbot::AutoEngie::AutoRepair.Value && !Vars::Aimbot::AutoEngie::AutoUpgrade.Value)
		return false;

	if (!m_pMySentryGun || m_pMySentryGun->m_bPlacing() || BuildingNeedsToBeSmacked(m_pMySentryGun)
		|| !m_pMyDispenser || m_pMyDispenser->m_bPlacing() || BuildingNeedsToBeSmacked(m_pMyDispenser))
		return false;

	return !(tClosestEnemy.m_pPlayer && !tClosestEnemy.m_pPlayer->IsDormant() && tClosestEnemy.m_pPlayer->IsAlive()
		&& tClosestEnemy.m_flDist < kHelpEnemyLocalRange);
}

bool CNavBotEngineer::IsHelpCandidate(CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	if (!pBuilding || pBuilding->IsDormant() || pBuilding->m_iHealth() <= 0)
		return false;

	if (pBuilding->m_hBuilder().GetEntryIndex() == pLocal->entindex())
		return false;

	if (!pBuilding->m_bBuilding() || pBuilding->m_bPlacing() || pBuilding->m_bCarried())
		return false;

	if (IsHelpTargetFailed(pBuilding->entindex()))
		return false;

	const Vector vOrigin = pBuilding->GetAbsOrigin();
	const float flDist = vOrigin.DistTo(pLocal->GetAbsOrigin());
	if (flDist > kHelpMaxDistance)
		return false;

	const float flRemaining = (1.f - pBuilding->m_flPercentageConstructed()) * GetBuildTime(pBuilding->m_iObjectType());
	if (flRemaining <= flDist / kHelpTravelSpeed + kHelpTravelMargin)
		return false;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pEnemy = pEntity->As<CTFPlayer>();
		if (!pEnemy->IsDormant() && pEnemy->IsAlive() && pEnemy->GetAbsOrigin().DistTo(vOrigin) < kHelpEnemyBuildingRange)
			return false;
	}

	return true;
}

CBaseObject* CNavBotEngineer::GetHelpBuilding(CTFPlayer* pLocal)
{
	if (m_iHelpTarget <= 0)
		return nullptr;

	auto pClientEntity = I::ClientEntityList->GetClientEntity(m_iHelpTarget);
	auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
	if (!pEntity || !pEntity->IsBuilding() || pEntity->m_iTeamNum() != pLocal->m_iTeamNum())
		return nullptr;

	auto pBuilding = pEntity->As<CBaseObject>();
	return IsHelpCandidate(pLocal, pBuilding) ? pBuilding : nullptr;
}

void CNavBotEngineer::RefreshHelpTarget(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy)
{
	if (!CanHelpFriendlies(pLocal, tClosestEnemy))
	{
		ReleaseHelp();
		return;
	}

	const float flCurTime = I::GlobalVars->curtime;
	if (m_iHelpTarget != -1)
	{
		if (auto pBuilding = GetHelpBuilding(pLocal))
		{
			m_flDistToHelpTarget = pBuilding->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin());
			if (m_eTaskStage != EngineerTaskStageEnum::HelpBuilding)
				m_flHelpAcquireTime = m_flHelpProgressTime = flCurTime;
			return;
		}

		ReleaseHelp();
	}

	if (!m_tHelpScan.Run(kHelpScanInterval))
		return;

	std::erase_if(m_vFailedHelpTargets, [flCurTime](const FailedHelp_t& tFailed) { return tFailed.m_flExpire <= flCurTime; });

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	CBaseObject* pBest = nullptr;
	float flBestDist = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (!IsHelpCandidate(pLocal, pBuilding))
			continue;

		const float flDist = pBuilding->GetAbsOrigin().DistTo(vLocalOrigin);
		if (flDist >= flBestDist)
			continue;

		flBestDist = flDist;
		pBest = pBuilding;
	}

	CNavArea* pLocalArea = F::NavEngine.GetLocalNavArea();
	if (!pBest || !pLocalArea)
		return;

	if (F::NavEngine.GetPathCost(pLocalArea, F::NavEngine.FindClosestNavArea(pBest->GetAbsOrigin(), false)) == FLT_MAX)
	{
		MarkHelpTargetFailed(pBest->entindex(), kHelpFailMemory);
		return;
	}

	m_iHelpTarget = pBest->entindex();
	m_flDistToHelpTarget = flBestDist;
	m_flHelpAcquireTime = m_flHelpProgressTime = flCurTime;
	m_flHelpProgress = pBest->m_flPercentageConstructed();
	m_bHelpReached = false;
}

void CNavBotEngineer::ReleaseHelp()
{
	if (m_bHelping && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
		F::NavEngine.CancelPath();

	if (m_eTaskStage == EngineerTaskStageEnum::HelpBuilding)
		m_eTaskStage = EngineerTaskStageEnum::None;

	m_iHelpTarget = -1;
	m_bHelping = false;
	m_bHelpReached = false;
	m_flDistToHelpTarget = FLT_MAX;
}

bool CNavBotEngineer::HelpFriendlyBuilding(CUserCmd* pCmd, CTFPlayer* pLocal)
{
	auto pBuilding = GetHelpBuilding(pLocal);
	if (!pBuilding)
	{
		ReleaseHelp();
		m_eTaskStage = EngineerTaskStageEnum::None;
		return false;
	}

	const float flCurTime = I::GlobalVars->curtime;
	const float flProgress = pBuilding->m_flPercentageConstructed();
	if (flProgress > m_flHelpProgress)
	{
		m_flHelpProgress = flProgress;
		m_flHelpProgressTime = flCurTime;
	}

	const bool bStalled = m_bHelpReached ? flCurTime - m_flHelpProgressTime > kHelpProgressTimeout : flCurTime - m_flHelpAcquireTime > kHelpReachTimeout;
	if (bStalled)
	{
		MarkHelpTargetFailed(m_iHelpTarget, m_bHelpReached ? kHelpStallMemory : kHelpFailMemory);
		ReleaseHelp();
		return false;
	}

	m_bHelping = true;
	if (SmackBuilding(pCmd, pLocal, pBuilding, true))
		return true;

	if (F::NavEngine.IsPriorityAllowed(PriorityListEnum::Engineer))
		MarkHelpTargetFailed(m_iHelpTarget, kHelpFailMemory);
	ReleaseHelp();
	return false;
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

void CNavBotEngineer::RefreshLocalBuildings(CTFPlayer* pLocal)
{
	if (!IsEngieMode(pLocal))
	{
		m_pMySentryGun = nullptr;
		m_pMyDispenser = nullptr;
		m_flDistToSentry = FLT_MAX;
		m_flDistToDispenser = FLT_MAX;
		m_eTaskStage = EngineerTaskStageEnum::None;
		ReleaseHelp();
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
	m_flDistToHelpTarget = FLT_MAX;
	m_iBuildAttempts = 0;
	m_flBuildYaw = 0.f;
	m_vBuildingSpots.clear();
	m_vFailedSpots.clear();
	m_vFailedHelpTargets.clear();
	m_tCurrentBuildingSpot = {};
	m_tCurrentFocusPoint = {};
	m_iHelpTarget = -1;
	m_bHelping = false;
	m_bHelpReached = false;
	m_flHelpAcquireTime = 0.f;
	m_flHelpProgress = 0.f;
	m_flHelpProgressTime = 0.f;
	m_eTaskStage = EngineerTaskStageEnum::None;
	m_tWaitUntilPath = Timer();
	m_tBuildRotation = Timer();
	m_tBuildAttempt = Timer();
	m_tBuildCommand = Timer();
	m_tRefreshBuildingSpots = Timer();
	m_tBuildingCheck = Timer();
	m_tHelpScan = Timer();
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

			return HelpFriendlyBuilding(pCmd, pLocal);
		}

		return BuildBuilding(pCmd, pLocal, tClosestEnemy, true);
	}

	return BuildBuilding(pCmd, pLocal, tClosestEnemy, false);
}
