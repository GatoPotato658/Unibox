#include "NavBotJobs.h"
#include "../Hazards.h"
#include "../Objectives.h"

static std::pair<CBaseEntity*, float> FindClosestThreatToArea(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CNavArea* pArea)
{
	if (!pLocal || !pWeapon || !pArea)
		return { nullptr, FLT_MAX };

	CBaseEntity* pClosestEnemy = nullptr;
	float flBestDist = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (F::BotUtils.ShouldTarget(pLocal, pWeapon, pEntity->entindex()) != ShouldTargetEnum::Target)
			continue;

		const float flDist = pEntity->GetAbsOrigin().DistTo(pArea->m_vCenter);
		if (flDist >= flBestDist)
			continue;

		flBestDist = flDist;
		pClosestEnemy = pEntity;
	}

	return { pClosestEnemy, flBestDist };
}

bool CNavBotRoam::GetDefendTarget(CTFPlayer* pLocal, Vector& vOut)
{
	const int iEnemyTeam = pLocal->m_iTeamNum() == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	F::NavBotCapture.m_bOverwriteCapture = false;
	if (F::GameObjectiveController.m_bVSH)
		return F::VSHController.GetGatherPoint(pLocal, vOut);

	switch (F::GameObjectiveController.m_eGameMode)
	{
	case TF_GAMETYPE_CP:
		return F::NavBotCapture.GetControlPointGoal(vLocalOrigin, iEnemyTeam, vOut);
	case TF_GAMETYPE_ESCORT:
		if (F::GameObjectiveController.m_bTugOfWar)
			return F::NavBotCapture.GetTugOfWarGoal(pLocal, pLocal->m_iTeamNum(), vOut);
		if (F::GameObjectiveController.m_bPayloadHybrid && F::NavBotCapture.GetControlPointGoal(vLocalOrigin, iEnemyTeam, vOut))
			return true;
		return F::NavBotCapture.GetPayloadGoal(pLocal->GetRefEHandle(), vLocalOrigin, iEnemyTeam, vOut);
	case TF_GAMETYPE_CTF:
		return F::GameObjectiveController.m_bHaarp && F::NavBotCapture.GetCtfGoal(pLocal, pLocal->m_iTeamNum(), iEnemyTeam, vOut);
	default:
		return false;
	}
}

bool CNavBotRoam::RunDefend(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vector& vTarget)
{
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	auto pClosestNav = F::NavEngine.FindClosestNavArea(vTarget);
	if (!pClosestNav)
		return false;

	if (m_pDefendSpotArea && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol
		&& F::NavEngine.IsPathing() && m_pDefendSpotArea->m_vCenter.DistTo(vLocalOrigin) > 250.f)
	{
		m_bDefending = true;
		return true;
	}

	const auto [pClosestEnemy, flBestDist] = FindClosestThreatToArea(pLocal, pWeapon, pClosestNav);

	Vector vVischeckPoint = {};
	const bool bVischeck = pClosestEnemy && flBestDist <= 1000.f;
	if (bVischeck)
	{
		vVischeckPoint = pClosestEnemy->GetAbsOrigin();
		vVischeckPoint.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	}

	std::pair<CNavArea*, int> tHidingSpot;
	if (!NavAreaUtils::FindClosestHidingSpot(pClosestNav, vVischeckPoint, 5, tHidingSpot, bVischeck) || !tHidingSpot.first)
		return false;

	if (tHidingSpot.first->m_vCenter.DistTo(vLocalOrigin) <= 250.f)
	{
		F::NavEngine.CancelPath();
		m_bDefending = true;
		return true;
	}

	if (!F::NavEngine.NavTo(tHidingSpot.first->m_vCenter, PriorityListEnum::Patrol))
		return false;

	m_pDefendSpotArea = tHidingSpot.first;
	m_bDefending = true;
	return true;
}

bool CNavBotRoam::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	auto pMap = F::NavEngine.GetNavMap();
	if (!pMap)
	{
		Reset();
		return false;
	}

	const void* pAreaData = pMap->m_navfile.m_vAreas.data();
	const size_t nAreaCount = pMap->m_navfile.m_vAreas.size();
	if (m_pLastMap != pMap || m_pLastAreaData != pAreaData || m_nLastAreaCount != nAreaCount)
	{
		Reset();
		m_pLastMap = pMap;
		m_pLastAreaData = pAreaData;
		m_nLastAreaCount = nAreaCount;
		m_tConnectedAreasRefresh.Update();
	}

	if (m_tVisitedAreasClear.Run(60.f) || m_vVisitedAreas.size() > 40)
	{
		m_vVisitedAreas.clear();
		m_iConsecutiveFails = 0;
	}

	if (!m_tRoamTimer.Run(0.5f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol && (m_bDefending || m_pCurrentTargetArea);

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Patrol)
		return false;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	Vector vObjectiveAnchor = {};
	bool bHasObjectiveAnchor = false;

	if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DefendObjectives)
	{
		bHasObjectiveAnchor = GetDefendTarget(pLocal, vObjectiveAnchor);
		if (F::NavBotCapture.m_bOverwriteCapture)
		{
			F::NavEngine.CancelPath();
			m_bDefending = true;
			return true;
		}

		if (bHasObjectiveAnchor && RunDefend(pLocal, pWeapon, vObjectiveAnchor))
			return true;
	}

	if (!bHasObjectiveAnchor)
	{
		const auto eMode = F::GameObjectiveController.m_eGameMode;
		bHasObjectiveAnchor = ((eMode == TF_GAMETYPE_PD || eMode == TF_GAMETYPE_RD)
			&& F::GameObjectiveController.GetBattleAnchor(pLocal->m_iTeamNum(), vObjectiveAnchor))
			|| F::VSHController.GetPatrolAnchor(pLocal, vObjectiveAnchor);
	}

	m_bDefending = false;
	if (m_pCurrentTargetArea && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol)
	{
		bool bBlacklisted = false;
		{
			std::lock_guard lock(pMap->m_mutex);
			bBlacklisted = pMap->GetAreaBlock(m_pCurrentTargetArea, I::GlobalVars->tickcount) == CMap::AreaBlock::Stuck;
		}
		const bool bReached = F::NavEngine.GetLocalNavArea() == m_pCurrentTargetArea;
		if (!bBlacklisted && (F::NavEngine.IsPathing()
			|| (!bReached && F::NavEngine.NavTo(m_pCurrentTargetArea->m_vCenter, PriorityListEnum::Patrol))))
			return true;
	}
	m_pCurrentTargetArea = nullptr;

	auto pLocalArea = F::NavEngine.GetLocalNavArea(vLocalOrigin);
	if (!pLocalArea)
		return false;

	if (m_pLastConnectedSeed != pLocalArea || m_sConnectedAreas.empty() || m_tConnectedAreasRefresh.Run(2.f))
	{
		std::vector<CNavArea*> vConnectedAreas;
		pMap->CollectAreasAround(vLocalOrigin, 100000.f, vConnectedAreas);

		m_sConnectedAreas.clear();
		for (auto pArea : vConnectedAreas)
		{
			if (pArea)
				m_sConnectedAreas.insert(pArea);
		}
		if (m_sConnectedAreas.empty())
			m_sConnectedAreas.insert(pLocalArea);

		m_pLastConnectedSeed = pLocalArea;
	}

	struct RoamCandidate_t
	{
		CNavArea* m_pArea = nullptr;
		float m_flDangerCost = 0.f;
		bool m_bSoftBlocked = false;
	};

	std::vector<RoamCandidate_t> vCandidates;
	{
		const int iNowTick = I::GlobalVars->tickcount;
		std::lock_guard lock(pMap->m_mutex);
		for (auto& tArea : pMap->m_navfile.m_vAreas)
		{
			if (!m_sConnectedAreas.contains(&tArea) || NavJobUtils::IsSpawnArea(&tArea))
				continue;

			const auto eBlock = pMap->GetAreaBlock(&tArea, iNowTick);
			if (eBlock == CMap::AreaBlock::Stuck)
				continue;

			const float flDangerCost = F::Hazards.GetCost(&tArea);
			if (std::isfinite(flDangerCost))
				vCandidates.push_back({ &tArea, flDangerCost, eBlock == CMap::AreaBlock::Soft });
		}
	}

	if (vCandidates.empty())
		return false;

	std::vector<NavAreaScore_t> vScoredAreas;
	vScoredAreas.reserve(vCandidates.size());
	const float flLocalToObjective = bHasObjectiveAnchor ? vLocalOrigin.DistTo(vObjectiveAnchor) : 0.f;

	constexpr float flPreferredPatrolDistance = 2200.f;
	constexpr float flNearPenaltyStart = 800.f;
	constexpr float flLongPenaltyStart = 4200.f;
	constexpr float flLongPenaltyCap = 5600.f;

	for (const auto& tCandidate : vCandidates)
	{
		auto pArea = tCandidate.m_pArea;
		const float flDist = pArea->m_vCenter.DistTo(vLocalOrigin);

		float flObjectiveScore = 0.f;
		if (bHasObjectiveAnchor)
		{
			const float flAreaToObjective = pArea->m_vCenter.DistTo(vObjectiveAnchor);
			flObjectiveScore = std::clamp((flLocalToObjective - flAreaToObjective) / 1200.f, -1.f, 1.f) * 900.f;
		}

		float flSafetyPenalty = std::clamp(tCandidate.m_flDangerCost, 0.f, 8000.f) * 0.08f;
		if (tCandidate.m_bSoftBlocked)
			flSafetyPenalty += 450.f;

		const float flDistanceFit = 1.f - std::clamp(std::fabs(flDist - flPreferredPatrolDistance) / flPreferredPatrolDistance, 0.f, 1.f);
		float flDistanceScore = flDistanceFit * 650.f;
		if (flDist < flNearPenaltyStart)
			flDistanceScore -= (1.f - flDist / flNearPenaltyStart) * 450.f;
		if (flDist > flLongPenaltyStart)
			flDistanceScore -= std::clamp((flDist - flLongPenaltyStart) / (flLongPenaltyCap - flLongPenaltyStart), 0.f, 1.f) * 420.f;

		float flVisitedPenalty = 0.f;
		for (auto pVisited : m_vVisitedAreas)
		{
			if (pVisited && pArea->m_vCenter.DistTo(pVisited->m_vCenter) < 750.f)
			{
				flVisitedPenalty = 500.f;
				break;
			}
		}

		vScoredAreas.push_back({ pArea, flObjectiveScore - flSafetyPenalty + flDistanceScore - flVisitedPenalty });
	}

	auto SortByScore = [&]
		{
			std::sort(vScoredAreas.begin(), vScoredAreas.end(), [](const NavAreaScore_t& a, const NavAreaScore_t& b)
				{
					return a.m_flScore > b.m_flScore;
				});
		};
	SortByScore();

	const size_t uPathCostCandidates = std::min<size_t>(vScoredAreas.size(), 16);
	std::vector<CNavArea*> vCostTargets(uPathCostCandidates, nullptr);
	for (size_t i = 0; i < uPathCostCandidates; i++)
		vCostTargets[i] = F::NavEngine.FindClosestNavArea(vScoredAreas[i].m_pArea->m_vCenter, false);

	std::vector<float> vPathCost;
	F::NavEngine.GetPathCostField(pLocalArea, vPathCost, FLT_MAX, &vCostTargets);

	for (size_t i = 0; i < uPathCostCandidates; i++)
	{
		const float flPathCost = vCostTargets[i] ? F::NavEngine.GetFieldCost(vPathCost, vCostTargets[i]) : FLT_MAX;
		if (std::isfinite(flPathCost) && flPathCost < FLT_MAX)
			vScoredAreas[i].m_flScore -= flPathCost * 0.12f;
		else
			vScoredAreas[i].m_flScore -= 1200.f;
	}

	if (uPathCostCandidates > 0)
		SortByScore();

	int iAttempts = 0;
	for (const auto& tAreaScore : vScoredAreas)
	{
		if (iAttempts++ > 40)
			break;

		if (F::NavEngine.NavTo(tAreaScore.m_pArea->m_vCenter, PriorityListEnum::Patrol))
		{
			m_pCurrentTargetArea = tAreaScore.m_pArea;
			m_vVisitedAreas.push_back(tAreaScore.m_pArea);
			m_iConsecutiveFails = 0;
			return true;
		}
	}

	if (++m_iConsecutiveFails >= 3)
	{
		m_vVisitedAreas.clear();
		m_iConsecutiveFails = 0;
	}

	return false;
}

void CNavBotRoam::Reset()
{
	m_pCurrentTargetArea = nullptr;
	m_pDefendSpotArea = nullptr;
	m_pLastConnectedSeed = nullptr;
	m_pLastMap = nullptr;
	m_pLastAreaData = nullptr;
	m_nLastAreaCount = 0;
	m_iConsecutiveFails = 0;
	m_bDefending = false;
	m_vVisitedAreas.clear();
	m_sConnectedAreas.clear();
}
