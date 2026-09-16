#include "Objectives.h"
#include "NavEngine.h"
#include "Jobs/NavBotJobs.h"
#include "BotUtils.h"
#include <string>
#include <string_view>

static std::string GetNormalizedLevelName()
{
	auto sMapName = std::string(I::EngineClient->GetLevelName());
	size_t nLastSlash = sMapName.find_last_of("/\\");
	if (nLastSlash != std::string::npos)
		sMapName = sMapName.substr(nLastSlash + 1);
	return sMapName;
}

static bool MapStartsWith(const std::string& sMapName, std::string_view sPrefix)
{
	return sMapName.find(sPrefix) == 0;
}

static ETFGameType GetGameType()
{

	auto sMapName = GetNormalizedLevelName();
	F::GameObjectiveController.m_bDoomsday = sMapName.find("sd_doomsday") != std::string::npos;
	F::GameObjectiveController.m_bHaarp = sMapName.find("ctf_haarp") != std::string::npos;

	int iType = TF_GAMETYPE_UNDEFINED;
	if (auto pGameRules = I::TFGameRules())
		iType = pGameRules->m_nGameType();

	return static_cast<ETFGameType>(iType);
}

void CGameObjectiveController::Update()
{
	static float flNextGameTypeRefresh = 0.0f;
	if (m_eGameMode == TF_GAMETYPE_UNDEFINED || I::GlobalVars->curtime >= flNextGameTypeRefresh)
	{
		m_eGameMode = GetGameType();
		flNextGameTypeRefresh = I::GlobalVars->curtime + 1.0f;
	}

	const auto sMapName = GetNormalizedLevelName();

	F::MVMController.Update();
	if (F::MVMController.IsActive())
		return;

	if (MapStartsWith(sMapName, "cppl_"))
	{
		F::CPController.Update();
		F::PLController.Update();
		return;
	}
	if (MapStartsWith(sMapName, "vsh_") || MapStartsWith(sMapName, "2koth_") || MapStartsWith(sMapName, "koth_") || MapStartsWith(sMapName, "cp_") || MapStartsWith(sMapName, "tc_"))
	{
		F::CPController.Update();
		return;
	}
	if (MapStartsWith(sMapName, "pl_") || MapStartsWith(sMapName, "plr_"))
	{
		F::PLController.Update();
		return;
	}
	if (MapStartsWith(sMapName, "pass_"))
	{
		F::PasstimeController.Update();
		return;
	}
	if (MapStartsWith(sMapName, "ctf_") || MapStartsWith(sMapName, "sd_") || MapStartsWith(sMapName, "rd_") || MapStartsWith(sMapName, "pd_"))
	{
		F::FlagController.Update();
		if (MapStartsWith(sMapName, "sd_doomsday"))
		{
			F::CPController.Update();
			F::DoomsdayController.Update();
		}
		if (MapStartsWith(sMapName, "ctf_haarp"))
		{
			F::CPController.Update();
			F::HaarpController.Update();
		}
		return;
	}

	switch (m_eGameMode)
	{
	case TF_GAMETYPE_CTF:
		F::FlagController.Update();
		if (m_bDoomsday)
		{
			F::CPController.Update();
			F::DoomsdayController.Update();
		}
		if (m_bHaarp)
		{
			F::CPController.Update();
			F::HaarpController.Update();
		}
		break;
	case TF_GAMETYPE_CP:
		F::CPController.Update();
		break;
	case TF_GAMETYPE_ESCORT:
		F::PLController.Update();
		break;
	case TF_GAMETYPE_PASSTIME:
		F::PasstimeController.Update();
		break;
	default:
		if (m_bDoomsday)
		{
			F::FlagController.Update();
			F::CPController.Update();
			F::DoomsdayController.Update();
		}
		if (m_bHaarp)
		{
			F::FlagController.Update();
			F::CPController.Update();
			F::HaarpController.Update();
		}
		break;
	}
}

void CGameObjectiveController::Reset()
{
	m_eGameMode = TF_GAMETYPE_UNDEFINED;
	m_bDoomsday = false;
	m_bHaarp = false;
	F::FlagController.Init();
	F::PLController.Init();
	F::CPController.Init();
	F::PasstimeController.Init();
	F::MVMController.Reset();
}

inline FlagInfo BuildFlagInfo(CCaptureFlag* pFlag)
{
	FlagInfo tFlag{};
	tFlag.m_pFlag = pFlag;
	tFlag.m_iTeam = pFlag ? pFlag->m_iTeamNum() : 0;
	return tFlag;
}

FlagInfo CFlagController::GetFlag(int iTeam)
{
	for (auto tFlag : m_vFlags)
	{
		if (!tFlag.m_pFlag)
			continue;

		if (tFlag.m_iTeam == iTeam)
			return tFlag;
	}

	return {};
}

Vector CFlagController::GetPosition(CCaptureFlag* pFlag)
{
	return pFlag->GetAbsOrigin();
}

bool CFlagController::GetPosition(int iTeam, Vector& vOut)
{
	auto tFlag = GetFlag(iTeam);
	if (tFlag.m_pFlag)
	{
		vOut = GetPosition(tFlag.m_pFlag);
		return true;
	}

	return false;
}

bool CFlagController::GetSpawnPosition(int iTeam, Vector& vOut)
{
	auto tFlag = GetFlag(iTeam);
	if (tFlag.m_pFlag && m_mSpawnPositions.contains(tFlag.m_pFlag->entindex()))
	{
		vOut = m_mSpawnPositions[tFlag.m_pFlag->entindex()];
		return true;
	}

	return false;
}

int CFlagController::GetCarrier(CCaptureFlag* pFlag)
{
	if (!pFlag)
		return -1;

	auto pOwnerEnt = pFlag->m_hOwnerEntity().Get();
	if (!pOwnerEnt || !pOwnerEnt->IsPlayer())
		return -1;

	auto pPlayer = pOwnerEnt->As<CTFPlayer>();
	if (pPlayer->IsDormant() || !pPlayer->IsAlive())
		return -1;

	return pPlayer->entindex();
}

int CFlagController::GetCarrier(int iTeam)
{
	auto tFlag = GetFlag(iTeam);
	if (tFlag.m_pFlag)
		return GetCarrier(tFlag.m_pFlag);

	return -1;
}

int CFlagController::GetStatus(CCaptureFlag* pFlag)
{
	return pFlag->m_nFlagStatus();
}

int CFlagController::GetStatus(int iTeam)
{
	auto tFlag = GetFlag(iTeam);
	if (tFlag.m_pFlag)
		return GetStatus(tFlag.m_pFlag);

	return TF_FLAGINFO_HOME;
}

void CFlagController::Init()
{

	m_vFlags.clear();
	m_mSpawnPositions.clear();
}

void CFlagController::Update()
{
	m_vFlags.clear();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		FlagInfo tFlag = BuildFlagInfo(pFlag);

		if (pFlag->m_nFlagStatus() == TF_FLAGINFO_HOME)
			m_mSpawnPositions[pFlag->entindex()] = pFlag->GetAbsOrigin();

		m_vFlags.push_back(tFlag);

		if (Vars::Debug::Info.Value)
		{
			G::SphereStorage.emplace_back(pFlag->GetAbsOrigin(), 50.f, 20, 20, I::GlobalVars->curtime + 0.1f, Color_t(255, 255, 255, 10), Color_t(255, 255, 255, 100));
		}
	}
}

template <typename TContainer>
static int FindIgnoredControlPointIndex(const std::string& sLevelName, const TContainer& aIgnorePoints)
{
	for (const auto& tIgnore : aIgnorePoints)
	{
		if (sLevelName.find(tIgnore.m_sMapName) != std::string::npos)
			return tIgnore.m_iPointIdx;
	}

	return -1;
}

void CCPController::UpdateObjectiveResource()
{

	m_pObjectiveResource = H::Entities.GetObjectiveResource();
}

void CCPController::UpdateControlPoints()
{

	if (!m_pObjectiveResource)
		return;

	const int iNumControlPoints = std::clamp(m_pObjectiveResource->m_iNumControlPoints(), 0, MAX_CONTROL_POINTS);

	if (!iNumControlPoints)
		return;

	if (iNumControlPoints <= MAX_CONTROL_POINTS)
	{
		for (int i = iNumControlPoints; i < MAX_CONTROL_POINTS; ++i)
			m_aControlPointData[i] = CPInfo();
	}

	for (int i = 0; i < iNumControlPoints; ++i)
	{
		auto& tData = m_aControlPointData[i];
		tData.m_iIdx = i;

		tData.m_vPos = m_pObjectiveResource->m_vCPPositions(i);
		tData.m_bGotPos = true;
	}

	static float flNextCapStatusRefresh = 0.0f;
	const float flCurrentTime = I::GlobalVars->curtime;
	if (flCurrentTime >= flNextCapStatusRefresh)
	{
		flNextCapStatusRefresh = flCurrentTime + 1.0f;

		for (int i = 0; i < iNumControlPoints; ++i)
		{
			auto& tData = m_aControlPointData[i];

			const bool bCanCapRED = IsPointUseable(i, TF_TEAM_RED);
			const bool bCanCapBLU = IsPointUseable(i, TF_TEAM_BLUE);

			tData.m_bCanCap.at(0) = bCanCapRED;
			tData.m_bCanCap.at(1) = bCanCapBLU;
		}
	}
}

bool CCPController::TeamCanCapPoint(int iIndex, int iTeam)
{
	return m_pObjectiveResource->m_bTeamCanCap(iIndex + iTeam * MAX_CONTROL_POINTS);
}

int CCPController::GetPreviousPointForPoint(int iIndex, int iTeam, int iPrevIdx)
{
	return m_pObjectiveResource->m_iPreviousPoints(iPrevIdx + (iIndex * MAX_PREVIOUS_POINTS) + (iTeam * MAX_CONTROL_POINTS * MAX_PREVIOUS_POINTS));
}

int CCPController::GetFarthestOwnedControlPoint(int iTeam)
{
	int iOwnedEnd = m_pObjectiveResource->m_iBaseControlPoints(iTeam);
	const int iNumControlPoints = std::clamp(m_pObjectiveResource->m_iNumControlPoints(), 0, MAX_CONTROL_POINTS);
	if (iOwnedEnd < 0 || iOwnedEnd >= iNumControlPoints)
		return -1;

	int iWalk = 1;
	int iEnemyEnd = iNumControlPoints - 1;
	if (iOwnedEnd != 0)
	{
		iWalk = -1;
		iEnemyEnd = 0;
	}

	int iFarthestPoint = iOwnedEnd;
	for (int iPoint = iOwnedEnd; iPoint != iEnemyEnd; iPoint += iWalk)
	{

		if (m_pObjectiveResource->m_iOwner(iPoint) != iTeam)
			break;

		iFarthestPoint = iPoint;
	}

	return iFarthestPoint;
}

bool CCPController::IsPointUseable(int iIndex, int iTeam)
{
	if (!m_pObjectiveResource || iIndex < 0 || iIndex >= std::clamp(m_pObjectiveResource->m_iNumControlPoints(), 0, MAX_CONTROL_POINTS)
		|| iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
		return false;

	if (m_pObjectiveResource->m_iOwner(iIndex) == iTeam)
		return false;

	if (!TeamCanCapPoint(iIndex, iTeam))
		return false;

	if (m_pObjectiveResource->m_bPlayingMiniRounds() && !m_pObjectiveResource->m_bInMiniRound(iIndex))
		return false;

	if (m_pObjectiveResource->m_bCPLocked(iIndex))
		return false;

	static auto tf_caplinear = H::ConVars.FindVar("tf_caplinear");
	if (!tf_caplinear || !tf_caplinear->GetBool() || m_pObjectiveResource->m_iNumControlPoints() == 1)
		return true;

	int iPointNeeded = GetPreviousPointForPoint(iIndex, iTeam, 0);

	if (iPointNeeded == iIndex)
		return true;

	if (iPointNeeded == -1)
	{

		if (!m_pObjectiveResource->m_bPlayingMiniRounds())
		{

			int iFarthestPoint = GetFarthestOwnedControlPoint(iTeam);
			return (abs(iFarthestPoint - iIndex) <= 1);
		}

		else

			return true;
	}

	for (int iPrevPoint = 0; iPrevPoint < MAX_PREVIOUS_POINTS; iPrevPoint++)
	{
		iPointNeeded = GetPreviousPointForPoint(iIndex, iTeam, iPrevPoint);
		if (iPointNeeded != -1)
		{

			if (m_pObjectiveResource->m_iOwner(iPointNeeded) != iTeam)
				return false;
		}
	}
	return true;
}

bool CCPController::GetClosestControlPointInfo(Vector vPos, int iTeam, std::pair<int, Vector>& tOut)
{

	if (!m_pObjectiveResource)
		return false;

	int iTeamIdx = iTeam - TF_TEAM_RED;
	if (iTeamIdx < 0 || iTeamIdx > 1)
		return false;

	if (!m_pObjectiveResource->m_iNumControlPoints())
		return false;

	const int IgnoreIdx = FindIgnoredControlPointIndex(SDK::GetLevelName(), m_aIgnorePoints);

	Vector BestControlPoint;
	int iBestIndex = -1;
	float flBestDist = FLT_MAX;
	for (auto tControlPoint : m_aControlPointData)
	{

		if (tControlPoint.m_iIdx == IgnoreIdx)
			continue;

		if (tControlPoint.m_bCanCap.at(iTeamIdx) && tControlPoint.m_bGotPos)
		{
			const auto flDist = tControlPoint.m_vPos.DistToSqr(vPos);

			if (flDist < flBestDist)
			{
				flBestDist = flDist;
				BestControlPoint = tControlPoint.m_vPos;
				iBestIndex = tControlPoint.m_iIdx;
			}
		}
	}

	if (flBestDist == FLT_MAX || iBestIndex == -1)
		return false;

	tOut = { iBestIndex, BestControlPoint };
	return true;
}

bool CCPController::GetClosestControlPoint(Vector vPos, int iTeam, Vector& vOut)
{
	std::pair<int, Vector> tInfo;
	if (GetClosestControlPointInfo(vPos, iTeam, tInfo))
	{
		vOut = tInfo.second;
		return true;
	}
	return false;
}

void CCPController::Init()
{
	for (auto& cp : m_aControlPointData)
		cp = CPInfo();

	m_pObjectiveResource = nullptr;
}

void CCPController::Update()
{
	UpdateObjectiveResource();
	UpdateControlPoints();
}

inline int GetPayloadTeamIndex(int iTeam)
{
	return iTeam - TF_TEAM_RED;
}

void CPLController::Init()
{
	m_aPayloadCounts = {};
}

void CPLController::Update()
{
	m_aPayloadCounts = {};

	for (auto pPayload : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pPayload || pPayload->GetClassID() != ETFClassID::CObjectCartDispenser)
			continue;

		int iTeam = pPayload->m_iTeamNum();

		if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
			continue;

		const auto iTeamIndex = GetPayloadTeamIndex(iTeam);
		auto& aPayloads = m_aPayloads[iTeamIndex];
		auto& nPayloadCount = m_aPayloadCounts[iTeamIndex];
		if (nPayloadCount < aPayloads.size())
			aPayloads[nPayloadCount++] = pPayload->As<CObjectCartDispenser>();
	}
}

CObjectCartDispenser* CPLController::GetClosestPayload(Vector vPos, int iTeam)
{
	if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
		return nullptr;

	float flMinDist = FLT_MAX;
	CObjectCartDispenser* pBestEnt = nullptr;

	const auto iTeamIndex = GetPayloadTeamIndex(iTeam);
	for (size_t nPayloadIndex = 0; nPayloadIndex < m_aPayloadCounts[iTeamIndex]; nPayloadIndex++)
	{
		auto pEntity = m_aPayloads[iTeamIndex][nPayloadIndex];
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CObjectCartDispenser || pEntity->IsDormant())
			continue;

		const auto vOrigin = pEntity->GetAbsOrigin();
		const auto flDist = vOrigin.DistToSqr(vPos);
		if (flDist < flMinDist)
		{
			pBestEnt = pEntity;
			flMinDist = flDist;
		}
	}

	return pBestEnt;
}

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

CCaptureFlag* CDoomsdayController::GetFlag()
{
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		return pEntity->As<CCaptureFlag>();
	}

	return nullptr;
}

static bool GetDoomsdayCapturePos(Vector& vOut)
{

	for (int n = I::EngineClient->GetMaxClients() + 1; n <= I::ClientEntityList->GetHighestEntityIndex(); n++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
		auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
		if (!pEntity || pEntity->IsDormant() || pEntity->GetClassID() != ETFClassID::CDynamicProp)
			continue;

		auto pModel = pEntity->GetModel();
		if (!pModel)
			continue;

		const char* pszModelName = I::ModelInfoClient->GetModelName(pModel);
		if (pszModelName && std::string_view(pszModelName).find("rocket_lid") != std::string_view::npos)
		{
			Vector vPos = pEntity->GetAbsOrigin();
			if (vPos.IsZero())
				vPos = pEntity->GetCenter();
			if (vPos.IsZero())
				continue;

			vOut = AdjustObjectivePosToNav(vPos);
			if (Vars::Debug::Logging.Value)
				SDK::Output("DoomsdayController", std::format("GetDoomsdayCapturePos: found rocket via rocket_lid_model ({})", pszModelName).c_str(), { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return true;
		}
	}

	return false;
}

bool CDoomsdayController::GetCapturePos(Vector& vOut)
{
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	Vector vCapturePos = {};
	if (GetDoomsdayCapturePos(vCapturePos))
	{
		m_vCachedCapturePos = vCapturePos;
		m_bHasCachedCapturePos = true;
	}
	else if (m_bHasCachedCapturePos)
	{
		vCapturePos = m_vCachedCapturePos;
	}

	if (vCapturePos.IsZero())
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("DoomsdayController", "GetCapturePos: failed to find rocket position", { 255, 100, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return false;
	}

	vOut = vCapturePos;
	return true;
}

bool CDoomsdayController::GetGoal(Vector& vOut)
{
	m_sDoomsdayStatus = L"";
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	auto pFlag = GetFlag();
	if (!pFlag)
		return false;

	int iLocalTeam = pLocal->m_iTeamNum();
	int iFlagTeam = pFlag->m_iTeamNum();

	bool bIsCarrier = F::FlagController.GetCarrier(pFlag) == pLocal->entindex();
	if (!bIsCarrier)
	{
		auto pCarried = pLocal->m_hCarriedObject().Get();
		if (pCarried == pFlag)
			bIsCarrier = true;
	}

	if (bIsCarrier)
	{
		if (GetCapturePos(vOut))
		{
			m_sDoomsdayStatus = L"Rocket";

			float flClosestDist = 1000.0f;
			CBaseEntity* pClosestTrain = nullptr;
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
			{
				if (pEntity->GetClassID() != ETFClassID::CFuncTrackTrain)
					continue;

				float flDist = pLocal->GetAbsOrigin().DistTo(pEntity->GetCenter());
				if (flDist < flClosestDist)
				{
					flClosestDist = flDist;
					pClosestTrain = pEntity;
				}
			}

			if (pClosestTrain)
			{
				vOut = pClosestTrain->GetCenter();
			}
			else
			{
				Vector vDir = vOut - pLocal->GetAbsOrigin();
				float len = vDir.Length2D();
				if (len > 0.001f)
				{
					vDir /= len;
					vOut -= (vDir * 40.0f);
				}
			}

			return true;
		}

		return false;
	}

	if (iFlagTeam != 0 && iFlagTeam != iLocalTeam)
		return false;

	int iCarrierIdx = F::FlagController.GetCarrier(pFlag);
	if (iCarrierIdx != -1)
	{
		m_sDoomsdayStatus = L"Assist";
		vOut = pFlag->GetAbsOrigin();
		return true;
	}

	m_sDoomsdayStatus = L"Australium";
	vOut = pFlag->GetAbsOrigin();
	return true;
}

void CDoomsdayController::Update()
{
	static std::string sLastMap = "";
	const char* szLevelName = I::EngineClient->GetLevelName();
	std::string sCurrentMap = szLevelName ? szLevelName : "";
	if (sCurrentMap != sLastMap || sCurrentMap.empty())
	{
		m_vCachedCapturePos = {};
		m_bHasCachedCapturePos = false;
		sLastMap = sCurrentMap;
	}
}

static CCaptureFlag* GetHaarpFlag(const Vector& vRelativePos = Vector())
{
	CCaptureFlag* pBestFlag = nullptr;
	float flBestDist = FLT_MAX;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		if (vRelativePos.IsZero())
			return pFlag;

		float flDist = vRelativePos.DistTo(pFlag->GetAbsOrigin());
		if (flDist < flBestDist)
		{
			flBestDist = flDist;
			pBestFlag = pFlag;
		}
	}

	return pBestFlag;
}

bool GetHaarpCapturePos(int iLocalTeam, Vector& vOut)
{
	auto pResource = H::Entities.GetObjectiveResource();
	if (!pResource)
		return false;

	for (auto& tTrigger : G::TriggerStorage)
	{
		if (tTrigger.m_eType != TriggerTypeEnum::CaptureArea)
			continue;

		if (tTrigger.m_iTeam != 0 && tTrigger.m_iTeam != TF_TEAM_RED)
			continue;

		vOut = AdjustObjectivePosToNav(tTrigger.m_vCenter);

		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(255, 0, 255, 10), Color_t(255, 0, 255, 100));

		return true;
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (pEntity->GetClassID() != ETFClassID::CCaptureZone)
			continue;

		static int nDisabledOffset = U::NetVars.GetNetVar("CBaseEntity", "m_bDisabled");
		if (*reinterpret_cast<bool*>(uintptr_t(pEntity) + nDisabledOffset))
			continue;

		int iTeam = pEntity->m_iTeamNum();
		if (iTeam != 0 && iTeam != iLocalTeam)
			continue;

		vOut = AdjustObjectivePosToNav(pEntity->GetAbsOrigin());

		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(255, 128, 0, 10), Color_t(255, 128, 0, 100));

		return true;
	}

	int iFallbackIdx = -1;
	const int iControlPointCount = std::clamp(pResource->m_iNumControlPoints(), 0, MAX_CONTROL_POINTS);
	for (int i = 0; i < iControlPointCount; i++)
	{
		if (!F::CPController.IsPointUseable(i, iLocalTeam))
			continue;

		Vector vCPPos = pResource->m_vCPPositions(i);

		bool bIsFlagSpot = false;
		for (int iTeam = 0; iTeam < 4; iTeam++)
		{
			Vector vSpawnPos;
			if (F::FlagController.GetSpawnPosition(iTeam, vSpawnPos))
			{
				if (vCPPos.DistTo(vSpawnPos) < 100.f)
				{
					bIsFlagSpot = true;
					break;
				}
			}
		}

		if (bIsFlagSpot)
		{
			if (iFallbackIdx == -1) iFallbackIdx = i;
			continue;
		}

		vOut = AdjustObjectivePosToNav(vCPPos);
		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(0, 255, 0, 10), Color_t(0, 255, 0, 100));

		return true;
	}

	if (iFallbackIdx != -1)
	{
		vOut = AdjustObjectivePosToNav(pResource->m_vCPPositions(iFallbackIdx));
		return true;
	}

	return false;
}

bool CHaarpController::GetCapturePos(Vector& vOut)
{
	m_sHaarpStatus = L"";
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	int iLocalTeam = pLocal->m_iTeamNum();
	if (iLocalTeam != TF_TEAM_BLUE)
		return false;

	Vector vCapturePos = {};
	if (GetHaarpCapturePos(iLocalTeam, vCapturePos))
	{
		m_vCachedCapturePos = vCapturePos;
		m_bHasCachedCapturePos = true;
	}
	else if (m_bHasCachedCapturePos)
		vCapturePos = m_vCachedCapturePos;

	if (vCapturePos.IsZero())
		return false;

	int iLocalIndex = pLocal->entindex();
	bool bIsCarryingFlag = false;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		int iCarrierIdx = F::FlagController.GetCarrier(pFlag);
		if (iCarrierIdx == iLocalIndex)
		{
			bIsCarryingFlag = true;
			break;
		}
	}

	if (!bIsCarryingFlag)
		return false;

	Vector vGoalPos = vCapturePos;
	if (F::NavEngine.IsNavMeshLoaded())
	{
		CNavArea* pArea = F::NavEngine.FindClosestNavArea(vCapturePos, false);
		if (pArea)
		{
			Vector vCenter = pArea->m_vCenter;
			vCenter.z = pArea->GetZ(vCenter.x, vCenter.y);
			vGoalPos = vCenter;
		}
	}

	m_sHaarpStatus = L"CP";
	vOut = vGoalPos;
	if (Vars::Debug::Info.Value)
		G::SphereStorage.emplace_back(vGoalPos, 30.f, 20, 20, I::GlobalVars->curtime + 2.2f, Color_t(0, 255, 0, 10), Color_t(0, 255, 0, 100));

	return true;
}

bool CHaarpController::GetDefensePos(Vector& vOut)
{
	m_sHaarpStatus = L"";
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	int iLocalTeam = pLocal->m_iTeamNum();
	if (iLocalTeam != TF_TEAM_RED)
		return false;

	Vector vCapturePos = {};
	if (GetHaarpCapturePos(TF_TEAM_BLUE, vCapturePos))
	{
		m_vCachedBluCapturePos = vCapturePos;
		m_bHasCachedBluCapturePos = true;
	}
	else if (m_bHasCachedBluCapturePos)
		vCapturePos = m_vCachedBluCapturePos;

	if (vCapturePos.IsZero())
		return false;

	auto pFlag = GetHaarpFlag(vCapturePos);

	if (!pFlag)
		return false;

	if (Vars::Debug::Info.Value)
		G::SphereStorage.emplace_back(vCapturePos, 30.f, 20, 20, I::GlobalVars->curtime + 2.2f, Color_t(0, 255, 255, 10), Color_t(0, 255, 255, 100));

	int iStatus = F::FlagController.GetStatus(pFlag);
	if (iStatus == TF_FLAGINFO_STOLEN)
	{
		m_sHaarpStatus = L"CP";
		vOut = vCapturePos;
		return true;
	}

	m_sHaarpStatus = L"Flag";
	vOut = pFlag->GetAbsOrigin();
	return true;
}

void CHaarpController::Update()
{
	static std::string sLastMap = "";
	const char* szLevelName = I::EngineClient->GetLevelName();
	std::string sCurrentMap = szLevelName ? szLevelName : "";
	if (sCurrentMap != sLastMap || sCurrentMap.empty())
	{
		m_vCachedCapturePos = {};
		m_bHasCachedCapturePos = false;
		m_vCachedBluCapturePos = {};
		m_bHasCachedBluCapturePos = false;
		sLastMap = sCurrentMap;
	}
}

static bool IsMadMilk(CTFWeaponBase* pWeapon)
{
	return pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_JAR_MILK;
}

static bool SlotHasShot(int iSlot)
{
	const WeaponAmmoInfo_t& tAmmoInfo = G::AmmoInSlot[iSlot];
	if (!tAmmoInfo.m_bUsesAmmo)
		return true;

	if (tAmmoInfo.m_iMaxClip == WEAPON_NOCLIP)
		return tAmmoInfo.m_iReserve > 0;

	return tAmmoInfo.m_iClip > 0;
}

static float GetPrimaryRange(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!pLocal || !pWeapon)
		return 300.f;

	switch (pLocal->m_iClass())
	{
	case TF_CLASS_PYRO:
		return 285.f;
	case TF_CLASS_SCOUT:
		return 520.f;
	case TF_CLASS_SNIPER:
		return 1800.f;
	default:
		return std::max(300.f, pWeapon->GetRange());
	}
}

static bool IsVisibleToShoot(CTFPlayer* pLocal, CBaseEntity* pTarget)
{
	if (!pLocal || !pTarget)
		return false;

	return F::NavEngine.IsVectorVisibleNavigation(pLocal->GetEyePosition(), pTarget->GetCenter(), MASK_SHOT | CONTENTS_GRATE);
}

bool CMVMController::IsSupportedClass(CTFPlayer* pLocal) const
{
	if (!pLocal)
		return false;
	const int iClass = pLocal->m_iClass();
	return iClass >= TF_CLASS_SCOUT && iClass <= TF_CLASS_ENGINEER;
}

bool CMVMController::PrimaryHasAmmo() const
{
	return SlotHasShot(SLOT_PRIMARY);
}

bool CMVMController::DesiredCombatWeaponCanFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) const
{
	if (!pLocal || !pWeapon)
		return false;

	if (pLocal->m_iClass() == TF_CLASS_SCOUT)
	{
		auto pSecondary = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
		if (IsMadMilk(pSecondary) && SlotHasShot(SLOT_SECONDARY))
			return true;

		return SlotHasShot(SLOT_PRIMARY);
	}

	return SlotHasShot(SLOT_PRIMARY);
}

bool CMVMController::GetTankTarget(CBaseEntity*& pOut) const
{
	pOut = nullptr;
	float flBestDistance = FLT_MAX;
	CTFPlayer* pLocal = H::Entities.GetLocal();
	const Vector vLocalOrigin = pLocal ? pLocal->GetAbsOrigin() : Vector();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
	{
		if (!pEntity || pEntity->IsDormant() || pEntity->GetClassID() != ETFClassID::CTFTankBoss)
			continue;

		const float flDistance = pLocal ? vLocalOrigin.DistTo(pEntity->GetAbsOrigin()) : 0.f;
		if (flDistance >= flBestDistance)
			continue;

		flBestDistance = flDistance;
		pOut = pEntity;
	}

	return pOut != nullptr;
}

bool CMVMController::GetRobotTarget(CTFPlayer* pLocal, CBaseEntity*& pOut) const
{
	pOut = nullptr;
	if (!pLocal)
		return false;

	float flBestDistance = FLT_MAX;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (!pEntity || !pEntity->IsPlayer())
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive() || pPlayer == pLocal || pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;

		Vector vOrigin;
		if (pEntity->IsDormant())
		{
			if (!F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
				continue;
		}
		else
		{
			vOrigin = pEntity->GetAbsOrigin();
		}

		const float flDistance = vLocalOrigin.DistTo(vOrigin);
		if (flDistance >= flBestDistance)
			continue;

		flBestDistance = flDistance;
		pOut = pEntity;
	}

	if (!pOut)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
		{
			if (!pEntity)
				continue;
			const ETFClassID iClass = pEntity->GetClassID();
			if (iClass != ETFClassID::CObjectSentrygun && iClass != ETFClassID::CObjectDispenser && iClass != ETFClassID::CObjectTeleporter)
				continue;
			if (iClass == ETFClassID::CObjectTeleporter)
			{
				auto pTele = pEntity->As<CObjectTeleporter>();
				if (pTele && pTele->m_iObjectMode() != 1)
					continue;
			}
			Vector vOrigin;
			if (pEntity->IsDormant())
			{
				if (!F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
					continue;
			}
			else
			{
				auto pObject = pEntity->As<CBaseObject>();
				if (!pObject || pObject->m_bCarried() || pObject->m_bBuilding() || pObject->m_bPlacing())
					continue;
				vOrigin = pEntity->GetAbsOrigin();
			}
			const float flDistance = vLocalOrigin.DistTo(vOrigin);
			if (flDistance >= flBestDistance)
				continue;
			flBestDistance = flDistance;
			pOut = pEntity;
		}
	}

	return pOut != nullptr;
}

bool CMVMController::GetMoneyTarget(CTFPlayer* pLocal, CBaseEntity*& pOut) const
{
	pOut = nullptr;
	if (!pLocal || pLocal->m_iClass() != TF_CLASS_SCOUT)
		return false;

	float flBestDistance = 1600.f;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PickupMoney))
	{
		if (!pEntity || pEntity->IsDormant())
			continue;

		auto pMoney = pEntity->As<CCurrencyPack>();
		if (pMoney->m_bDistributed())
			continue;

		const float flDistance = vLocalOrigin.DistTo(pEntity->GetAbsOrigin());
		if (flDistance >= flBestDistance)
			continue;

		flBestDistance = flDistance;
		pOut = pEntity;
	}

	return pOut != nullptr;
}

void CMVMController::RefreshSpawnAnchors(CTFPlayer* pLocal)
{
	if (!pLocal || !m_tAnchorRefresh.Run(2.f))
		return;

	m_vSpawnAnchors.clear();
	for (const auto& tRoom : F::NavEngine.GetRespawnRooms())
	{
		if (!tRoom.tData.m_vCenter.IsZero() && tRoom.m_iTeam != pLocal->m_iTeamNum())
			m_vSpawnAnchors.emplace_back(tRoom.tData.m_vCenter);
	}

	if (!F::NavEngine.IsNavMeshLoaded())
		return;

	const uint32_t uEnemySpawnFlag = pLocal->m_iTeamNum() == TF_TEAM_RED ? TF_NAV_SPAWN_ROOM_BLUE : TF_NAV_SPAWN_ROOM_RED;
	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		if (tArea.m_iTFAttributeFlags & uEnemySpawnFlag)
			m_vSpawnAnchors.emplace_back(tArea.m_vCenter);
	}

	if (m_vSpawnAnchors.empty())
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
		{
			if (pEntity && !pEntity->IsDormant())
				m_vSpawnAnchors.emplace_back(pEntity->GetCenter());
		}
	}
}

bool CMVMController::GetFrontlineTarget(CTFPlayer* pLocal, Vector& vOut)
{
	if (!pLocal)
		return false;

	RefreshSpawnAnchors(pLocal);

	CBaseEntity* pRobot = nullptr;
	if (GetRobotTarget(pLocal, pRobot) && pRobot)
	{
		vOut = pRobot->GetAbsOrigin();
		return true;
	}

	if (!m_vSpawnAnchors.empty())
	{
		const Vector vLocalOrigin = pLocal->GetAbsOrigin();
		float flBestDistance = FLT_MAX;
		for (const Vector& vAnchor : m_vSpawnAnchors)
		{
			const float flDistance = vLocalOrigin.DistTo(vAnchor);
			if (flDistance >= flBestDistance)
				continue;

			flBestDistance = flDistance;
			vOut = vAnchor;
		}
		return true;
	}

	return false;
}

bool CMVMController::RunTank(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTank)
{
	if (!pCmd || !pLocal || !pWeapon || !pTank)
		return false;

	m_eTask = MVMTaskEnum::Tank;
	if (F::BotUtils.m_iCurrentSlot != SLOT_PRIMARY)
		F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);

	const Vector vTarget = pTank->GetCenter();
	const float flDistance = pLocal->GetAbsOrigin().DistTo(vTarget);
	const float flRange = GetPrimaryRange(pLocal, pWeapon);
	pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), vTarget);

	if (flDistance > flRange * 0.85f)
		F::NavEngine.NavTo(pTank->GetAbsOrigin(), PriorityListEnum::MVMTank);
	else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMTank && F::NavEngine.IsPathing())
		F::NavEngine.CancelPath();

	if (F::BotUtils.m_iCurrentSlot == SLOT_PRIMARY && PrimaryHasAmmo() && flDistance <= flRange && IsVisibleToShoot(pLocal, pTank))
		pCmd->buttons |= IN_ATTACK;

	return true;
}

bool CMVMController::RunCombat(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget)
{
	if (!pCmd || !pLocal || !pWeapon || !pTarget)
		return false;

	m_eTask = MVMTaskEnum::Combat;
	const Vector vTarget = pTarget->GetCenter();
	const float flDistance = pLocal->GetAbsOrigin().DistTo(vTarget);
	const int iClass = pLocal->m_iClass();
	float flRange = GetPrimaryRange(pLocal, pWeapon);
	if (iClass == TF_CLASS_SCOUT || iClass == TF_CLASS_PYRO)
		flRange = 110.f;
	else if (iClass == TF_CLASS_SPY)
		flRange = 800.f;

	if (iClass == TF_CLASS_SCOUT)
	{
		auto pSecondary = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
		if (IsMadMilk(pSecondary) && SlotHasShot(SLOT_SECONDARY) && flDistance <= 620.f)
			F::BotUtils.SetSlot(pLocal, SLOT_SECONDARY);
		else if (iClass == TF_CLASS_SCOUT && flDistance <= 90.f)
			F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
		else
			F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
	}
	else if (iClass == TF_CLASS_SPY)
	{
		if (pTarget->IsBuilding())
		{
			auto pBuilding = pTarget->As<CBaseObject>();
			if (!pBuilding->m_bHasSapper() && flDistance < 130.f)
			{
				F::BotUtils.SetSlot(pLocal, SLOT_SECONDARY);
				pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), vTarget);
				if (flDistance < 90.f && IsVisibleToShoot(pLocal, pTarget))
					pCmd->buttons |= IN_ATTACK;
				if (flDistance > flRange * 0.8f)
					F::NavEngine.NavTo(pTarget->GetAbsOrigin(), PriorityListEnum::MVMCombat);
				return true;
			}
		}
		if (pTarget->IsPlayer())
		{
			auto pTargPlayer = pTarget->As<CTFPlayer>();
			Vec3 vFwd; Math::AngleVectors(pTargPlayer->GetEyeAngles(), &vFwd);
			vFwd.z = 0.f; Vec3 vToLocal = pLocal->GetAbsOrigin() - pTargPlayer->GetAbsOrigin(); vToLocal.z = 0.f;
			bool bBehind = vFwd.Normalize() > 0.01f && vToLocal.Normalize() > 0.01f && vFwd.Dot(vToLocal) < -0.3f;
			auto pKnife = pLocal->GetWeaponFromSlot(SLOT_MELEE);
			bool bReady = pKnife && pKnife->GetWeaponID() == TF_WEAPON_KNIFE && pKnife->As<CTFKnife>()->m_bReadyToBackstab();
			if ((bReady || bBehind) && flDistance < 200.f)
				F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
			else
				F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
		}
		else
			F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
	}
	else if (iClass == TF_CLASS_PYRO && flDistance <= 85.f)
	{
		F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
	}
	else
		F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);

	pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), vTarget);

	if (iClass == TF_CLASS_PYRO || iClass == TF_CLASS_SCOUT)
	{
		if (flDistance > 85.f)
			F::NavEngine.NavTo(pTarget->GetAbsOrigin(), PriorityListEnum::MVMCombat);
		else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMCombat && F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();
	}
	else
	{
		if (flDistance > flRange * 0.8f)
			F::NavEngine.NavTo(pTarget->GetAbsOrigin(), PriorityListEnum::MVMCombat);
		else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMCombat && F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();
	}

	if (F::BotUtils.m_iCurrentSlot == SLOT_SECONDARY && IsMadMilk(pWeapon) && G::CanPrimaryAttack && flDistance <= 620.f && IsVisibleToShoot(pLocal, pTarget))
		pCmd->buttons |= IN_ATTACK;
	else if (F::BotUtils.m_iCurrentSlot == SLOT_PRIMARY && PrimaryHasAmmo() && flDistance <= flRange && IsVisibleToShoot(pLocal, pTarget))
		pCmd->buttons |= IN_ATTACK;
	else if (F::BotUtils.m_iCurrentSlot == SLOT_MELEE && flDistance <= 90.f && IsVisibleToShoot(pLocal, pTarget))
		pCmd->buttons |= IN_ATTACK;

	return true;
}

bool CMVMController::RunMoney(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseEntity* pMoney)
{
	if (!pCmd || !pLocal || !pMoney)
		return false;

	m_eTask = MVMTaskEnum::Money;
	const Vector vOrigin = pMoney->GetAbsOrigin();
	const float flDistance = pLocal->GetAbsOrigin().DistTo(vOrigin);
	if (flDistance <= 65.f)
	{
		SDK::WalkTo(pCmd, pLocal, vOrigin);
		return true;
	}

	return F::NavEngine.NavTo(vOrigin, PriorityListEnum::MVMMoney);
}

bool CMVMController::RunFrontline(CTFPlayer* pLocal)
{
	if (!pLocal)
		return false;

	Vector vTarget = {};
	bool bHasTarget = GetFrontlineTarget(pLocal, vTarget) && !vTarget.IsZero();
	if (!bHasTarget)
	{
		RefreshSpawnAnchors(pLocal);
		if (!m_vSpawnAnchors.empty())
		{
			vTarget = m_vSpawnAnchors.front();
			bHasTarget = true;
		}
		else if (auto pArea = F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin()))
		{
			vTarget = pArea->m_vCenter;
			Vector vFwd; Math::AngleVectors(pLocal->GetEyeAngles(), &vFwd);
			vFwd.z = 0.f; vFwd.Normalize();
			vTarget += vFwd * 600.f;
			if (auto pNear = F::NavEngine.FindClosestNavArea(vTarget, false))
				vTarget = pNear->m_vCenter;
			bHasTarget = true;
		}
	}
	if (!bHasTarget || vTarget.IsZero())
		return false;

	m_eTask = MVMTaskEnum::Frontline;
	const float flDistance = pLocal->GetAbsOrigin().DistTo(vTarget);
	if (flDistance < 120.f)
		return true;

	return F::NavEngine.NavTo(vTarget, PriorityListEnum::MVMFrontline);
}

void CMVMController::Update()
{
	auto pGameRules = I::TFGameRules();
	m_bActive = pGameRules && pGameRules->m_bPlayingMannVsMachine();
	if (!m_bActive)
		m_eTask = MVMTaskEnum::None;
}

void CMVMController::Reset()
{
	m_bActive = false;
	m_eTask = MVMTaskEnum::None;
	m_vSpawnAnchors.clear();
}

bool CMVMController::WantsPrimary(CTFPlayer* pLocal) const
{
	if (!m_bActive || !IsSupportedClass(pLocal))
		return false;

	return m_eTask == MVMTaskEnum::Tank || pLocal->m_iClass() == TF_CLASS_PYRO || pLocal->m_iClass() == TF_CLASS_SNIPER;
}

bool CMVMController::WantsScoutSecondary(CTFPlayer* pLocal) const
{
	if (!m_bActive || !pLocal || pLocal->m_iClass() != TF_CLASS_SCOUT || m_eTask == MVMTaskEnum::Tank)
		return false;

	auto pSecondary = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
	return IsMadMilk(pSecondary) && SlotHasShot(SLOT_SECONDARY);
}

bool CMVMController::Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_eTask = MVMTaskEnum::None;
	if (!m_bActive || !IsSupportedClass(pLocal) || !pCmd || !pWeapon)
		return false;

	const float flHealth = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
	const int iClass = pLocal->m_iClass();
	const bool bScoutPyro = iClass == TF_CLASS_SCOUT || iClass == TF_CLASS_PYRO;
	const bool bSpy = iClass == TF_CLASS_SPY;
	const bool bFrontline = iClass == TF_CLASS_SNIPER || iClass == TF_CLASS_HEAVYWEAPONS || iClass == TF_CLASS_DEMOMAN || iClass == TF_CLASS_SOLDIER;

	if (bScoutPyro || bSpy)
	{
		if (flHealth < 0.56f)
		{
			if (F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::Forced))
			{
				m_eTask = MVMTaskEnum::Health;
				return true;
			}
			if (F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced))
			{
				m_eTask = MVMTaskEnum::Ammo;
				return true;
			}
		}
	}

	CBaseEntity* pTank = nullptr;
	if (GetTankTarget(pTank))
		return RunTank(pCmd, pLocal, pWeapon, pTank);

	CBaseEntity* pRobot = nullptr;
	if (GetRobotTarget(pLocal, pRobot))
		return RunCombat(pCmd, pLocal, pWeapon, pRobot);

	CBaseEntity* pMoney = nullptr;
	if (GetMoneyTarget(pLocal, pMoney) && RunMoney(pCmd, pLocal, pMoney))
		return true;

	if (bFrontline)
	{
		if (flHealth < 0.78f && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::Forced))
		{
			m_eTask = MVMTaskEnum::Health;
			return true;
		}
		if (!DesiredCombatWeaponCanFire(pLocal, pWeapon) && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced))
		{
			m_eTask = MVMTaskEnum::Ammo;
			return true;
		}
	}
	else if (!bScoutPyro && !bSpy)
	{
		if (flHealth < 0.35f && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::Forced))
		{
			m_eTask = MVMTaskEnum::Health;
			return true;
		}
		if (!DesiredCombatWeaponCanFire(pLocal, pWeapon) && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced))
		{
			m_eTask = MVMTaskEnum::Ammo;
			return true;
		}
	}

	return RunFrontline(pLocal);
}
