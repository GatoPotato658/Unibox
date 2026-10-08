#include "Objectives.h"
#include "NavEngine.h"
#include "Jobs/NavBotJobs.h"
#include "BotUtils.h"

namespace ObjectiveUtils
{
	Vector GetObjectiveOrigin(CBaseEntity* pEntity)
	{
		if (!pEntity)
			return {};

		Vector vPos = pEntity->GetCenter();
		if (!vPos.IsZero())
			return vPos;

		vPos = pEntity->GetAbsOrigin();
		return vPos.IsZero() ? pEntity->m_vecOrigin() : vPos;
	}

	Vector AdjustPosToNav(Vector vPos)
	{
		if (!F::NavEngine.IsNavMeshLoaded())
			return vPos;

		if (auto pArea = F::NavEngine.FindClosestNavArea(vPos, false))
		{
			Vector vCorrected = pArea->GetNearestPoint(vPos.Get2D());
			vCorrected.z = pArea->GetZ(vCorrected.x, vCorrected.y);
			return vCorrected;
		}

		return vPos;
	}

	bool GetTeamSpawnCenter(int iTeam, Vector& vOut)
	{
		Vector vSum = {};
		int iCount = 0;
		for (const auto& tRoom : F::NavEngine.GetRespawnRooms())
		{
			if (tRoom.tData.m_vCenter.IsZero() || (tRoom.m_iTeam != 0 && tRoom.m_iTeam != iTeam))
				continue;

			vSum += tRoom.tData.m_vCenter;
			iCount++;
		}
		if (iCount > 0)
		{
			vOut = vSum / static_cast<float>(iCount);
			return true;
		}

		const uint32_t uFlag = iTeam == TF_TEAM_RED ? TF_NAV_SPAWN_ROOM_RED : iTeam == TF_TEAM_BLUE ? TF_NAV_SPAWN_ROOM_BLUE : 0;
		auto pNavFile = F::NavEngine.GetNavFile();
		if (!uFlag || !F::NavEngine.IsNavMeshLoaded() || !pNavFile)
			return false;

		for (const auto& tArea : pNavFile->m_vAreas)
		{
			if (!(tArea.m_iTFAttributeFlags & uFlag))
				continue;

			vSum += tArea.m_vCenter;
			iCount++;
		}
		if (!iCount)
			return false;

		vOut = vSum / static_cast<float>(iCount);
		return true;
	}

	void CollectCaptureZones(std::vector<CaptureZone_t>& vOut)
	{
		vOut.clear();
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
		{
			if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureZone)
				continue;

			auto pZone = pEntity->As<CCaptureZone>();
			if (pZone->m_bDisabled())
				continue;

			const Vector vOrigin = pZone->GetAbsOrigin();
			CaptureZone_t tZone{};
			tZone.m_pZone = pZone;
			tZone.m_iTeam = pZone->m_iTeamNum();
			tZone.m_vMins = vOrigin + pZone->m_vecMins();
			tZone.m_vMaxs = vOrigin + pZone->m_vecMaxs();
			tZone.m_vCenter = (tZone.m_vMins + tZone.m_vMaxs) * 0.5f;
			vOut.push_back(tZone);
		}
	}

	bool FindZoneStandPos(const CaptureZone_t& tZone, const Vector& vFrom, Vector& vOut)
	{
		if (!F::NavEngine.IsNavMeshLoaded())
		{
			vOut = tZone.m_vCenter;
			return true;
		}

		CNavArea* pBest = nullptr;
		float flBestDist = FLT_MAX;
		if (auto pNavFile = F::NavEngine.GetNavFile())
		{
			for (auto& tArea : pNavFile->m_vAreas)
			{
				if (tArea.m_vCenter.z < tZone.m_vMins.z - 64.f || tArea.m_vCenter.z > tZone.m_vMaxs.z + 16.f
					|| !tZone.Contains(Vector(tArea.m_vCenter.x, tArea.m_vCenter.y, tZone.m_vCenter.z)))
					continue;

				const float flDist = tArea.m_vCenter.DistToSqr(vFrom);
				if (flDist < flBestDist)
				{
					flBestDist = flDist;
					pBest = &tArea;
				}
			}
		}

		if (!pBest)
			pBest = F::NavEngine.FindClosestNavArea(tZone.m_vCenter, false);
		if (!pBest)
			return false;

		vOut = pBest->m_vCenter;
		return true;
	}

	std::vector<float> GetPathCosts(const std::vector<Vector>& vPositions)
	{
		std::vector<float> vCosts(vPositions.size(), FLT_MAX);
		auto pStart = F::NavEngine.GetLocalNavArea();
		if (vPositions.empty() || !pStart || !F::NavEngine.IsNavMeshLoaded())
			return vCosts;

		std::vector<CNavArea*> vTargets;
		vTargets.reserve(vPositions.size());
		for (const auto& vPos : vPositions)
			vTargets.push_back(F::NavEngine.FindClosestNavArea(vPos, false));

		std::vector<float> vField;
		if (!F::NavEngine.GetPathCostField(pStart, vField, FLT_MAX, &vTargets))
			return vCosts;

		for (size_t i = 0; i < vTargets.size(); i++)
		{
			if (!vTargets[i])
				continue;

			const float flCost = F::NavEngine.GetFieldCost(vField, vTargets[i]);
			if (std::isfinite(flCost) && flCost < FLT_MAX * 0.5f)
				vCosts[i] = flCost;
		}
		return vCosts;
	}

	int PickNearest(const std::vector<Vector>& vPositions, const Vector& vFrom, bool bPathCosts, float flMaxPathCost)
	{
		const auto vCosts = bPathCosts ? GetPathCosts(vPositions) : std::vector<float>{};
		int iBest = -1;
		float flBest = FLT_MAX;
		for (size_t i = 0; i < vPositions.size(); i++)
		{
			const float flCost = bPathCosts ? vCosts[i] : vPositions[i].DistToSqr(vFrom);
			if (flCost < flBest && (!bPathCosts || flCost <= flMaxPathCost))
			{
				flBest = flCost;
				iBest = static_cast<int>(i);
			}
		}
		return iBest;
	}
}

bool CaptureZone_t::Contains(const Vector& vPos, float flPad) const
{
	return vPos.x >= m_vMins.x - flPad && vPos.x <= m_vMaxs.x + flPad
		&& vPos.y >= m_vMins.y - flPad && vPos.y <= m_vMaxs.y + flPad
		&& vPos.z >= m_vMins.z - flPad && vPos.z <= m_vMaxs.z + flPad;
}

namespace
{
	enum ControllerEnum : uint32_t
	{
		Controller_Flag = 1 << 0,
		Controller_CP = 1 << 1,
		Controller_PL = 1 << 2,
		Controller_Passtime = 1 << 3,
		Controller_PD = 1 << 4,
		Controller_RD = 1 << 5,
		Controller_Doomsday = 1 << 6,
		Controller_Haarp = 1 << 7,
		Controller_ZI = 1 << 8
	};

	uint32_t GetControllerMask(ETFGameType eMode, bool bDoomsday, bool bHaarp, bool bZombieInfection)
	{
		if (bZombieInfection)
			return Controller_ZI;

		uint32_t uMask = 0;
		switch (eMode)
		{
		case TF_GAMETYPE_CTF: uMask = Controller_Flag; break;
		case TF_GAMETYPE_CP:
		case TF_GAMETYPE_ARENA: uMask = Controller_CP; break;
		case TF_GAMETYPE_ESCORT: uMask = Controller_PL | Controller_CP; break;
		case TF_GAMETYPE_PASSTIME: uMask = Controller_Passtime; break;
		case TF_GAMETYPE_PD: uMask = Controller_PD | Controller_CP; break;
		case TF_GAMETYPE_RD: uMask = Controller_RD; break;
		default: uMask = Controller_Flag | Controller_CP; break;
		}

		if (bDoomsday)
			uMask |= Controller_Flag | Controller_CP | Controller_Doomsday;
		if (bHaarp)
			uMask |= Controller_Flag | Controller_CP | Controller_Haarp;
		return uMask;
	}
}

void CGameObjectiveController::RefreshGameType()
{
	const std::string sMapName = SDK::GetLevelName();
	m_bDoomsday = sMapName.find("sd_doomsday") != std::string::npos;
	m_bHaarp = sMapName.find("ctf_haarp") != std::string::npos;
	m_bPayloadHybrid = sMapName.starts_with("cppl_");
	m_bTugOfWar = sMapName.starts_with("tow_");
	m_bZombieInfection = sMapName.starts_with("zi_");
	m_bVSH = sMapName.starts_with("vsh_");

	auto pGameRules = I::TFGameRules();
	m_eGameMode = pGameRules ? static_cast<ETFGameType>(pGameRules->m_nGameType()) : TF_GAMETYPE_UNDEFINED;
}

void CGameObjectiveController::Update()
{
	if (I::GlobalVars->curtime >= m_flNextRefresh)
	{
		RefreshGameType();
		m_flNextRefresh = I::GlobalVars->curtime + 1.f;
	}

	F::MVMController.Update();
	if (F::MVMController.IsActive())
		return;

	uint32_t uMask = GetControllerMask(m_eGameMode, m_bDoomsday, m_bHaarp, m_bZombieInfection);
	if (m_bVSH)
		uMask |= Controller_CP;
	if (uMask & Controller_Flag)
		F::FlagController.Update();
	if (uMask & Controller_CP)
		F::CPController.Update();
	if (uMask & Controller_PL)
		F::PLController.Update();
	if (uMask & Controller_Passtime)
		F::PasstimeController.Update();
	if (uMask & Controller_PD)
		F::PDController.Update();
	if (uMask & Controller_RD)
		F::RDController.Update();
	if (uMask & Controller_ZI)
		F::ZIController.Update();
	if (m_bVSH)
		F::VSHController.Update();
}

void CGameObjectiveController::Reset()
{
	m_flNextRefresh = 0.f;
	m_eGameMode = TF_GAMETYPE_UNDEFINED;
	m_bDoomsday = false;
	m_bHaarp = false;
	m_bPayloadHybrid = false;
	m_bTugOfWar = false;
	m_bZombieInfection = false;
	m_bVSH = false;
	F::FlagController.Init();
	F::PLController.Init();
	F::CPController.Init();
	F::PasstimeController.Init();
	F::DoomsdayController.Init();
	F::HaarpController.Init();
	F::PDController.Init();
	F::RDController.Init();
	F::MVMController.Reset();
	F::ZIController.Reset();
	F::VSHController.Reset();
}

bool CGameObjectiveController::GetBattleAnchor(int iTeam, Vector& vOut) const
{
	Vector vOwn, vEnemy;
	if (!ObjectiveUtils::GetTeamSpawnCenter(iTeam, vOwn)
		|| !ObjectiveUtils::GetTeamSpawnCenter(iTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE, vEnemy))
		return false;

	vOut = ObjectiveUtils::AdjustPosToNav((vOwn + vEnemy) * 0.5f);
	return true;
}

FlagInfo CFlagController::GetFlag(int iTeam)
{
	for (const auto& tFlag : m_vFlags)
	{
		if (tFlag.m_pFlag && tFlag.m_iTeam == iTeam)
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
	if (!tFlag.m_pFlag)
		return false;

	vOut = GetPosition(tFlag.m_pFlag);
	return true;
}

bool CFlagController::GetSpawnPosition(int iTeam, Vector& vOut)
{
	auto tFlag = GetFlag(iTeam);
	if (!tFlag.m_pFlag)
		return false;

	auto it = m_mSpawnPositions.find(tFlag.m_pFlag->entindex());
	if (it == m_mSpawnPositions.end())
		return false;

	vOut = it->second;
	return true;
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
	return tFlag.m_pFlag ? GetCarrier(tFlag.m_pFlag) : -1;
}

int CFlagController::GetStatus(CCaptureFlag* pFlag)
{
	return pFlag->m_nFlagStatus();
}

int CFlagController::GetStatus(int iTeam)
{
	auto tFlag = GetFlag(iTeam);
	return tFlag.m_pFlag ? GetStatus(tFlag.m_pFlag) : TF_FLAGINFO_HOME;
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
		const int iType = pFlag->m_nType();
		if (iType == TF_FLAGTYPE_PLAYER_DESTRUCTION || iType == TF_FLAGTYPE_ROBOT_DESTRUCTION)
			continue;

		if (pFlag->m_nFlagStatus() == TF_FLAGINFO_HOME)
			m_mSpawnPositions[pFlag->entindex()] = pFlag->GetAbsOrigin();

		m_vFlags.emplace_back(pFlag, pFlag->m_iTeamNum());

		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(pFlag->GetAbsOrigin(), 50.f, 20, 20, I::GlobalVars->curtime + 0.1f, Color_t(255, 255, 255, 10), Color_t(255, 255, 255, 100));
	}
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

	for (int i = iNumControlPoints; i < MAX_CONTROL_POINTS; ++i)
		m_aControlPointData[i] = CPInfo();

	for (int i = 0; i < iNumControlPoints; ++i)
	{
		auto& tData = m_aControlPointData[i];
		tData.m_iIdx = i;
		tData.m_vPos = m_pObjectiveResource->m_vCPPositions(i);
		tData.m_bGotPos = true;
		tData.m_bPushPoint = std::max(m_pObjectiveResource->m_flTeamCapTime(i + TF_TEAM_RED * MAX_CONTROL_POINTS),
			m_pObjectiveResource->m_flTeamCapTime(i + TF_TEAM_BLUE * MAX_CONTROL_POINTS)) >= PUSH_POINT_CAP_TIME;
	}

	if (F::GameObjectiveController.m_bTugOfWar || !m_tCapStatusRefresh.Run(1.f))
		return;

	m_iIgnoredPoint = -1;
	const std::string sLevelName = SDK::GetLevelName();
	if (sLevelName.find("cp_steel") != std::string::npos)
		m_iIgnoredPoint = 4;

	for (int i = 0; i < iNumControlPoints; ++i)
	{
		auto& tData = m_aControlPointData[i];
		for (int iTeamIdx = 0; iTeamIdx < 2; ++iTeamIdx)
		{
			tData.m_bCanCap[iTeamIdx] = EvaluatePoint(i, TF_TEAM_RED + iTeamIdx, false);
			tData.m_bCanCapSoon[iTeamIdx] = EvaluatePoint(i, TF_TEAM_RED + iTeamIdx, true);
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
	const int iOwnedEnd = m_pObjectiveResource->m_iBaseControlPoints(iTeam);
	const int iNumControlPoints = std::clamp(m_pObjectiveResource->m_iNumControlPoints(), 0, MAX_CONTROL_POINTS);
	if (iOwnedEnd < 0 || iOwnedEnd >= iNumControlPoints)
		return -1;

	const int iWalk = iOwnedEnd != 0 ? -1 : 1;
	const int iEnemyEnd = iOwnedEnd != 0 ? 0 : iNumControlPoints - 1;

	int iFarthestPoint = iOwnedEnd;
	for (int iPoint = iOwnedEnd; iPoint != iEnemyEnd; iPoint += iWalk)
	{
		if (m_pObjectiveResource->m_iOwner(iPoint) != iTeam)
			break;

		iFarthestPoint = iPoint;
	}

	return iFarthestPoint;
}

bool CCPController::EvaluatePoint(int iIndex, int iTeam, bool bPending)
{
	if (!m_pObjectiveResource || iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
		return false;

	const int iNumControlPoints = std::clamp(m_pObjectiveResource->m_iNumControlPoints(), 0, MAX_CONTROL_POINTS);
	if (iIndex < 0 || iIndex >= iNumControlPoints)
		return false;

	if (m_pObjectiveResource->m_iOwner(iIndex) == iTeam || !TeamCanCapPoint(iIndex, iTeam))
		return false;

	if (m_pObjectiveResource->m_bPlayingMiniRounds() && !m_pObjectiveResource->m_bInMiniRound(iIndex))
		return false;

	static auto tf_caplinear = H::ConVars.FindVar("tf_caplinear");
	if (!tf_caplinear || !tf_caplinear->GetBool())
		return true;

	int iPointNeeded = GetPreviousPointForPoint(iIndex, iTeam, 0);
	if (iPointNeeded == iIndex)
		return true;

	if (!bPending && m_pObjectiveResource->m_bCPLocked(iIndex))
		return false;

	const bool bArena = F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_ARENA;
	if (iNumControlPoints == 1 && !bArena)
		return true;

	if (iPointNeeded == -1)
	{
		if (bArena)
		{
			if (bPending)
				return true;

			auto pGameRules = I::TFGameRules();
			return pGameRules && pGameRules->m_iRoundState() == GR_STATE_STALEMATE
				&& pGameRules->m_flCapturePointEnableTime() <= I::GlobalVars->curtime;
		}

		if (m_pObjectiveResource->m_bPlayingMiniRounds())
			return true;

		return abs(GetFarthestOwnedControlPoint(iTeam) - iIndex) <= 1;
	}

	for (int iPrevPoint = 0; iPrevPoint < MAX_PREVIOUS_POINTS; iPrevPoint++)
	{
		iPointNeeded = GetPreviousPointForPoint(iIndex, iTeam, iPrevPoint);
		if (iPointNeeded != -1 && m_pObjectiveResource->m_iOwner(iPointNeeded) != iTeam)
			return false;
	}
	return true;
}

bool CCPController::IsPointUseable(int iIndex, int iTeam)
{
	return EvaluatePoint(iIndex, iTeam, false);
}

bool CCPController::IsCapturing(int iIndex, int iTeam) const
{
	return m_pObjectiveResource && iIndex >= 0 && iIndex < MAX_CONTROL_POINTS
		&& m_pObjectiveResource->m_iCappingTeam(iIndex) == iTeam;
}

bool CCPController::HasControlPoints() const
{
	return m_pObjectiveResource && m_pObjectiveResource->m_iNumControlPoints() > 0;
}

bool CCPController::GetClosestControlPointInfo(Vector vPos, int iTeam, std::pair<int, Vector>& tOut, bool bIncludePending)
{
	const int iTeamIdx = iTeam - TF_TEAM_RED;
	if (!m_pObjectiveResource || iTeamIdx < 0 || iTeamIdx > 1 || !m_pObjectiveResource->m_iNumControlPoints())
		return false;

	int iBestIndex = -1;
	Vector vBestPoint = {};
	float flBestDist = FLT_MAX;
	for (const auto& tControlPoint : m_aControlPointData)
	{
		if (tControlPoint.m_iIdx < 0 || tControlPoint.m_iIdx == m_iIgnoredPoint || !tControlPoint.m_bGotPos || tControlPoint.m_bPushPoint)
			continue;

		if (!(bIncludePending ? tControlPoint.m_bCanCapSoon[iTeamIdx] : tControlPoint.m_bCanCap[iTeamIdx]))
			continue;

		const float flDist = tControlPoint.m_vPos.DistToSqr(vPos);
		if (flDist < flBestDist)
		{
			flBestDist = flDist;
			vBestPoint = tControlPoint.m_vPos;
			iBestIndex = tControlPoint.m_iIdx;
		}
	}

	if (iBestIndex == -1)
		return false;

	tOut = { iBestIndex, vBestPoint };
	return true;
}

bool CCPController::GetClosestControlPoint(Vector vPos, int iTeam, Vector& vOut)
{
	std::pair<int, Vector> tInfo;
	if (!GetClosestControlPointInfo(vPos, iTeam, tInfo, true))
		return false;

	vOut = tInfo.second;
	return true;
}

void CCPController::Init()
{
	for (auto& tData : m_aControlPointData)
		tData = CPInfo();

	m_pObjectiveResource = nullptr;
	m_iIgnoredPoint = -1;
	m_tCapStatusRefresh -= 10.f;
}

void CCPController::Update()
{
	UpdateObjectiveResource();
	UpdateControlPoints();
}

void CPLController::Init()
{
	for (auto& vPayloads : m_aPayloads)
		vPayloads.clear();
	m_iTugPoint = -1;
}

void CPLController::Update()
{
	Init();

	const bool bTugOfWar = F::GameObjectiveController.m_bTugOfWar;
	for (auto pPayload : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pPayload || pPayload->GetClassID() != ETFClassID::CObjectCartDispenser)
			continue;

		auto pCart = pPayload->As<CObjectCartDispenser>();
		if (bTugOfWar)
		{
			for (auto& vPayloads : m_aPayloads)
				vPayloads.push_back(pCart);
			continue;
		}

		const int iTeam = pPayload->m_iTeamNum();
		if (iTeam >= TF_TEAM_RED && iTeam <= TF_TEAM_BLUE)
			m_aPayloads[iTeam - TF_TEAM_RED].push_back(pCart);
	}

	auto pResource = bTugOfWar ? H::Entities.GetObjectiveResource() : nullptr;
	if (!pResource)
		return;

	const int iNumControlPoints = std::clamp(pResource->m_iNumControlPoints(), 0, MAX_CONTROL_POINTS);
	for (int i = 0; i < iNumControlPoints; ++i)
	{
		if (pResource->m_bTeamCanCap(i + TF_TEAM_RED * MAX_CONTROL_POINTS) && pResource->m_bTeamCanCap(i + TF_TEAM_BLUE * MAX_CONTROL_POINTS))
		{
			m_iTugPoint = i;
			break;
		}
	}
}

int CPLController::GetTugOwner() const
{
	auto pResource = H::Entities.GetObjectiveResource();
	return pResource && m_iTugPoint >= 0 ? pResource->m_iOwner(m_iTugPoint) : TEAM_UNASSIGNED;
}

int CPLController::GetTugPushers(int iTeam) const
{
	auto pResource = H::Entities.GetObjectiveResource();
	return pResource && m_iTugPoint >= 0 ? pResource->m_iNumTeamMembers(m_iTugPoint + iTeam * MAX_CONTROL_POINTS) : 0;
}

CObjectCartDispenser* CPLController::GetClosestPayload(Vector vPos, int iTeam)
{
	if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
		return nullptr;

	float flMinDist = FLT_MAX;
	CObjectCartDispenser* pBestEnt = nullptr;
	for (auto pEntity : m_aPayloads[iTeam - TF_TEAM_RED])
	{
		if (!pEntity || pEntity->IsDormant())
			continue;

		const float flDist = pEntity->GetAbsOrigin().DistToSqr(vPos);
		if (flDist < flMinDist)
		{
			pBestEnt = pEntity;
			flMinDist = flDist;
		}
	}

	return pBestEnt;
}

void CMissionBoard::Reset()
{
	m_tMission = {};
	m_flUrgency = 0.f;
	m_iFriendliesNear = 0;
	m_iEnemiesNear = 0;
	m_sStatus = L"";
}

void CMissionBoard::SetMission(MissionKindEnum::MissionKindEnum eKind, const Vector& vPos, float flValue, float flScore, int iCarrierIdx, const wchar_t* sStatus)
{
	m_tMission.m_eKind = eKind;
	m_tMission.m_vPos = vPos;
	m_tMission.m_flValue = flValue;
	m_tMission.m_flScore = flScore;
	m_tMission.m_iCarrierIdx = iCarrierIdx;
	m_tMission.m_bValid = true;
	m_sStatus = sStatus;
}

void CMissionBoard::CountForces(CTFPlayer* pLocal, const Vector& vPos)
{
	constexpr float flRadius = 900.f;
	m_iFriendliesNear = 0;
	m_iEnemiesNear = 0;
	const int iLocalIdx = pLocal ? pLocal->entindex() : -1;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		if (!pEntity || pEntity->entindex() == iLocalIdx || pEntity->IsDormant())
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->IsAlive() && pPlayer->GetAbsOrigin().DistTo(vPos) <= flRadius)
			m_iFriendliesNear++;
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (!pEntity || pEntity->IsDormant())
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->IsAlive() && pPlayer->GetAbsOrigin().DistTo(vPos) <= flRadius)
			m_iEnemiesNear++;
	}
}

void CMissionBoard::UpdateCtf(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam)
{
	const int iLocalIdx = pLocal->entindex();
	const int iCarrierIdx = F::FlagController.GetCarrier(iEnemyTeam);
	Vector vPos = {};

	if (iCarrierIdx == iLocalIdx)
	{
		if (F::FlagController.GetSpawnPosition(iOurTeam, vPos) || F::FlagController.GetPosition(iEnemyTeam, vPos))
			SetMission(MissionKindEnum::CtfCarry, vPos, 1.f, 1700.f, iLocalIdx, L"CTF carry");
		return;
	}

	if (iCarrierIdx > 0 && F::BotUtils.GetDormantOrigin(iCarrierIdx, &vPos) && F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
	{
		SetMission(MissionKindEnum::CtfEscort, vPos, 0.8f, 880.f, iCarrierIdx, L"CTF escort");
		return;
	}

	if (F::FlagController.GetPosition(iEnemyTeam, vPos))
		SetMission(MissionKindEnum::CtfSteal, vPos, 0.6f, 520.f, -1, L"CTF steal");
}

void CMissionBoard::UpdateCp(CTFPlayer* pLocal, int iOurTeam)
{
	std::pair<int, Vector> tInfo{};
	if (F::CPController.GetClosestControlPointInfo(pLocal->GetAbsOrigin(), iOurTeam, tInfo, true))
		SetMission(MissionKindEnum::ControlPoint, tInfo.second, 0.6f, 520.f, -1, L"CP");
}

void CMissionBoard::UpdatePayload(CTFPlayer* pLocal, int iOurTeam)
{
	if (F::GameObjectiveController.m_bPayloadHybrid)
	{
		UpdateCp(pLocal, iOurTeam);
		if (m_tMission.m_bValid)
			return;
	}

	if (auto pPayload = F::PLController.GetClosestPayload(pLocal->GetAbsOrigin(), iOurTeam))
		SetMission(MissionKindEnum::Payload, pPayload->GetAbsOrigin(), 0.7f, 520.f, -1, L"Payload");
}

void CMissionBoard::UpdatePasstime(CTFPlayer* pLocal, int iOurTeam)
{
	const int iLocalIdx = pLocal->entindex();
	const int iCarrierIdx = F::PasstimeController.GetCarrier();
	Vector vPos = {};

	if (pLocal->m_bHasPasstimeBall() || iCarrierIdx == iLocalIdx)
	{
		if (F::PasstimeController.GetGoalPos(iOurTeam, pLocal->GetAbsOrigin(), vPos) || F::PasstimeController.GetBallPos(vPos))
			SetMission(MissionKindEnum::Passtime, vPos, 0.9f, 1750.f, iLocalIdx, L"Passtime run");
		return;
	}

	if (iCarrierIdx > 0 && F::BotUtils.GetDormantOrigin(iCarrierIdx, &vPos))
	{
		SetMission(MissionKindEnum::Passtime, vPos, 0.7f, 900.f, iCarrierIdx, L"Passtime chase");
		return;
	}

	if (F::PasstimeController.GetBallPos(vPos))
		SetMission(MissionKindEnum::Passtime, vPos, 0.5f, 760.f, -1, L"Passtime ball");
}

void CMissionBoard::UpdatePlayerDestruction(CTFPlayer* pLocal)
{
	PDGoal_t tGoal;
	if (!F::PDController.GetGoal(pLocal, false, tGoal))
		return;

	switch (tGoal.m_eKind)
	{
	case PDGoalEnum::Deliver:
		SetMission(MissionKindEnum::PlayerDestruction, tGoal.m_vPos, 1.f, 1100.f + std::min(tGoal.m_iCarried, 10) * 40.f, pLocal->entindex(), L"PD deliver");
		break;
	case PDGoalEnum::Collect:
		SetMission(MissionKindEnum::PlayerDestruction, tGoal.m_vPos, 0.7f, 850.f, -1, L"PD collect");
		break;
	case PDGoalEnum::Hunt:
		SetMission(MissionKindEnum::PlayerDestruction, tGoal.m_vPos, 0.5f, 620.f, tGoal.m_iTargetIdx, L"PD leader");
		break;
	case PDGoalEnum::Escort:
		SetMission(MissionKindEnum::PlayerDestruction, tGoal.m_vPos, 0.4f, 560.f, tGoal.m_iTargetIdx, L"PD escort");
		break;
	default:
		break;
	}
}

void CMissionBoard::UpdateRobotDestruction(CTFPlayer* pLocal)
{
	RDGoal_t tGoal;
	if (!F::RDController.GetGoal(pLocal, false, tGoal))
		return;

	switch (tGoal.m_eKind)
	{
	case RDGoalEnum::Deliver:
		SetMission(MissionKindEnum::RobotDestruction, tGoal.m_vPos, 1.f, 1200.f, pLocal->entindex(), L"RD deliver");
		break;
	case RDGoalEnum::Collect:
		SetMission(MissionKindEnum::RobotDestruction, tGoal.m_vPos, 0.7f, 800.f, -1, L"RD collect");
		break;
	case RDGoalEnum::Attack:
		SetMission(MissionKindEnum::RobotDestruction, tGoal.m_vPos, 0.5f, 600.f, -1, L"RD robot");
		break;
	default:
		break;
	}
}

void CMissionBoard::Update(CTFPlayer* pLocal)
{
	static Timer tThrottle{};
	if (!tThrottle.Run(0.3f))
		return;

	m_tMission = {};
	m_flUrgency = 0.f;
	m_iFriendliesNear = 0;
	m_iEnemiesNear = 0;
	m_sStatus = L"";
	if (!pLocal)
		return;

	const auto pGameRules = I::TFGameRules();
	if (!pGameRules || pGameRules->m_bInWaitingForPlayers())
		return;

	const int iRoundState = pGameRules->m_iRoundState();
	if (iRoundState != GR_STATE_RND_RUNNING && iRoundState != GR_STATE_STALEMATE)
		return;

	if (F::MVMController.IsActive())
	{
		m_tMission.m_eKind = MissionKindEnum::MVM;
		m_sStatus = L"MvM";
		return;
	}

	const int iOurTeam = pLocal->m_iTeamNum();
	const int iEnemyTeam = iOurTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
	const auto& tObjectives = F::GameObjectiveController;

	if (tObjectives.m_bZombieInfection)
		UpdateZombieInfection();
	else switch (tObjectives.m_eGameMode)
	{
	case TF_GAMETYPE_CTF:
		UpdateCtf(pLocal, iOurTeam, iEnemyTeam);
		break;
	case TF_GAMETYPE_CP:
	case TF_GAMETYPE_ARENA:
		UpdateCp(pLocal, iOurTeam);
		break;
	case TF_GAMETYPE_ESCORT:
		UpdatePayload(pLocal, iOurTeam);
		break;
	case TF_GAMETYPE_PASSTIME:
		UpdatePasstime(pLocal, iOurTeam);
		break;
	case TF_GAMETYPE_PD:
		UpdatePlayerDestruction(pLocal);
		if (!m_tMission.m_bValid)
			UpdateCp(pLocal, iOurTeam);
		break;
	case TF_GAMETYPE_RD:
		UpdateRobotDestruction(pLocal);
		break;
	default:
		if (tObjectives.m_bDoomsday)
		{
			UpdateCtf(pLocal, iOurTeam, iEnemyTeam);
			if (m_tMission.m_bValid)
			{
				m_tMission.m_eKind = MissionKindEnum::Doomsday;
				m_tMission.m_flValue = std::max(m_tMission.m_flValue, 0.8f);
				m_sStatus = F::DoomsdayController.m_sDoomsdayStatus.empty() ? std::wstring(L"Doomsday") : F::DoomsdayController.m_sDoomsdayStatus;
			}
		}
		else
			UpdateCp(pLocal, iOurTeam);
		break;
	}

	if (!m_tMission.m_bValid)
		return;

	CountForces(pLocal, m_tMission.m_vPos);
	m_flUrgency = m_tMission.m_flValue;
	if (m_iEnemiesNear > m_iFriendliesNear &&
		(m_tMission.m_eKind == MissionKindEnum::ControlPoint || m_tMission.m_eKind == MissionKindEnum::Payload))
		m_flUrgency = std::min(m_flUrgency + 0.15f, 1.f);

	if (Vars::Debug::Logging.Value)
	{
		static std::wstring sLastStatus{};
		if (sLastStatus != m_sStatus)
		{
			sLastStatus = m_sStatus;
			SDK::Output("NavBotMission", std::format("Mission: {} urgency={:.2f} friends={} enemies={}",
				SDK::ConvertWideToUTF8(m_sStatus), m_flUrgency, m_iFriendliesNear, m_iEnemiesNear).c_str(),
				{ 120, 220, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}
	}
}
