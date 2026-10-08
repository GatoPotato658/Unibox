#include "Objectives.h"
#include "NavEngine.h"
#include "BotUtils.h"
#include "Hazards.h"
#include "Jobs/NavBotJobs.h"

#include <algorithm>

namespace
{
	constexpr float kPlanInterval = 0.4f;
	constexpr float kTeamRadius = 1800.f;
	constexpr float kHoldSlotRadius = 420.f;
	constexpr float kHoldSpotRing = 170.f;
	constexpr float kHoldArriveDist = 70.f;
	constexpr float kHoldMateClearance = 70.f;
	constexpr float kZombieRoomAvoid = 1400.f;
	constexpr float kPickGrace = 0.5f;
	constexpr float kPickCycleInterval = 0.4f;
	constexpr float kPickDeadline = 7.f;
	constexpr float kPickSameSpot = 32.f;
	constexpr size_t kPickMaxSamples = 12;
	constexpr size_t kRallyCandidates = 12;

	float GetNearestRoomDist(const std::vector<Vector>& vRooms, const Vector& vPos)
	{
		float flBest = FLT_MAX;
		for (const auto& vRoom : vRooms)
			flBest = std::min(flBest, vRoom.DistTo(vPos));
		return flBest;
	}

	float ScoreRallyArea(const CNavArea& tArea, const std::vector<Vector>& vRooms, const Vector& vSpawn, bool bHaveSpawn)
	{
		float flScore = 0.f;
		for (const auto& tSpot : tArea.m_vHidingSpots)
		{
			if (tSpot.HasGoodCover())
				flScore += 30.f;
			if (tSpot.IsExposed())
				flScore -= 12.f;
		}
		flScore = std::min(flScore, 120.f);

		if (tArea.m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT)
			flScore += 90.f;

		flScore -= std::max(0, static_cast<int>(tArea.m_vConnections.size()) - 4) * 10.f;

		const float flRoomDist = GetNearestRoomDist(vRooms, tArea.m_vCenter);
		if (flRoomDist < kZombieRoomAvoid)
			flScore -= (kZombieRoomAvoid - flRoomDist) * 0.5f;

		if (bHaveSpawn)
		{
			const float flSpawnDist = tArea.m_vCenter.DistTo(vSpawn);
			if (flSpawnDist < 600.f)
				flScore -= (600.f - flSpawnDist) * 0.5f;
			else if (flSpawnDist > 2800.f)
				flScore -= (flSpawnDist - 2800.f) * 0.1f;
		}

		return flScore;
	}
}

void CZIController::GetZombieRooms(std::vector<Vector>& vOut) const
{
	vOut.clear();
	for (const auto& tRoom : F::NavEngine.GetRespawnRooms())
	{
		if (tRoom.m_iTeam == TF_TEAM_BLUE && !tRoom.tData.m_vCenter.IsZero())
			vOut.push_back(tRoom.tData.m_vCenter);
	}
}

void CZIController::BuildRallyCandidates()
{
	m_vRallyCandidates.clear();
	m_pRally = nullptr;

	auto pNavFile = F::NavEngine.GetNavFile();
	if (!pNavFile)
		return;

	std::vector<Vector> vRooms;
	GetZombieRooms(vRooms);
	Vector vSpawn = {};
	const bool bHaveSpawn = ObjectiveUtils::GetTeamSpawnCenter(TF_TEAM_RED, vSpawn);

	std::vector<std::pair<float, CNavArea*>> vScored;
	vScored.reserve(pNavFile->m_vAreas.size());
	for (auto& tArea : pNavFile->m_vAreas)
	{
		if (NavJobUtils::IsSpawnArea(&tArea))
			continue;

		vScored.emplace_back(ScoreRallyArea(tArea, vRooms, vSpawn, bHaveSpawn), &tArea);
	}

	const size_t nKeep = std::min(vScored.size(), kRallyCandidates);
	std::partial_sort(vScored.begin(), vScored.begin() + nKeep, vScored.end(), [](const auto& a, const auto& b)
		{
			return a.first != b.first ? a.first > b.first : a.second->m_uId < b.second->m_uId;
		});

	for (size_t i = 0; i < nKeep; i++)
		m_vRallyCandidates.push_back(vScored[i].second);
}

bool CZIController::GetRally(CTFPlayer* pLocal, Vector& vOut)
{
	auto pNavFile = F::NavEngine.GetNavFile();
	if (!pNavFile)
		return false;

	size_t nRooms = 0;
	for (const auto& tRoom : F::NavEngine.GetRespawnRooms())
		nRooms += tRoom.m_iTeam == TF_TEAM_BLUE;

	if (m_pRallyData != pNavFile->m_vAreas.data() || m_nRallyAreas != pNavFile->m_vAreas.size() || m_nRallyRooms != nRooms)
	{
		m_pRallyData = pNavFile->m_vAreas.data();
		m_nRallyAreas = pNavFile->m_vAreas.size();
		m_nRallyRooms = nRooms;
		BuildRallyCandidates();
	}

	if (m_pRally && !m_tRallyCheck.Run(5.f))
	{
		vOut = m_pRally->m_vCenter;
		return true;
	}

	auto pStart = F::NavEngine.GetLocalNavArea();
	if (!pStart || m_vRallyCandidates.empty())
		return false;

	std::vector<float> vField;
	if (!F::NavEngine.GetPathCostField(pStart, vField, FLT_MAX, &m_vRallyCandidates))
		return false;

	m_pRally = nullptr;
	for (auto pArea : m_vRallyCandidates)
	{
		const float flCost = F::NavEngine.GetFieldCost(vField, pArea);
		if (std::isfinite(flCost) && flCost < FLT_MAX * 0.5f)
		{
			m_pRally = pArea;
			break;
		}
	}
	if (!m_pRally)
		return false;

	vOut = m_pRally->m_vCenter;
	return true;
}

bool CZIController::PickHoldSpot(CTFPlayer* pLocal, const Vector& vAnchor, const std::vector<Vector>& vMates, Vector& vOut)
{
	auto pMap = F::NavEngine.GetNavMap();
	if (!pMap)
		return false;

	std::vector<CNavArea*> vAreas;
	pMap->CollectAreasAround(vAnchor, kHoldSlotRadius, vAreas);

	std::vector<Vector> vRooms;
	GetZombieRooms(vRooms);
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	bool bFound = false;
	float flBestScore = -FLT_MAX;
	const auto Consider = [&](const Vector& vPos, float flCover, float flHazard)
		{
			float flScore = flCover - std::fabs(vPos.DistTo(vAnchor) - kHoldSpotRing) * 0.35f - flHazard * 0.05f;
			for (const auto& vMate : vMates)
			{
				if (vMate.DistTo2D(vPos) < kHoldMateClearance)
					flScore -= 250.f;
			}
			if (const float flRoomDist = GetNearestRoomDist(vRooms, vPos); flRoomDist < kZombieRoomAvoid)
				flScore -= (kZombieRoomAvoid - flRoomDist) * 0.5f;
			if (vPos.DistTo2D(vLocalOrigin) < 100.f)
				flScore += 40.f;

			if (flScore > flBestScore)
			{
				flBestScore = flScore;
				vOut = vPos;
				bFound = true;
			}
		};

	for (auto pArea : vAreas)
	{
		if (!pArea || NavJobUtils::IsSpawnArea(pArea))
			continue;

		const float flHazard = F::Hazards.GetCost(pArea);
		if (!std::isfinite(flHazard))
			continue;

		Consider(pArea->m_vCenter, 0.f, flHazard);
		for (const auto& tSpot : pArea->m_vHidingSpots)
		{
			if (tSpot.HasGoodCover() && !tSpot.IsExposed())
				Consider(tSpot.m_vPos, 70.f, flHazard);
		}
	}

	return bFound;
}

void CZIController::PlanZombie(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<int> vIndices;
	std::vector<Vector> vPositions;
	std::vector<int> vHealth;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (pEntity->m_iTeamNum() != TF_TEAM_RED || F::BotUtils.ShouldTarget(pLocal, pWeapon, pEntity->entindex()) != ShouldTargetEnum::Target)
			continue;

		Vector vOrigin;
		if (!F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		vIndices.push_back(pEntity->entindex());
		vPositions.push_back(vOrigin);
		vHealth.push_back(pPlayer->m_iHealth() * 100 / std::max(1, pPlayer->GetMaxHealth()));
	}

	int iBest = -1;
	if (!vPositions.empty())
	{
		const auto vCosts = ObjectiveUtils::GetPathCosts(vPositions);
		std::vector<float> vScores(vPositions.size(), FLT_MAX);
		float flBestScore = FLT_MAX;
		float flPreviousScore = FLT_MAX;
		int iPrevious = -1;
		for (size_t i = 0; i < vPositions.size(); i++)
		{
			if (vCosts[i] >= FLT_MAX)
				continue;

			int iCrowd = 0;
			for (size_t j = 0; j < vPositions.size(); j++)
				iCrowd += i != j && vPositions[i].DistTo(vPositions[j]) < 350.f;

			vScores[i] = vCosts[i] + iCrowd * 160.f + (100 - std::clamp(vHealth[i], 0, 100)) * 2.5f;
			if (vScores[i] < flBestScore)
			{
				flBestScore = vScores[i];
				iBest = static_cast<int>(i);
			}
			if (vIndices[i] == m_iTargetIdx)
			{
				iPrevious = static_cast<int>(i);
				flPreviousScore = vScores[i];
			}
		}

		if (iBest != -1 && iPrevious != -1 && flPreviousScore <= flBestScore * 1.25f + 150.f)
			iBest = iPrevious;
	}

	if (iBest != -1)
	{
		m_iTargetIdx = vIndices[iBest];
		m_tGoal = { true, false, vPositions[iBest], 0.6f, 650.f, L"Hunt" };
		return;
	}

	m_iTargetIdx = -1;
	Vector vRally;
	if (GetRally(pLocal, vRally))
		m_tGoal = { true, false, vRally, 0.45f, 560.f, L"Sweep" };
	else
		m_tGoal = {};
}

void CZIController::PlanSurvivor(CTFPlayer* pLocal)
{
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	std::vector<Vector> vMates;
	Vector vSum = {};
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		if (pEntity->entindex() == pLocal->entindex() || pEntity->IsDormant() || !pEntity->As<CTFPlayer>()->IsAlive())
			continue;

		const Vector vOrigin = pEntity->GetAbsOrigin();
		if (vOrigin.DistTo(vLocalOrigin) > kTeamRadius)
			continue;

		vMates.push_back(vOrigin);
		vSum += vOrigin;
	}

	Vector vAnchor = {};
	const wchar_t* sStatus = L"Group";
	if (!m_bSetup && !vMates.empty())
		vAnchor = vSum / static_cast<float>(vMates.size());
	else if (GetRally(pLocal, vAnchor))
		sStatus = L"Rally";
	else
	{
		m_tGoal = {};
		return;
	}
	vAnchor = ObjectiveUtils::AdjustPosToNav(vAnchor);

	bool bKeep = m_bHasHoldSpot && m_vHoldSpot.DistTo(vAnchor) <= kHoldSlotRadius + 100.f;
	if (bKeep)
	{
		for (const auto& vMate : vMates)
		{
			if (vMate.DistTo2D(m_vHoldSpot) < kHoldMateClearance * 0.6f)
			{
				bKeep = false;
				break;
			}
		}
	}
	if (!bKeep)
		m_bHasHoldSpot = PickHoldSpot(pLocal, vAnchor, vMates, m_vHoldSpot);

	const Vector vSpot = m_bHasHoldSpot ? m_vHoldSpot : vAnchor;
	m_tGoal = { true, vLocalOrigin.DistTo2D(vSpot) <= kHoldArriveDist, vSpot, 0.4f, 560.f, sStatus };
}

void CZIController::Update()
{
	m_bActive = true;
	m_bZombie = m_bSurvivor = false;

	auto pLocal = H::Entities.GetLocal();
	auto pGameRules = I::TFGameRules();
	if (!pLocal || !pGameRules)
	{
		m_tGoal = {};
		return;
	}

	m_bZombie = pLocal->m_iTeamNum() == TF_TEAM_BLUE;
	m_bSurvivor = pLocal->m_iTeamNum() == TF_TEAM_RED;
	m_bSetup = pGameRules->m_bInSetup();

	int iSurvivors = 0, iZombies = 0;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		if (!pEntity->As<CTFPlayer>()->IsAlive())
			continue;

		iSurvivors += pEntity->m_iTeamNum() == TF_TEAM_RED;
		iZombies += pEntity->m_iTeamNum() == TF_TEAM_BLUE;
	}
	m_iSurvivorsAlive = iSurvivors;
	m_iZombiesAlive = iZombies;

	if (!Vars::Misc::Movement::NavBot::Enabled.Value || !F::NavEngine.IsReady() || !pLocal->IsAlive() || (!m_bZombie && !m_bSurvivor))
	{
		m_tGoal = {};
		return;
	}

	if (!m_tPlan.Run(kPlanInterval))
		return;

	if (m_bZombie)
	{
		if (auto pWeapon = H::Entities.GetWeapon())
			PlanZombie(pLocal, pWeapon);
		else
			m_tGoal = {};
	}
	else
		PlanSurvivor(pLocal);
}

void CZIController::Reset()
{
	m_bActive = m_bZombie = m_bSurvivor = m_bSetup = false;
	m_iSurvivorsAlive = m_iZombiesAlive = 0;
	m_tGoal = {};
	m_iTargetIdx = -1;
	m_bHasHoldSpot = false;
	m_vRallyCandidates.clear();
	m_pRally = nullptr;
	m_pRallyData = nullptr;
	m_nRallyAreas = m_nRallyRooms = 0;
	m_bPicking = m_bPickSelecting = m_bPickConfirmed = false;
	m_vPickSamples.clear();
}

bool CZIController::IsSpawning(CTFPlayer* pLocal) const
{
	if (!m_bActive || !pLocal || pLocal->m_iTeamNum() != TF_TEAM_BLUE || !pLocal->IsAlive())
		return false;

	return pLocal->m_nForceTauntCam() && pLocal->InCond(TF_COND_INVULNERABLE_USER_BUFF)
		&& !pLocal->IsTaunting() && !pLocal->InCond(TF_COND_SHIELD_CHARGE) && pLocal->m_vecVelocity().Length2D() < 5.f;
}

void CZIController::CreateMove(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!m_bActive || !m_bZombie || !pLocal || !pCmd || !Vars::Misc::Movement::NavBot::Enabled.Value
		|| (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT)) || !IsSpawning(pLocal))
	{
		m_bPicking = false;
		return;
	}

	RunSpawnPicker(pLocal, pCmd);
}

void CZIController::RunSpawnPicker(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	const float flNow = I::GlobalVars->curtime;
	if (!m_bPicking)
	{
		m_bPicking = true;
		m_bPickSelecting = m_bPickConfirmed = false;
		m_flPickStart = flNow;
		m_flPickNextCycle = flNow + kPickGrace;
		m_vPickSamples.clear();
	}

	if (m_bPickConfirmed)
	{
		if (flNow < m_flPickNextCycle)
			pCmd->buttons |= IN_JUMP;
		return;
	}

	const auto Confirm = [&]
		{
			m_bPickConfirmed = true;
			m_flPickNextCycle = flNow + 0.25f;
			pCmd->buttons |= IN_JUMP;
		};

	const float flElapsed = flNow - m_flPickStart;
	if (flElapsed > kPickDeadline)
		return Confirm();
	if (flNow < m_flPickNextCycle)
		return;

	const Vector vOrigin = pLocal->GetAbsOrigin();
	if (!m_bPickSelecting)
	{
		std::vector<Vector> vSurvivors;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			Vector vPos;
			if (pEntity->m_iTeamNum() == TF_TEAM_RED && F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vPos))
				vSurvivors.push_back(vPos);
		}
		if (vSurvivors.empty())
			return Confirm();

		bool bRepeat = false;
		if (!m_vPickSamples.empty())
			bRepeat = vOrigin.DistTo(m_vPickSamples.back().m_vOrigin) < kPickSameSpot || (m_vPickSamples.size() > 1 && vOrigin.DistTo(m_vPickSamples.front().m_vOrigin) < kPickSameSpot);

		if (!bRepeat)
		{
			float flScore = FLT_MAX;
			for (const auto& vSurvivor : vSurvivors)
				flScore = std::min(flScore, vSurvivor.DistTo(vOrigin));
			m_vPickSamples.push_back({ vOrigin, flScore });
		}

		if (bRepeat || m_vPickSamples.size() >= kPickMaxSamples)
		{
			m_bPickSelecting = true;
			m_vPickBest = std::min_element(m_vPickSamples.begin(), m_vPickSamples.end(), [](const auto& a, const auto& b) { return a.m_flScore < b.m_flScore; })->m_vOrigin;
		}
		else
		{
			pCmd->buttons |= IN_ATTACK;
			m_flPickNextCycle = flNow + kPickCycleInterval;
			return;
		}
	}

	if (vOrigin.DistTo(m_vPickBest) < kPickSameSpot)
		return Confirm();

	pCmd->buttons |= IN_ATTACK;
	m_flPickNextCycle = flNow + kPickCycleInterval;
}

void CMissionBoard::UpdateZombieInfection()
{
	const ZIGoal_t& tGoal = F::ZIController.GetGoal();
	if (tGoal.m_bValid)
		SetMission(MissionKindEnum::ZombieInfection, tGoal.m_vPos, tGoal.m_flValue, tGoal.m_flScore, -1, tGoal.m_sStatus);
}

bool CNavBotCapture::GetZombieInfectionGoal(Vector& vOut)
{
	const ZIGoal_t& tGoal = F::ZIController.GetGoal();
	if (!tGoal.m_bValid)
		return false;

	vOut = tGoal.m_vPos;
	m_sCaptureStatus = tGoal.m_sStatus;
	m_bOverwriteCapture = tGoal.m_bHold;
	m_bWalkTo = false;
	return true;
}
