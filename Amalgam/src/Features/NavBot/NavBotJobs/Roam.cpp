#include "NavBotJobs.h"
#include "../NavAreaUtils.h"
#include "../Hazards/Hazards.h"
#include "../NavEngine/Controllers/Controller.h"

static std::pair<CBaseEntity*, float> FindClosestThreatToArea(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CNavArea* pArea)
{
	if (!pLocal || !pWeapon || !pArea)
		return { nullptr, FLT_MAX };

	CBaseEntity* pClosestEnemy = nullptr;
	float flBestDist = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (!F::BotUtils.ShouldTarget(pLocal, pWeapon, pEntity->entindex()))
			continue;

		const float flDist = pEntity->GetAbsOrigin().DistTo(pArea->m_vCenter);
		if (flDist >= flBestDist)
			continue;

		flBestDist = flDist;
		pClosestEnemy = pEntity;
	}

	return { pClosestEnemy, flBestDist };
}

bool CNavBotRoam::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tRoamTimer;
	static Timer tVisitedAreasClearTimer;
	static Timer tConnectedAreasRefreshTimer;

	auto pMap = F::NavEngine.GetNavMap();
	if (!pMap)
	{
		Reset();
		return false;
	}

	if (m_pLastMap != pMap)
	{
		Reset();
		m_pLastMap = pMap;
		tConnectedAreasRefreshTimer.Update();
	}

	// Clear visited areas if they get too large or after some time
	if (tVisitedAreasClearTimer.Run(60.f) || m_vVisitedAreas.size() > 40)
	{
		m_vVisitedAreas.clear();
		m_iConsecutiveFails = 0;
	}

	// Keep the current roam objective alive between expensive refreshes.
	if (!tRoamTimer.Run(0.5f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol && (m_bDefending || m_pCurrentTargetArea);

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Patrol)
		return false;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	Vector vObjectiveAnchor = {};
	bool bHasObjectiveAnchor = false;

	// Defend our objective if possible
	if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DefendObjectives)
	{
		int iEnemyTeam = pLocal->m_iTeamNum() == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;

		Vector vTarget;
		const auto vLocalOrigin = pLocal->GetAbsOrigin();
		bool bGotTarget = false;

		switch (F::GameObjectiveController.m_eGameMode)
		{
		case TF_GAMETYPE_CP:
			bGotTarget = F::NavBotCapture.GetControlPointGoal(vLocalOrigin, iEnemyTeam, vTarget);
			break;
		case TF_GAMETYPE_ESCORT:
			bGotTarget = F::NavBotCapture.GetPayloadGoal(pLocal->GetRefEHandle(), vLocalOrigin, iEnemyTeam, vTarget);
			break;
		case TF_GAMETYPE_CTF:
			if (F::GameObjectiveController.m_bHaarp)
				bGotTarget = F::NavBotCapture.GetCtfGoal(pLocal, pLocal->m_iTeamNum(), iEnemyTeam, vTarget);
			break;
		default:
			break;
		}
		if (bGotTarget)
		{
			vObjectiveAnchor = vTarget;
			bHasObjectiveAnchor = true;
		}

		if (bGotTarget || F::NavBotCapture.m_bOverwriteCapture)
		{
			if (F::NavBotCapture.m_bOverwriteCapture)
			{
				F::NavEngine.CancelPath();
				m_bDefending = true;
				return true;
			}

			if (auto pClosestNav = F::NavEngine.FindClosestNavArea(vTarget))
			{
				const auto [pClosestEnemy, flBestDist] = FindClosestThreatToArea(pLocal, pWeapon, pClosestNav);

				Vector vVischeckPoint;
				bool bVischeck = pClosestEnemy && flBestDist <= 1000.f;
				if (bVischeck)
				{
					vVischeckPoint = pClosestEnemy->GetAbsOrigin();
					vVischeckPoint.z += PLAYER_CROUCHED_JUMP_HEIGHT;
				}

				std::pair<CNavArea*, int> tHidingSpot;
				if (NavAreaUtils::FindClosestHidingSpot(pClosestNav, vVischeckPoint, 5, tHidingSpot, bVischeck))
				{
					if (tHidingSpot.first && tHidingSpot.first->m_vCenter.DistTo(vLocalOrigin) <= 250.f)
					{
						F::NavEngine.CancelPath();
						m_bDefending = true;
						return true;
					}
					if (F::NavEngine.NavTo(tHidingSpot.first->m_vCenter, PriorityListEnum::Patrol, true, !F::NavEngine.IsPathing()))
					{
						m_bDefending = true;
						return true;
					}
				}
			}
		}
	}
	m_bDefending = false;
	if (m_pCurrentTargetArea && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol)
	{
		bool bCanKeepTarget = F::NavEngine.IsPathing() && m_pCurrentTargetArea->m_vCenter.DistTo(vLocalOrigin) <= 4200.f;
		if (pMap)
		{
			auto tAreaKey = std::pair<CNavArea*, CNavArea*>(m_pCurrentTargetArea, m_pCurrentTargetArea);
			auto it = pMap->m_mVischeckCache.find(tAreaKey);
			if (it != pMap->m_mVischeckCache.end() && !it->second.m_bPassable && (it->second.m_iExpireTick == 0 || it->second.m_iExpireTick > I::GlobalVars->tickcount) && it->second.m_bStuckBlacklist)
				bCanKeepTarget = false;
		}

		if (bCanKeepTarget)
			return true;

		m_pCurrentTargetArea = nullptr;
	}

	// Reset current target if we are not pathing or it's invalid
	m_pCurrentTargetArea = nullptr;

	struct RoamCandidate_t
	{
		CNavArea* m_pArea = nullptr;
		float m_flBlacklistPenalty = 0.f;
		float m_flDangerCost = 0.f;
		bool m_bSoftBlocked = false;
	};

	std::vector<RoamCandidate_t> vCandidates;
	auto pLocalArea = F::NavEngine.GetLocalNavArea(vLocalOrigin);
	if (!pLocalArea)
		return false;

	if (m_pLastConnectedSeed != pLocalArea || m_sConnectedAreas.empty() || tConnectedAreasRefreshTimer.Run(2.f))
	{
		std::vector<CNavArea*> vConnectedAreas;
		if (pMap)
			pMap->CollectAreasAround(vLocalOrigin, 100000.f, vConnectedAreas);

		m_sConnectedAreas.clear();
		for (auto pArea : vConnectedAreas)
			if (pArea)
				m_sConnectedAreas.insert(pArea);

		if (m_sConnectedAreas.empty())
			m_sConnectedAreas.insert(pLocalArea);

		m_pLastConnectedSeed = pLocalArea;
	}

	// Get all nav areas
	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		if (!m_sConnectedAreas.contains(&tArea))
			continue;

		float flBlacklistPenalty = 0.f;
		if (pMap)
		{
			auto itBlacklist = F::NavEngine.GetFreeBlacklist()->find(&tArea);
			if (itBlacklist != F::NavEngine.GetFreeBlacklist()->end())
			{
				flBlacklistPenalty = pMap->GetBlacklistPenalty(itBlacklist->second);
				if (!std::isfinite(flBlacklistPenalty) || flBlacklistPenalty >= 4000.f)
					continue;
			}
		}

		bool bSoftBlocked = false;
		if (pMap)
		{
			auto tAreaKey = std::pair<CNavArea*, CNavArea*>(&tArea, &tArea);
			auto it = pMap->m_mVischeckCache.find(tAreaKey);
			if (it != pMap->m_mVischeckCache.end() && !it->second.m_bPassable && (it->second.m_iExpireTick == 0 || it->second.m_iExpireTick > I::GlobalVars->tickcount))
			{
				if (it->second.m_bStuckBlacklist)
					continue;

				bSoftBlocked = true;
			}
		}

		// Dont run in spawn bitch
		if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED))
			continue;

		RoamCandidate_t tCandidate{};
		tCandidate.m_pArea = &tArea;
		tCandidate.m_flBlacklistPenalty = flBlacklistPenalty;
		tCandidate.m_flDangerCost = F::Hazards.GetCost(&tArea);
		tCandidate.m_bSoftBlocked = bSoftBlocked;
		vCandidates.push_back(tCandidate);
	}

	// No valid areas found
	if (vCandidates.empty())
		return false;

	std::vector<NavAreaScore_t> vScoredAreas;
	vScoredAreas.reserve(vCandidates.size());
	const float flLocalToObjective = bHasObjectiveAnchor ? vLocalOrigin.DistTo(vObjectiveAnchor) : 0.f;

	for (const auto& tCandidate : vCandidates)
	{
		auto pArea = tCandidate.m_pArea;
		if (!pArea)
			continue;

		const float flDist = pArea->m_vCenter.DistTo(vLocalOrigin);

		float flObjectiveScore = 0.f;
		if (bHasObjectiveAnchor)
		{
			const float flAreaToObjective = pArea->m_vCenter.DistTo(vObjectiveAnchor);
			const float flProgress = std::clamp((flLocalToObjective - flAreaToObjective) / 1200.f, -1.f, 1.f);
			flObjectiveScore = flProgress * 900.f;
		}

		float flSafetyPenalty = std::clamp(tCandidate.m_flDangerCost, 0.f, 8000.f) * 0.08f;
		flSafetyPenalty += std::min(tCandidate.m_flBlacklistPenalty, 2500.f) * 0.45f;
		if (tCandidate.m_bSoftBlocked)
			flSafetyPenalty += 450.f;

		constexpr float flPreferredPatrolDistance = 2200.f;
		constexpr float flNearPenaltyStart = 800.f;
		constexpr float flLongPenaltyStart = 4200.f;
		constexpr float flLongPenaltyCap = 5600.f;

		const float flDistanceDelta = std::fabs(flDist - flPreferredPatrolDistance);
		const float flDistanceFit = 1.f - std::clamp(flDistanceDelta / flPreferredPatrolDistance, 0.f, 1.f);
		float flDistanceScore = flDistanceFit * 650.f;

		if (flDist < flNearPenaltyStart)
			flDistanceScore -= (1.f - (flDist / flNearPenaltyStart)) * 450.f;

		if (flDist > flLongPenaltyStart)
		{
			const float flLongFraction = std::clamp((flDist - flLongPenaltyStart) / (flLongPenaltyCap - flLongPenaltyStart), 0.f, 1.f);
			flDistanceScore -= flLongFraction * 420.f;
		}

		float flVisitedPenalty = 0.f;
		for (auto pVisited : m_vVisitedAreas)
		{
			if (pVisited && pArea->m_vCenter.DistTo(pVisited->m_vCenter) < 750.f)
			{
				flVisitedPenalty += 500.f;
				break;
			}
		}

		float flScore = flObjectiveScore - flSafetyPenalty + flDistanceScore - flVisitedPenalty;

		vScoredAreas.push_back({ pArea, flScore });
	}

	std::sort(vScoredAreas.begin(), vScoredAreas.end(), [](const NavAreaScore_t& a, const NavAreaScore_t& b)
		{
			return a.m_flScore > b.m_flScore;
		});

	const size_t uPathCostCandidates = std::min<size_t>(vScoredAreas.size(), 16);
	for (size_t i = 0; i < uPathCostCandidates; i++)
	{
		auto& tScoredArea = vScoredAreas[i];
		const float flPathCost = F::NavEngine.GetPathCost(vLocalOrigin, tScoredArea.m_pArea->m_vCenter);
		if (std::isfinite(flPathCost) && flPathCost < FLT_MAX)
			tScoredArea.m_flScore -= flPathCost * 0.12f;
		else
			tScoredArea.m_flScore -= 1200.f;
	}

	if (uPathCostCandidates > 0)
	{
		std::sort(vScoredAreas.begin(), vScoredAreas.end(), [](const NavAreaScore_t& a, const NavAreaScore_t& b)
			{
				return a.m_flScore > b.m_flScore;
			});
	}

	int iAttempts = 0;
	for (const auto& tAreaScore : vScoredAreas)
	{
		auto pArea = tAreaScore.m_pArea;
		if (!pArea)
			continue;

		if (iAttempts++ > 40)
			break;
		if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::Patrol))
		{
			m_pCurrentTargetArea = pArea;
			m_vVisitedAreas.push_back(pArea);
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
	m_pLastConnectedSeed = nullptr;
	m_pLastMap = nullptr;
	m_iConsecutiveFails = 0;
	m_vVisitedAreas.clear();
	m_sConnectedAreas.clear();
}
