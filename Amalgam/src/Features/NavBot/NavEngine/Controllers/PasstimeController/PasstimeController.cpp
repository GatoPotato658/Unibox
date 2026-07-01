#include "PasstimeController.h"
#include "../../NavEngine.h"

constexpr float kPasstimeGoalPointEpsilon = 8.0f;

static Vector GetObjectiveOrigin(CBaseEntity* pEntity)
{
	if (!pEntity) return {};
	Vector v = pEntity->GetCenter();
	if (!v.IsZero()) return v;
	v = pEntity->GetAbsOrigin();
	if (!v.IsZero()) return v;
	return pEntity->m_vecOrigin();
}

static Vector GetObjectiveOrigin(CServerBaseEntity* pEntity)
{
	return GetObjectiveOrigin(reinterpret_cast<CBaseEntity*>(pEntity));
}

static Vector GetGoalWorldMins(CFuncPasstimeGoal* pGoal)
{
	auto pE = reinterpret_cast<CBaseEntity*>(pGoal);
	return pE->GetAbsOrigin() + pE->m_vecMins();
}

static Vector GetGoalWorldMaxs(CFuncPasstimeGoal* pGoal)
{
	auto pE = reinterpret_cast<CBaseEntity*>(pGoal);
	return pE->GetAbsOrigin() + pE->m_vecMaxs();
}

static Vector AdjustObjectivePosToNav(Vector vPos)
{
	if (!F::NavEngine.IsNavMeshLoaded()) return vPos;
	if (auto pArea = F::NavEngine.FindClosestNavArea(vPos, false))
	{
		Vector vCorrected = pArea->GetNearestPoint(vPos.Get2D());
		vCorrected.z = pArea->GetZ(vCorrected.x, vCorrected.y);
		return vCorrected;
	}
	return vPos;
}

static bool HasPasstimeThrowStandSpace(const Vector& vPos)
{
	CTraceFilterWorldAndPropsOnly filter = {};
	CGameTrace trace = {};
	const Vector vStart = vPos + Vec3(0.f, 0.f, 4.f);
	SDK::TraceHull(vStart, vStart, Vec3(-20.f, -20.f, 0.f), Vec3(20.f, 20.f, 72.f), MASK_PLAYERSOLID, &filter, &trace);
	if (trace.startsolid || trace.allsolid) return false;

	CGameTrace ground = {};
	SDK::TraceHull(vPos + Vec3(0.f, 0.f, 24.f), vPos - Vec3(0.f, 0.f, 56.f), Vec3(-18.f, -18.f, 0.f), Vec3(18.f, 18.f, 2.f), MASK_PLAYERSOLID, &filter, &ground);
	return ground.DidHit();
}

static bool GetTeamSpawnCenter(int iTeam, Vector& vOut)
{
	Vector vSum = {};
	int iCount = 0;
	for (const auto& tRoom : F::NavEngine.GetRespawnRooms())
	{
		if (tRoom.tData.m_vCenter.IsZero()) continue;
		if (tRoom.m_iTeam != 0 && tRoom.m_iTeam != iTeam) continue;
		vSum += tRoom.tData.m_vCenter;
		iCount++;
	}
	if (iCount > 0) { vOut = vSum / static_cast<float>(iCount); return true; }

	if (!F::NavEngine.IsNavMeshLoaded()) return false;
	const uint32_t uFlag = iTeam == TF_TEAM_RED ? TF_NAV_SPAWN_ROOM_RED : iTeam == TF_TEAM_BLUE ? TF_NAV_SPAWN_ROOM_BLUE : 0;
	if (!uFlag) return false;
	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		if (!(tArea.m_iTFAttributeFlags & uFlag)) continue;
		vSum += tArea.m_vCenter;
		iCount++;
	}
	if (iCount == 0) return false;
	vOut = vSum / static_cast<float>(iCount);
	return true;
}

void CPasstimeController::Init()
{
	m_vGoals.clear();
	m_pBall = nullptr;
	m_pLogic = nullptr;
}

void CPasstimeController::Update()
{
	Init();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->IsDormant()) continue;

		switch (pEntity->GetClassID())
		{
		case ETFClassID::CFuncPasstimeGoal:  m_vGoals.push_back(pEntity->As<CFuncPasstimeGoal>()); break;
		case ETFClassID::CPasstimeBall:      m_pBall = pEntity->As<CPasstimeBall>();              break;
		case ETFClassID::CTFPasstimeLogic:   m_pLogic = pEntity->As<CTFPasstimeLogic>();           break;
		}
	}

	if (!m_pBall && m_pLogic) m_pBall = m_pLogic->GetBall();

	if (Vars::Debug::Info.Value)
	{
		for (auto pGoal : m_vGoals)
		{
			if (!pGoal) continue;
			const Vector vOrigin = GetObjectiveOrigin(pGoal);
			const Vector vMins = GetGoalWorldMins(pGoal);
			const Vector vMaxs = GetGoalWorldMaxs(pGoal);

			Color_t tColor = { 255, 255, 255, 180 };
			if (pGoal->m_bTriggerDisabled())              tColor = { 255, 80, 80, 180 };
			else if (GetGoalTeam(pGoal) == TEAM_UNASSIGNED) tColor = { 80, 255, 120, 180 };

			G::BoxStorage.emplace_back(vOrigin, vMins - vOrigin, vMaxs - vOrigin, Vec3(),
				I::GlobalVars->curtime + 0.2f, tColor, Color_t(0, 0, 0, 0), true);
		}
	}
}

int CPasstimeController::GetGoalTeam(CFuncPasstimeGoal* pGoal) const
{
	if (!pGoal) return TEAM_UNASSIGNED;

	// Some maps store the defending team via targetname; convert to scoring team (opposite).
	const int iMapTeam = SDK::GetPasstimeGoalMapTeam(GetObjectiveOrigin(pGoal), nullptr);
	if (iMapTeam == TF_TEAM_RED)  return TF_TEAM_BLUE;
	if (iMapTeam == TF_TEAM_BLUE) return TF_TEAM_RED;
	return pGoal->m_iTeamNum();
}

CPasstimeBall* CPasstimeController::GetBall()
{
	if (!m_pBall && m_pLogic) m_pBall = m_pLogic->GetBall();
	return m_pBall;
}

int CPasstimeController::GetCarrier()
{
	auto pBall = GetBall();
	if (!pBall) return -1;
	auto pCarrier = pBall->GetCarrier();
	if (!pCarrier || pCarrier->IsDormant() || !pCarrier->IsAlive()) return -1;
	return pCarrier->entindex();
}

bool CPasstimeController::GetGoalInfo(int iScoringTeam, const Vector& vRelativePos, PasstimeGoalInfo& tOut)
{
	CFuncPasstimeGoal* pBestOwn = nullptr;
	float flBestOwnDist = FLT_MAX;
	CFuncPasstimeGoal* pBestNeutral = nullptr;
	float flBestNeutralScore = -FLT_MAX;

	Vector vScoringSpawn = {};
	const bool bHaveSpawn = iScoringTeam != TEAM_UNASSIGNED && GetTeamSpawnCenter(iScoringTeam, vScoringSpawn);

	for (auto pGoal : m_vGoals)
	{
		if (!pGoal || pGoal->m_bTriggerDisabled()) continue;
		const Vector vGoalPos = GetObjectiveOrigin(pGoal);
		if (vGoalPos.IsZero()) continue;

		const int iGoalTeam = GetGoalTeam(pGoal);
		if (iGoalTeam == iScoringTeam)
		{
			const float flDist = vRelativePos.IsZero() ? 0.f : vRelativePos.DistToSqr(vGoalPos);
			if (flDist < flBestOwnDist) { pBestOwn = pGoal; flBestOwnDist = flDist; }
			continue;
		}

		if (iGoalTeam == TEAM_UNASSIGNED || iGoalTeam == TEAM_INVALID || iGoalTeam == 0)
		{
			// Farther from our spawn = closer to enemy = better neutral goal.
			float flScore = bHaveSpawn ? vScoringSpawn.DistToSqr(vGoalPos)
				: -vRelativePos.DistToSqr(vGoalPos);
			if (flScore > flBestNeutralScore) { pBestNeutral = pGoal; flBestNeutralScore = flScore; }
		}
	}

	CFuncPasstimeGoal* pSelected = pBestOwn ? pBestOwn : pBestNeutral;
	if (!pSelected) return false;

	tOut.m_pGoal = pSelected;
	tOut.m_iGoalType = pSelected->m_iGoalType();
	tOut.m_iTeam = GetGoalTeam(pSelected);
	tOut.m_vOrigin = GetObjectiveOrigin(pSelected);
	tOut.m_vMins = GetGoalWorldMins(pSelected);
	tOut.m_vMaxs = GetGoalWorldMaxs(pSelected);
	return !tOut.m_vOrigin.IsZero();
}

bool CPasstimeController::GetGoalPos(int iScoringTeam, const Vector& vRelativePos, Vector& vOut)
{
	PasstimeGoalInfo tGoal = {};
	if (!GetGoalInfo(iScoringTeam, vRelativePos, tGoal)) return false;

	vOut = IsEndzoneGoal(tGoal.m_iGoalType)
		? AdjustObjectivePosToNav(tGoal.m_vOrigin)
		: GetThrowTargetPos(tGoal, vRelativePos);
	return !vOut.IsZero();
}

bool CPasstimeController::GetBallPos(Vector& vOut)
{
	auto pBall = GetBall();
	if (!pBall) return false;

	auto pCarrier = pBall->GetCarrier();
	if (pCarrier && !pCarrier->IsDormant() && pCarrier->IsAlive())
	{
		vOut = AdjustObjectivePosToNav(pCarrier->GetAbsOrigin());
		return !vOut.IsZero();
	}

	vOut = AdjustObjectivePosToNav(GetObjectiveOrigin(pBall));
	return !vOut.IsZero();
}

bool CPasstimeController::IsPointInGoal(const PasstimeGoalInfo& tGoal, const Vector& vPoint) const
{
	if (!tGoal.m_pGoal) return false;
	return vPoint.x >= tGoal.m_vMins.x - kPasstimeGoalPointEpsilon && vPoint.x <= tGoal.m_vMaxs.x + kPasstimeGoalPointEpsilon
		&& vPoint.y >= tGoal.m_vMins.y - kPasstimeGoalPointEpsilon && vPoint.y <= tGoal.m_vMaxs.y + kPasstimeGoalPointEpsilon
		&& vPoint.z >= tGoal.m_vMins.z - kPasstimeGoalPointEpsilon && vPoint.z <= tGoal.m_vMaxs.z + kPasstimeGoalPointEpsilon;
}

Vector CPasstimeController::GetThrowTargetPos(const PasstimeGoalInfo& tGoal, const Vector& vRelativePos)
{
	if (!tGoal.m_pGoal) return {};

	const Vector vGoalCenter = (tGoal.m_vMins + tGoal.m_vMaxs) * 0.5f;
	const Vector vHalfExtents = (tGoal.m_vMaxs - tGoal.m_vMins) * 0.5f;
	const float flGoalRadius = std::max(vHalfExtents.Length2D(), 96.0f);
	const float flMaxPassRange = GetMaxPassRange();
	const std::array<float, 4> vStandOffs = flMaxPassRange != FLT_MAX
		? std::array<float, 4>{
		std::clamp(flMaxPassRange * 0.18f, 120.f, 220.f),
			std::clamp(flMaxPassRange * 0.28f, 180.f, 320.f),
			std::clamp(flMaxPassRange * 0.38f, 240.f, 420.f),
			std::clamp(flMaxPassRange * 0.48f, 300.f, 520.f) }
	: std::array<float, 4>{ 140.f, 220.f, 320.f, 420.f };

	Vector vPreferredDir = vRelativePos - vGoalCenter; vPreferredDir.z = 0.f;
	if (vPreferredDir.Normalize() <= 0.01f) vPreferredDir = { 1.f, 0.f, 0.f };

	Vector vBest = {};
	float flBestScore = -FLT_MAX;
	for (float flStandOff : vStandOffs)
	{
		for (int i = 0; i < 12; i++)
		{
			const float flYaw = Math::Deg2Rad(30.f * i);
			const Vector vDir = { cosf(flYaw), sinf(flYaw), 0.f };
			Vector vCandidate = vGoalCenter + vDir * (flGoalRadius + flStandOff);
			vCandidate.z = tGoal.m_vOrigin.z;
			vCandidate = AdjustObjectivePosToNav(vCandidate);
			if (vCandidate.IsZero() || !HasPasstimeThrowStandSpace(vCandidate)) continue;

			float flScore = vDir.Dot(vPreferredDir) * 120.f
				- vCandidate.DistToSqr(vRelativePos) * 0.0008f
				- flStandOff * 2.f;
			if (F::NavEngine.IsVectorVisibleNavigation(vCandidate + Vec3(0, 0, 45), vGoalCenter + Vec3(0, 0, 45), MASK_SHOT | CONTENTS_GRATE))
				flScore += 1200.f;

			if (flScore > flBestScore) { flBestScore = flScore; vBest = vCandidate; }
		}
	}

	if (!vBest.IsZero()) return vBest;

	Vector vFallback = vGoalCenter + vPreferredDir * (flGoalRadius + vStandOffs.front());
	vFallback.z = tGoal.m_vOrigin.z;
	return AdjustObjectivePosToNav(vFallback);
}
