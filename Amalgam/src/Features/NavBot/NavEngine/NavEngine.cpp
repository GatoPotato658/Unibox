#include "NavEngine.h"
#include "PathWorker.h"
#include "../Hazards/Hazards.h"
#include "../NavBotJobs/NavBotJobs.h"
#include "Controllers/Controller.h"
#include "Controllers/PLController/PLController.h"
#include "../../Configs/Configs.h"
#include "../../Ticks/Ticks.h"
#include "../../Misc/Misc.h"
#include "../BotUtils.h"
#include "../NavRuntime.h"
#include "../../FollowBot/FollowBot.h"

namespace
{
	Vector GetNearestPointOnArea(CNavArea* pArea, const Vector& vPos)
	{
		if (!pArea) return vPos;
		const float flX = std::clamp(vPos.x, pArea->m_vNwCorner.x, pArea->m_vSeCorner.x);
		const float flY = std::clamp(vPos.y, pArea->m_vNwCorner.y, pArea->m_vSeCorner.y);
		return { flX, flY, pArea->GetZ(flX, flY) };
	}

	float GetAreaVerticalOutside(CNavArea* pArea, const Vector& vPos)
	{
		if (!pArea) return FLT_MAX;
		const float flBelow = std::max(pArea->m_flMinZ - vPos.z, 0.0f);
		const float flAbove = std::max(vPos.z - pArea->m_flMaxZ, 0.0f);
		return flBelow + flAbove;
	}

	float GetAreaLocalScore(CNavArea* pArea, const Vector& vPos)
	{
		if (!pArea) return FLT_MAX;
		const Vector vNearest = GetNearestPointOnArea(pArea, vPos);
		Vector vPlanar = vNearest - vPos; vPlanar.z = 0.0f;
		const float flSurfaceDelta = std::fabs(vNearest.z - vPos.z);
		const float flOutside = GetAreaVerticalOutside(pArea, vPos);
		float flScore = vPlanar.LengthSqr() + (flSurfaceDelta * flSurfaceDelta * 6.0f) + (flOutside * flOutside * 18.0f);
		if (pArea->IsOverlapping(vPos)) flScore *= 0.45f;
		if (pArea->IsOverlapping(vPos) && flOutside <= 18.0f) flScore *= 0.15f;
		return flScore;
	}

	bool IsPayloadEscortPaceState(CTFPlayer* pLocal, const Vector& vLocalOrigin)
	{
		if (!pLocal || F::GameObjectiveController.m_eGameMode != TF_GAMETYPE_ESCORT)
			return false;
		if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::Capture)
			return false;

		static std::array<Vector, 2> aLastPayloadPos{};
		static std::array<float, 2> aLastPayloadMoveTime{};
		static std::array<bool, 2> aSeenPayload{};
		static std::string sLastLevelName{};

		const std::string sLevelName = SDK::GetLevelName();
		if (sLastLevelName != sLevelName)
		{
			aLastPayloadPos = {};
			aLastPayloadMoveTime = {};
			aSeenPayload = {};
			sLastLevelName = sLevelName;
		}

		auto pPayload = F::PLController.GetClosestPayload(vLocalOrigin, pLocal->m_iTeamNum());
		if (!pPayload) return false;

		constexpr float flEscortRadius = 120.0f;
		constexpr float flMaxHeight = PLAYER_JUMP_HEIGHT + 24.0f;
		constexpr float flMoveThreshold = 4.0f;
		constexpr float flMoveGrace = 0.35f;

		Vector vPayloadPos = pPayload->GetAbsOrigin();
		if (std::fabs(vPayloadPos.z - vLocalOrigin.z) > flMaxHeight) return false;
		if (vPayloadPos.DistTo2DSqr(vLocalOrigin) > flEscortRadius * flEscortRadius) return false;

		const int iIdx = pLocal->m_iTeamNum() - TF_TEAM_RED;
		if (iIdx < 0 || iIdx >= static_cast<int>(aLastPayloadPos.size())) return false;

		if (aSeenPayload[iIdx])
		{
			if (vPayloadPos.DistToSqr(aLastPayloadPos[iIdx]) >= flMoveThreshold * flMoveThreshold)
				aLastPayloadMoveTime[iIdx] = I::GlobalVars->curtime;
		}
		else
		{
			aSeenPayload[iIdx] = true;
		}
		aLastPayloadPos[iIdx] = vPayloadPos;

		return I::GlobalVars->curtime - aLastPayloadMoveTime[iIdx] <= flMoveGrace;
	}

	bool CanJumpIfScoped(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
	{
		if (pLocal->m_fFlags() & FL_INWATER) return true;
		const auto iWeaponID = pWeapon->GetWeaponID();
		return iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC
			? !pWeapon->As<CTFSniperRifleClassic>()->m_bCharging()
			: !pLocal->InCond(TF_COND_ZOOMED);
	}
}

CNavEngine::CNavEngine()
	: m_pPathWorker(std::make_unique<PathWorker::CPathWorker>())
{
}

CNavEngine::~CNavEngine() = default;

bool CNavEngine::IsSetupTime()
{
	static Timer tCheckTimer{};
	static bool bSetupTime = false;
	if (Vars::Misc::Movement::NavEngine::PathInSetup.Value)
		return false;

	auto pLocal = H::Entities.GetLocal();
	if (pLocal && pLocal->IsAlive() && tCheckTimer.Run(0.5f))
	{
		const std::string sLevelName = SDK::GetLevelName();
		if (sLevelName == "plr_pipeline") return false;

		if (auto pGameRules = I::TFGameRules())
		{
			if (pGameRules->m_iRoundState() == GR_STATE_PREROUND) return bSetupTime = true;
			if (pLocal->m_iTeamNum() == TF_TEAM_BLUE
				&& (pGameRules->m_bInSetup()
					|| (pGameRules->m_bInWaitingForPlayers() && (sLevelName.starts_with("pl_") || sLevelName.starts_with("cp_")))))
				return bSetupTime = true;
			bSetupTime = false;
		}
	}
	return bSetupTime;
}

bool CNavEngine::IsVectorVisibleNavigation(const Vector vFrom, const Vector vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterNavigation filter;
	SDK::Trace(vFrom, vTo, nMask, &filter, &trace);
	return trace.fraction == 1.0f;
}

bool CNavEngine::IsPlayerPassableNavigation(CTFPlayer* pLocal, const Vector vFrom, Vector vTo, unsigned int nMask)
{
	if (!pLocal) return false;

	Vector vDelta = vTo - vFrom; vDelta.z = 0.f;
	if (vDelta.Length() < 16.f) return true;

	Vec3 vForward, vRight, vUp;
	Math::AngleVectors(Math::VectorAngles(vDelta), &vForward, &vRight, &vUp);
	vRight.z = 0.f;
	const float flRightLen = vRight.Length();
	if (flRightLen <= 0.001f) return false;
	vRight /= flRightLen;

	Vector vStart = vFrom; vStart.z += PLAYER_JUMP_HEIGHT;
	Vector vEnd = vTo;     vEnd.z += PLAYER_JUMP_HEIGHT;
	const Vector vOffset = vRight * (HALF_PLAYER_WIDTH * 0.8f);

	CTraceFilterNavigation tFilter(pLocal);
	CGameTrace tLeft{}, tRight{};

	SDK::Trace(vStart - vOffset, vEnd - vOffset, nMask, &tFilter, &tLeft);
	if (tLeft.fraction < 1.0f) return false;

	SDK::Trace(vStart + vOffset, vEnd + vOffset, nMask, &tFilter, &tRight);
	return tRight.fraction >= 1.0f;
}

void CNavEngine::EmitIntraAreaCrumbs(const Vector& vStart, const Vector& vDestination, CNavArea* pArea)
{
	if (!pArea) return;

	const Vector vDelta = vDestination - vStart;
	Vector vPlanar = vDelta; vPlanar.z = 0.f;
	const float flPlanar = vPlanar.Length();
	const float flVertical = std::fabs(vDelta.z);
	const float flEffective = std::max(flPlanar, flVertical);
	if (flEffective <= 1.f) return;

	Vector vApproachDir = vPlanar;
	if (const float flLen = vApproachDir.Length(); flLen > 0.01f)
		vApproachDir /= flLen;
	else
		vApproachDir = {};

	constexpr float kSpacing = 90.f;
	const int nIntermediate = std::clamp(static_cast<int>(std::ceil(flEffective / kSpacing)), 1, 6);
	const Vector vStep = vDelta / static_cast<float>(nIntermediate + 1);

	for (int i = 1; i <= nIntermediate; ++i)
	{
		Crumb_t tCrumb{};
		tCrumb.m_pNavArea = pArea;
		tCrumb.m_vPos = vStart + vStep * static_cast<float>(i);
		tCrumb.m_vApproachDir = vApproachDir;
		m_vCrumbs.push_back(tCrumb);
	}
}

void CNavEngine::EmitConnectionCrumbs(CNavArea* pFrom, CNavArea* pTo)
{
	if (!pFrom || !pTo || !m_pMap) return;

	const bool bIsOneWay = m_pMap->IsOneWay(pFrom, pTo);
	NavPoints_t tPoints = m_pMap->DeterminePoints(pFrom, pTo, bIsOneWay);
	DropdownHint_t tDrop = m_pMap->HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext, bIsOneWay);
	tPoints.m_vCenter = tDrop.m_vAdjustedPos;

	Vector vApproachDir = tPoints.m_vCenter - tPoints.m_vCurrent;
	vApproachDir.z = 0.f;
	if (const float flLen = vApproachDir.Length(); flLen > 0.01f)
		vApproachDir /= flLen;
	else
		vApproachDir = {};

	auto Push = [&](const Vector& vPos, const Vector& vDir, CNavArea* pArea, bool bDrop, float flDropH, float flApproach, const Vector& vDropDir)
	{
		if (!m_vCrumbs.empty() && m_vCrumbs.back().m_vPos.DistToSqr(vPos) < 1.0f)
			return;
		Crumb_t tCrumb{};
		tCrumb.m_pNavArea = pArea;
		tCrumb.m_vPos = vPos;
		tCrumb.m_vApproachDir = vDir;
		tCrumb.m_bRequiresDrop = bDrop;
		tCrumb.m_flDropHeight = flDropH;
		tCrumb.m_flApproachDistance = flApproach;
		if (bDrop && vDropDir.LengthSqr() > 0.f)
			tCrumb.m_vApproachDir = vDropDir;
		m_vCrumbs.push_back(tCrumb);
	};

	Push(tPoints.m_vCenter, vApproachDir, pFrom,
		tDrop.m_bRequiresDrop, tDrop.m_flDropHeight, tDrop.m_flApproachDistance, tDrop.m_vApproachDir);

	Vector vNextDir = tPoints.m_vNext - tPoints.m_vCenter;
	vNextDir.z = 0.f;
	if (const float flLen = vNextDir.Length(); flLen > 0.01f) vNextDir /= flLen; else vNextDir = vApproachDir;
	Push(tPoints.m_vNext, vNextDir, pTo, false, 0.f, 0.f, {});
}

bool CNavEngine::NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority, bool bShouldRepath, bool bNavToLocal, bool bIgnoreTraces)
{
	if (!m_pMap || !m_pPathWorker) return false;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal) return false;

	const Vector vPreviousDestination = m_vLastDestination;
	const bool bPreviousNavToLocal = m_bCurrentNavToLocal;
	const bool bPreviousIgnoreTraces = m_bIgnoreTraces;

	m_vLastDestination = vDestination;
	m_bCurrentNavToLocal = bNavToLocal;
	m_bRepathOnFail = bShouldRepath;
	m_bIgnoreTraces = bIgnoreTraces;
	m_sLastFailureReason = "";

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap) { m_sLastFailureReason = "Warping/Doubletapping"; return false; }
	if (!IsReady())                                 { m_sLastFailureReason = "Not ready"; return false; }
	if (ePriority < m_eCurrentPriority)             { m_sLastFailureReason = "Priority too low"; return false; }
	if (!GetLocalNavArea())                         { m_sLastFailureReason = "No local nav area"; return false; }

	CNavArea* pDestArea = FindClosestNavArea(vDestination, false);
	if (!pDestArea) { m_sLastFailureReason = "No destination nav area"; return false; }

	constexpr float flReuseRadiusSq = 160.f * 160.f;
	const uint64_t uHazardGen = F::Hazards.GetGenerationId();
	const bool bCanReuse = IsPathing()
		&& !m_bRepathRequested
		&& m_uPendingRequestId == 0
		&& ePriority == m_eCurrentPriority
		&& bNavToLocal == bPreviousNavToLocal
		&& bIgnoreTraces == bPreviousIgnoreTraces
		&& uHazardGen == m_uHazardGenerationSeen
		&& vPreviousDestination.DistToSqr(vDestination) <= flReuseRadiusSq;
	if (bCanReuse) return true;

	const int iNow = I::GlobalVars ? I::GlobalVars->tickcount : 0;
	if (m_uPendingRequestId != 0 && iNow - m_iLastSubmitTick < TIME_TO_TICKS(0.1f))
		return true;

	PathWorker::PathRequest tReq{};
	tReq.m_uRequestId         = ++m_uNextRequestId;
	tReq.m_uWorldGeneration   = m_uWorldGeneration;
	tReq.m_uHazardGeneration  = uHazardGen;
	tReq.m_pStartArea         = m_pLocalArea;
	tReq.m_pDestArea          = pDestArea;
	tReq.m_vDestination       = vDestination;
	tReq.m_ePriority          = ePriority;
	tReq.m_bIgnoreTraces      = bIgnoreTraces;
	tReq.m_bNavToLocal        = bNavToLocal;
	tReq.m_tCtx               = CMap::BuildSolveContext();

	m_pPathWorker->Submit(std::move(tReq));
	m_uPendingRequestId       = m_uNextRequestId;
	m_uHazardGenerationSeen   = uHazardGen;
	m_iLastSubmitTick         = iNow;
	return true;
}

bool CNavEngine::BuildCrumbsFromResult(const PathWorker::PathResult& tResult, CTFPlayer* pLocal)
{
	const auto& vPath = tResult.m_vPath;
	const bool bSingleAreaPath = tResult.m_iSolveResult == 3;

	if (!bSingleAreaPath && vPath.empty()) return false;

	if (!bSingleAreaPath)
	{
		std::lock_guard lock(m_pMap->m_mutex);
		for (size_t i = 0; i + 1 < vPath.size(); ++i)
		{
			if (!vPath[i] || !vPath[i + 1] || !m_pMap->IsAreaValid(vPath[i]) || !m_pMap->IsAreaValid(vPath[i + 1])
				|| !m_pMap->HasDirectConnection(vPath[i], vPath[i + 1]))
			{
				m_sLastFailureReason = "Path contains disconnected areas";
				return false;
			}
		}
	}

	m_vCrumbs.clear();
	if (bSingleAreaPath)
	{
		const Vector vStart = pLocal->IsAlive() ? pLocal->GetAbsOrigin()
			: (m_pLocalArea ? m_pLocalArea->m_vCenter : tResult.m_vDestination);
		EmitIntraAreaCrumbs(vStart, tResult.m_vDestination, m_pLocalArea);

		Crumb_t tEnd{};
		tEnd.m_pNavArea = m_pLocalArea;
		tEnd.m_vPos = tResult.m_vDestination;
		m_vCrumbs.push_back(tEnd);
	}
	else
	{
		m_vCrumbs.reserve(vPath.size() * 2 + 1);
		for (size_t i = 0; i < vPath.size(); ++i)
		{
			CNavArea* pArea = vPath[i];
			if (!pArea) continue;

			if (i + 1 < vPath.size())
				EmitConnectionCrumbs(pArea, vPath[i + 1]);
			else
			{
				Crumb_t tEnd{};
				tEnd.m_pNavArea = pArea;
				tEnd.m_vPos = tResult.m_vDestination;
				m_vCrumbs.push_back(tEnd);
			}
		}
	}

	if (m_vCrumbs.size() >= 2)
	{
		const Vector vLocalOrigin = pLocal->GetAbsOrigin();
		const Vector vFirst = m_vCrumbs[0].m_vPos;
		const Vector vSecond = m_vCrumbs[1].m_vPos;
		const Vector vToSecond = vSecond - vFirst;
		if (vToSecond.LengthSqr() > 0.001f && (vLocalOrigin - vFirst).Dot(vToSecond) > 0.f)
			m_vCrumbs.erase(m_vCrumbs.begin());
	}

	if (!tResult.m_bIgnoreTraces && !m_vCrumbs.empty())
	{
		const int iExpire = TICKCOUNT_TIMESTAMP(std::min(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value, 45));
		std::lock_guard lock(m_pMap->m_mutex);
		bool bValid = true;

		for (size_t i = 0; i + 1 < m_vCrumbs.size(); ++i)
		{
			const auto& a = m_vCrumbs[i];
			const auto& b = m_vCrumbs[i + 1];
			if (a.m_pNavArea && a.m_pNavArea == b.m_pNavArea) continue;

			const auto tKey = std::pair<CNavArea*, CNavArea*>(a.m_pNavArea, b.m_pNavArea);
			auto it = m_pMap->m_mVischeckCache.find(tKey);
			if (it != m_pMap->m_mVischeckCache.end() && it->second.m_iExpireTick > I::GlobalVars->tickcount)
			{
				if (!it->second.m_bPassable) { bValid = false; break; }
				continue;
			}

			if (!IsPlayerPassableNavigation(pLocal, a.m_vPos, b.m_vPos))
			{
				auto& tEnt = m_pMap->m_mVischeckCache[tKey];
				tEnt.m_iExpireTick = iExpire;
				tEnt.m_eVischeckState = VischeckStateEnum::NotVisible;
				tEnt.m_bPassable = false;
				tEnt.m_flCachedCost = std::numeric_limits<float>::max();
				bValid = false;
				break;
			}
			else
			{
				auto& tEnt = m_pMap->m_mVischeckCache[tKey];
				tEnt.m_iExpireTick = iExpire;
				tEnt.m_eVischeckState = VischeckStateEnum::Visible;
				tEnt.m_bPassable = true;
			}
		}

		if (!bValid)
		{
			m_vCrumbs.clear();
			m_sLastFailureReason = "Path blocked by traces";
			return false;
		}
	}

	return true;
}

void CNavEngine::PollPathWorker()
{
	if (!m_pPathWorker) return;
	auto pLocal = H::Entities.GetLocal();

	while (auto oResult = m_pPathWorker->Poll())
	{
		const auto& tResult = *oResult;

		if (tResult.m_uWorldGeneration != m_uWorldGeneration) continue;
		if (tResult.m_uRequestId != m_uPendingRequestId)      continue;
		m_uPendingRequestId = 0;

		if (tResult.m_bCancelled) { m_sLastFailureReason = "Path cancelled"; continue; }

		// Hazards moved while we solved — re-path rather than commit a now-stale route.
		if (tResult.m_uHazardGeneration != F::Hazards.GetGenerationId())
		{
			m_bRepathRequested = true;
			m_iNextRepathTick = std::max(m_iNextRepathTick, TICKCOUNT_TIMESTAMP(0.05f));
			continue;
		}

		if (!pLocal) { m_sLastFailureReason = "No local player"; continue; }

		if (tResult.m_iSolveResult == 1) { m_sLastFailureReason = "No solution found";    continue; }
		if (tResult.m_iSolveResult == 2) { m_sLastFailureReason = "Pathing engine error"; continue; }

		if (!BuildCrumbsFromResult(tResult, pLocal)) continue;

		m_eCurrentPriority = tResult.m_ePriority;
	}
}

float CNavEngine::GetPathCost(CNavArea* pStartArea, CNavArea* pDestinationArea)
{
	if (!m_pMap) return FLT_MAX;
	SolveContext tCtx = CMap::BuildSolveContext();
	std::lock_guard lock(m_pMap->m_mutex);
	std::vector<CNavArea*> vPath;
	float flCost = FLT_MAX;
	const int iResult = m_pMap->Solve(pStartArea, pDestinationArea, tCtx, vPath, &flCost);
	if (iResult == 3) return 0.f;
	if (iResult != 0) return FLT_MAX;
	return flCost;
}

float CNavEngine::GetPathCost(const Vector& vStart, const Vector& vDestination, bool bLocal)
{
	if (!IsNavMeshLoaded()) return FLT_MAX;
	CNavArea* pStart = bLocal ? GetLocalNavArea(vStart) : FindClosestNavArea(vStart, false);
	if (!pStart) return FLT_MAX;
	CNavArea* pDest = FindClosestNavArea(vDestination, false);
	if (!pDest) return FLT_MAX;
	return GetPathCost(pStart, pDest);
}

CNavArea* CNavEngine::GetLocalNavArea(const Vector& vLocalOrigin)
{
	static Timer tRefresh{};

	const bool bAreaInvalid = !m_pLocalArea || !m_pMap->IsAreaValid(m_pLocalArea);
	const bool bOutsideXY = !bAreaInvalid && !m_pLocalArea->IsOverlapping(vLocalOrigin);
	const float flVerticalOutside = !bAreaInvalid ? GetAreaVerticalOutside(m_pLocalArea, vLocalOrigin) : FLT_MAX;
	const float flSurfaceDelta = !bAreaInvalid ? std::fabs(GetNearestPointOnArea(m_pLocalArea, vLocalOrigin).z - vLocalOrigin.z) : FLT_MAX;
	const bool bNeedsRefresh = bAreaInvalid || bOutsideXY || flVerticalOutside > 24.0f || flSurfaceDelta > PLAYER_HEIGHT;

	if (bNeedsRefresh || tRefresh.Run(0.10f))
	{
		CNavArea* pBest = FindClosestNavArea(vLocalOrigin, true);
		if (!m_pLocalArea || !pBest)
			m_pLocalArea = pBest;
		else
		{
			const float flCur = GetAreaLocalScore(m_pLocalArea, vLocalOrigin);
			const float flNew = GetAreaLocalScore(pBest, vLocalOrigin);
			if (bNeedsRefresh || (pBest != m_pLocalArea && flNew + 4.0f < flCur))
				m_pLocalArea = pBest;
		}
	}
	return m_pLocalArea;
}

void CNavEngine::VischeckPath()
{
	static Timer tVischeck{};
	if (m_vCrumbs.size() < 2 || m_bIgnoreTraces) return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal) return;

	// Must hold m_mutex — path worker reads m_mVischeckCache concurrently.
	std::lock_guard lock(m_pMap->m_mutex);

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	GetLocalNavArea(vLocalOrigin);

	float flInterval = Vars::Misc::Movement::NavEngine::VischeckTime.Value;
	Vector vToFirst = m_vCrumbs.front().m_vPos - vLocalOrigin; vToFirst.z = 0.f;
	if (vToFirst.LengthSqr() <= 500.f * 500.f) flInterval = std::min(flInterval, 0.22f);
	if (pLocal->GetAbsVelocity().Length2D() > 180.f) flInterval = std::min(flInterval, 0.16f);
	if (m_vCrumbs.front().m_bRequiresDrop) flInterval = std::min(flInterval, 0.14f);

	flInterval = std::clamp(flInterval, 0.03f, std::max(Vars::Misc::Movement::NavEngine::VischeckTime.Value, 0.03f));
	if (!tVischeck.Run(flInterval)) return;

	static Timer tOffTrack{};
	if (tOffTrack.Run(0.35f) && m_pLocalArea)
	{
		bool bOnTrack = false;
		for (size_t i = 0; i < m_vCrumbs.size() && i < 6; ++i)
			if (m_vCrumbs[i].m_pNavArea == m_pLocalArea) { bOnTrack = true; break; }

		if (!bOnTrack)
		{
			auto* pFrontArea = m_vCrumbs.front().m_pNavArea;
			const bool bConnected = pFrontArea
				&& (m_pMap->HasDirectConnection(m_pLocalArea, pFrontArea)
					|| m_pMap->HasDirectConnection(pFrontArea, m_pLocalArea));

			Vector vOff = m_vCrumbs.front().m_vPos - vLocalOrigin; vOff.z = 0.f;
			if (!bConnected && vOff.LengthSqr() > 280.f * 280.f)
			{
				AbandonPath("Off track");
				return;
			}
		}
	}

	{
		const auto& tFirst = m_vCrumbs.front();
		const bool bDirect = IsPlayerPassableNavigation(pLocal, vLocalOrigin, tFirst.m_vPos);
		bool bSecond = false;
		if (!bDirect && m_vCrumbs.size() > 1 && !tFirst.m_bRequiresDrop)
			bSecond = IsPlayerPassableNavigation(pLocal, vLocalOrigin, m_vCrumbs[1].m_vPos);

		if (!bDirect && !bSecond && !tFirst.m_bRequiresDrop)
		{
			if (m_pLocalArea && tFirst.m_pNavArea)
			{
				auto& tEnt = m_pMap->m_mVischeckCache[std::pair{ m_pLocalArea, tFirst.m_pNavArea }];
				tEnt.m_iExpireTick = TICKCOUNT_TIMESTAMP(8.f);
				tEnt.m_eVischeckState = VischeckStateEnum::NotVisible;
				tEnt.m_bPassable = false;
				tEnt.m_flCachedCost = std::numeric_limits<float>::max();
			}
			AbandonPath("Path entrance blocked");
			return;
		}
	}

	const int iExpire = TICKCOUNT_TIMESTAMP(std::min(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value, 45));
	const size_t nMax = std::min<size_t>(m_vCrumbs.size() - 1, 14);
	for (size_t i = 0; i < nMax; ++i)
	{
		const auto& a = m_vCrumbs[i];
		const auto& b = m_vCrumbs[i + 1];
		if (a.m_pNavArea && a.m_pNavArea == b.m_pNavArea) continue;

		const auto tKey = std::pair<CNavArea*, CNavArea*>(a.m_pNavArea, b.m_pNavArea);
		auto it = m_pMap->m_mVischeckCache.find(tKey);
		if (it != m_pMap->m_mVischeckCache.end() && it->second.m_iExpireTick > I::GlobalVars->tickcount)
		{
			if (!it->second.m_bPassable) { AbandonPath("Traceline blocked (cached)"); break; }
			continue;
		}

		if (!IsPlayerPassableNavigation(pLocal, a.m_vPos, b.m_vPos))
		{
			auto& tEnt = m_pMap->m_mVischeckCache[tKey];
			tEnt.m_iExpireTick = iExpire;
			tEnt.m_eVischeckState = VischeckStateEnum::NotVisible;
			tEnt.m_bPassable = false;
			tEnt.m_flCachedCost = std::numeric_limits<float>::max();
			AbandonPath("Traceline blocked");
			break;
		}
		else
		{
			auto& tEnt = m_pMap->m_mVischeckCache[tKey];
			tEnt.m_iExpireTick = iExpire;
			tEnt.m_eVischeckState = VischeckStateEnum::Visible;
			tEnt.m_bPassable = true;
		}
	}
}

void CNavEngine::CheckBlacklist(CTFPlayer* pLocal)
{
	static Timer tCheck{};
	if (!tCheck.Run(0.5f) || m_bIgnoreTraces) return;

	F::Hazards.SetIgnoreSentries(m_eCurrentPriority == PriorityListEnum::SnipeSentry
		|| m_eCurrentPriority == PriorityListEnum::Capture);

	// Suspend hazard cost while standing on a hazard so A* doesn't path around the bot's own spot.
	F::Hazards.UpdateBotStanding(m_pLocalArea);
	if (F::Hazards.BotStandingOnHazard() || pLocal->IsInvulnerable())
		return;

	std::lock_guard lock(m_pMap->m_mutex);

	const int iNow = I::GlobalVars->tickcount;
	const int iCooldown = TIME_TO_TICKS(0.4f);
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	const float flThreshold = m_eCurrentPriority == PriorityListEnum::Capture ? 4000.f : 2500.f;

	for (size_t i = 0; i < m_vCrumbs.size() && i < 20; ++i)
	{
		const auto& tCrumb = m_vCrumbs[i];
		Vector vAhead = tCrumb.m_vPos - vLocalOrigin; vAhead.z = 0.f;
		if (vAhead.LengthSqr() > 1800.f * 1800.f) break;

		const float flHazard = F::Hazards.GetCost(tCrumb.m_pNavArea);
		if (std::isfinite(flHazard) && flHazard >= flThreshold && iNow - m_iLastBlacklistAbandonTick >= iCooldown)
		{
			m_iLastBlacklistAbandonTick = iNow;
			AbandonPath("Hazardous area ahead");
			return;
		}
		if (!std::isfinite(flHazard) && iNow - m_iLastBlacklistAbandonTick >= iCooldown)
		{
			m_iLastBlacklistAbandonTick = iNow;
			AbandonPath("Hard-blocked area ahead");
			return;
		}

		if (tCrumb.m_pNavArea)
		{
			const auto tKey = std::pair<CNavArea*, CNavArea*>(tCrumb.m_pNavArea, tCrumb.m_pNavArea);
			auto itVc = m_pMap->m_mVischeckCache.find(tKey);
			if (itVc != m_pMap->m_mVischeckCache.end() && !itVc->second.m_bPassable
				&& (itVc->second.m_iExpireTick == 0 || itVc->second.m_iExpireTick > iNow)
				&& itVc->second.m_bStuckBlacklist
				&& iNow - m_iLastBlacklistAbandonTick >= iCooldown)
			{
				m_iLastBlacklistAbandonTick = iNow;
				AbandonPath("Area blacklisted (stuck)");
				return;
			}
		}
	}
}

void CNavEngine::Reset(bool bForced)
{
	CancelPath();
	m_bIgnoreTraces = false;
	m_iNextRepathTick = 0;
	m_iLastBlacklistAbandonTick = 0;
	m_pLocalArea = nullptr;
	m_tOffMeshTimer.Update();
	m_vOffMeshTarget = {};

	// Bump invalidates any in-flight results from the previous map.
	m_uWorldGeneration++;
	if (m_pPathWorker) m_pPathWorker->CancelAll();

	static std::string sPath = std::filesystem::current_path().string();
	if (std::string sLevelName = I::EngineClient->GetLevelName(); !sLevelName.empty())
	{
		if (m_pMap) m_pMap->Reset();

		if (bForced || !m_pMap || m_pMap->m_sMapName != sLevelName)
		{
			if (m_pPathWorker) m_pPathWorker->Stop();
			F::NavBotDanger.ResetSpawn();
			F::Hazards.Reset();
			sLevelName.erase(sLevelName.find_last_of('.'));
			const std::string sNavPath = std::format("{}\\tf\\{}.nav", sPath, sLevelName);
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavEngine", std::format("Nav File location: {}", sNavPath).c_str(), { 50, 255, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
			m_pMap = std::make_unique<CMap>(sNavPath.c_str());
			m_vRespawnRoomExitAreas.clear();
			m_bUpdatedRespawnRooms = false;
			if (m_pPathWorker && m_pMap->m_eState == NavStateEnum::Active)
				m_pPathWorker->Start(m_pMap.get());
		}
	}
}

void CNavEngine::shutdown()
{
	if (m_pPathWorker) m_pPathWorker->Stop();
}

bool CNavEngine::IsReady(bool bRoundCheck)
{
	static Timer tRestartTimer{};
	if (!Vars::Misc::Movement::NavEngine::Enabled.Value) { tRestartTimer.Update(); return false; }
	if (!tRestartTimer.Check(0.5f))                       return false;
	if (!I::EngineClient->IsInGame())                     return false;
	if (!m_pMap || m_pMap->m_eState != NavStateEnum::Active) return false;
	if (!bRoundCheck && IsSetupTime())                    return false;
	return true;
}

bool CNavEngine::IsBlacklistIrrelevant()
{
	static bool bIrrelevant = false;
	static Timer tUpdateTimer{};
	if (tUpdateTimer.Run(0.5f))
	{
		int iRoundState = GR_STATE_RND_RUNNING;
		if (auto pGameRules = I::TFGameRules())
			iRoundState = pGameRules->m_iRoundState();

		bIrrelevant = iRoundState == GR_STATE_TEAM_WIN
			|| iRoundState == GR_STATE_STALEMATE
			|| iRoundState == GR_STATE_PREROUND
			|| iRoundState == GR_STATE_GAME_OVER;
	}
	return bIrrelevant;
}

void CNavEngine::ClearPathState()
{
	m_vCrumbs.clear();
	m_tLastCrumb.m_pNavArea = nullptr;
	m_vCurrentPathDir = {};
	m_vLastLookTarget = {};
	m_iStuckJumpAttempts = 0;
	m_iRecentFallSpeedIndex = 0;
	m_nRecentFallSpeedCount = 0;
	m_vLastStuckSamplePos = {};
	m_flLastDistToCrumb = FLT_MAX;
	m_iNoProgressSamples = 0;
	m_tStuckSampleTimer.Update();
}

void CNavEngine::ClearDebugPaths()
{
	m_vPossiblePaths.clear();
	m_vRejectedPaths.clear();
	m_vDebugWalkablePaths.clear();
}

void CNavEngine::AbandonPath(const std::string& sReason)
{
	if (!m_pMap) return;

	m_sLastFailureReason = sReason;
	ClearPathState();
	m_uPendingRequestId = 0;
	if (m_pPathWorker) m_pPathWorker->CancelAll();
	if (m_bRepathOnFail)
	{
		m_bRepathRequested = true;
		const float flDelay = (sReason.find("Blacklisted") != std::string::npos
							 || sReason.find("Stuck") != std::string::npos) ? 0.45f : 0.2f;
		m_iNextRepathTick = std::max(m_iNextRepathTick, TICKCOUNT_TIMESTAMP(flDelay));
		m_bRepathOnFail = false;
	}
	else
	{
		m_eCurrentPriority = PriorityListEnum::None;
	}
}

void CNavEngine::CancelPath()
{
	ClearPathState();
	m_eCurrentPriority = PriorityListEnum::None;
	m_bIgnoreTraces = false;
	m_iNextRepathTick = 0;
	m_bRepathRequested = false;
	m_uPendingRequestId = 0;
	if (m_pPathWorker) m_pPathWorker->CancelAll();
}

void CNavEngine::UpdateRespawnRooms()
{
	if (m_vRespawnRooms.empty() || !m_pMap) return;

	std::unordered_set<CNavArea*> setSpawnAreas;
	for (const auto& tRoom : m_vRespawnRooms)
	{
		static Vector vStep(0.0f, 0.0f, 18.0f);
		for (auto& tArea : m_pMap->m_navfile.m_vAreas)
		{
			if (setSpawnAreas.contains(&tArea)) continue;

			if (tRoom.tData.PointIsWithin(tArea.m_vCenter + vStep)
				|| tRoom.tData.PointIsWithin(tArea.m_vNwCorner + vStep)
				|| tRoom.tData.PointIsWithin(tArea.GetNeCorner() + vStep)
				|| tRoom.tData.PointIsWithin(tArea.GetSwCorner() + vStep)
				|| tRoom.tData.PointIsWithin(tArea.m_vSeCorner + vStep))
			{
				setSpawnAreas.insert(&tArea);
				const uint32_t uFlags = tRoom.m_iTeam == 0
					? (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED)
					: (tRoom.m_iTeam == TF_TEAM_BLUE ? TF_NAV_SPAWN_ROOM_BLUE : TF_NAV_SPAWN_ROOM_RED);
				tArea.m_iTFAttributeFlags |= uFlags;
			}
		}
	}

	for (auto pArea : setSpawnAreas)
		for (auto& tConn : pArea->m_vConnections)
			if (tConn.m_pArea
				&& !(tConn.m_pArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT)))
			{
				tConn.m_pArea->m_iTFAttributeFlags |= TF_NAV_SPAWN_ROOM_EXIT;
				m_vRespawnRoomExitAreas.push_back(tConn.m_pArea);
			}

	m_bUpdatedRespawnRooms = true;
}

void CNavEngine::RecoverOffMesh(CTFPlayer* pLocal, CNavArea* pArea, const Vector& vLocalOrigin)
{
	std::vector<CNavArea*> vCandidates;
	m_pMap->CollectAreasAround(vLocalOrigin, 325.0f, vCandidates);
	if (pArea && std::find(vCandidates.begin(), vCandidates.end(), pArea) == vCandidates.end())
		vCandidates.push_back(pArea);

	CNavArea* pRecovery = nullptr;
	Vector vTarget = {};
	float flBest = FLT_MAX;
	CTraceFilterNavigation filter(pLocal);

	for (auto* pCand : vCandidates)
	{
		if (!pCand) continue;
		Vector vCandTarget = GetNearestPointOnArea(pCand, vLocalOrigin);
		const float flOutside = GetAreaVerticalOutside(pCand, vLocalOrigin);
		const float flSurfaceDelta = std::fabs(vCandTarget.z - vLocalOrigin.z);
		if (flOutside > PLAYER_CROUCHED_JUMP_HEIGHT && flSurfaceDelta > PLAYER_HEIGHT + 24.0f)
			continue;

		CGameTrace trace;
		SDK::Trace(vLocalOrigin, vCandTarget, MASK_PLAYERSOLID, &filter, &trace);
		Vector vResolved = trace.fraction >= 1.0f ? vCandTarget : trace.endpos;
		if (trace.fraction <= 0.05f || vResolved.DistToSqr(vLocalOrigin) < 16.0f * 16.0f)
			continue;

		Vector vPlanar = vCandTarget - vLocalOrigin; vPlanar.z = 0.0f;
		float flScore = vPlanar.LengthSqr() + flSurfaceDelta * flSurfaceDelta * 8.0f + flOutside * flOutside * 12.0f;
		if (pCand == pArea) flScore *= 0.9f;

		if (flScore < flBest) { flBest = flScore; pRecovery = pCand; vTarget = vResolved; }
	}

	if (pRecovery)
	{
		m_vOffMeshTarget = vTarget;
		m_vCrumbs.clear();
		EmitIntraAreaCrumbs(vLocalOrigin, vTarget, pRecovery);
		Crumb_t tEnd{}; tEnd.m_pNavArea = pRecovery; tEnd.m_vPos = vTarget;
		m_vCrumbs.push_back(tEnd);
		m_eCurrentPriority = PriorityListEnum::Patrol;
		m_tOffMeshTimer.Update();
	}
}

void CNavEngine::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static bool bWasOn = false;
	if (!Vars::Misc::Movement::NavEngine::Enabled.Value) bWasOn = false;
	else if (I::EngineClient->IsInGame() && !bWasOn)
	{
		bWasOn = true;
		Reset(true);
	}

	if (!m_bUpdatedRespawnRooms) UpdateRespawnRooms();

	PollPathWorker();

	if (!pLocal->IsAlive() || F::FollowBot.m_bActive)
	{
		CancelPath();
		return;
	}

	if (NavRuntime::IsMovementLocked(pLocal))
	{
		CancelPath();
		return;
	}

	if (m_bRepathRequested && I::GlobalVars->tickcount >= m_iNextRepathTick)
	{
		m_bRepathRequested = false;
		if (!NavTo(m_vLastDestination, m_eCurrentPriority, true, m_bCurrentNavToLocal, m_bIgnoreTraces))
			m_iNextRepathTick = std::max(m_iNextRepathTick, TICKCOUNT_TIMESTAMP(0.25f));
	}

	if ((m_eCurrentPriority == PriorityListEnum::Engineer
			&& ((!Vars::Aimbot::AutoEngie::AutoRepair.Value && !Vars::Aimbot::AutoEngie::AutoUpgrade.Value)
				|| pLocal->m_iClass() != TF_CLASS_ENGINEER))
		|| (m_eCurrentPriority == PriorityListEnum::Capture
			&& !(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::CaptureObjectives)))
	{
		CancelPath();
		return;
	}

	if (!pCmd
		|| (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
		|| !IsReady(true))
		return;

	if (IsSetupTime())
	{
		CancelPath();
		return;
	}

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	CNavArea* pArea = GetLocalNavArea(vLocalOrigin);
	const bool bOnNavMesh = pArea
		&& pArea->IsOverlapping(vLocalOrigin)
		&& GetAreaVerticalOutside(pArea, vLocalOrigin) <= 18.0f
		&& std::fabs(GetNearestPointOnArea(pArea, vLocalOrigin).z - vLocalOrigin.z) < 18.0f;

	if (bOnNavMesh || IsPathing())
		m_tOffMeshTimer.Update();
	else if (pArea)
		RecoverOffMesh(pLocal, pArea, vLocalOrigin);

	if (Vars::Misc::Movement::NavEngine::VischeckEnabled.Value && !F::Ticks.m_bWarp && !F::Ticks.m_bDoubletap)
		VischeckPath();

	ClearDebugPaths();

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::PossiblePaths)
	{
		std::lock_guard lock(m_pMap->m_mutex);
		if (pArea)
		{
			std::vector<CNavArea*> vAreas;
			m_pMap->CollectAreasAround(vLocalOrigin, 500.f, vAreas);
			for (auto* pCur : vAreas)
				for (auto& tConn : pCur->m_vConnections)
				{
					if (!tConn.m_pArea) continue;
					auto it = m_pMap->m_mVischeckCache.find({ pCur, tConn.m_pArea });
					if (it != m_pMap->m_mVischeckCache.end())
					{
						if (it->second.m_bPassable)
							m_vPossiblePaths.push_back({ it->second.m_tPoints.m_vCurrent, it->second.m_tPoints.m_vNext });
						else
							m_vRejectedPaths.push_back({ it->second.m_tPoints.m_vCurrent, it->second.m_tPoints.m_vNext });
					}
				}
		}
	}

	FollowCrumbs(pLocal, pWeapon, pCmd);
	CheckBlacklist(pLocal);
}

void CNavEngine::SampleFallSpeed(float flVerticalVelocity)
{
	m_flRecentFallSpeeds[m_iRecentFallSpeedIndex] = flVerticalVelocity;
	m_iRecentFallSpeedIndex = (m_iRecentFallSpeedIndex + 1) % m_flRecentFallSpeeds.size();
	m_nRecentFallSpeedCount = std::min(m_nRecentFallSpeedCount + 1, m_flRecentFallSpeeds.size());
}

bool CNavEngine::RecentlyAtRest() const
{
	if (m_nRecentFallSpeedCount == 0) return false;
	for (size_t i = 0; i < m_nRecentFallSpeedCount; ++i)
		if (m_flRecentFallSpeeds[i] > 0.01f || m_flRecentFallSpeeds[i] < -0.01f)
			return false;
	return true;
}

StuckPhase CNavEngine::TickStuckSample(const Vector& vLocalOrigin, const Vector& vCrumbTarget)
{
	if (!m_tStuckSampleTimer.Check(0.4f)) return StuckPhase::Idle;
	m_tStuckSampleTimer.Update();

	const float flDistToCrumb = (vCrumbTarget - vLocalOrigin).Length2D();
	const float flMoved = (vLocalOrigin - m_vLastStuckSamplePos).Length2D();
	const bool bProgress = (flDistToCrumb < m_flLastDistToCrumb - 10.f) || (flMoved > 20.f);

	m_vLastStuckSamplePos = vLocalOrigin;
	m_flLastDistToCrumb = flDistToCrumb;

	if (bProgress) { m_iNoProgressSamples = 0; m_iStuckJumpAttempts = 0; return StuckPhase::Idle; }
	m_iNoProgressSamples++;

	// 0.4s sample period. <3 idle, 3-4 nudge, 5-9 jump, 10+ fail.
	if (m_iNoProgressSamples < 3)  return StuckPhase::Idle;
	if (m_iNoProgressSamples < 5)  return StuckPhase::Nudge;
	if (m_iNoProgressSamples < 10) return StuckPhase::Jump;
	return StuckPhase::Fail;
}

void CNavEngine::DoLookAtPath(CTFPlayer* pLocal, CUserCmd* pCmd, const Vector& vMoveTarget, bool bHaveTarget)
{
	if (G::Attacking == 1)
	{
		m_vLastLookTarget = {};
		F::BotUtils.InvalidateLLAP();
		return;
	}

	const auto eLook = Vars::Misc::Movement::NavEngine::LookAtPath.Value;
	const bool bSilent = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Silent
					 || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;
	const bool bLegit = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Legit
					 || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;

	if (eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Off
		|| (bSilent && G::AntiAim))
	{
		m_vLastLookTarget = {};
		F::BotUtils.InvalidateLLAP();
		return;
	}

	if (!bHaveTarget)
	{
		m_vLastLookTarget = {};
		F::BotUtils.InvalidateLLAP();
		if (bLegit) F::BotUtils.LookLegit(pLocal, pCmd, Vec3{}, bSilent);
		return;
	}

	// Smooth toward target to avoid snapping when the active crumb changes.
	Vector vLookTarget = vMoveTarget;
	if (!m_vLastLookTarget.IsZero())
	{
		const float flBlend = std::clamp(I::GlobalVars->interval_per_tick * 15.f, 0.f, 1.f);
		vLookTarget = m_vLastLookTarget.Lerp(vLookTarget, flBlend);
	}
	m_vLastLookTarget = vLookTarget;

	if (bLegit)
	{
		F::BotUtils.LookLegit(pLocal, pCmd, vLookTarget, bSilent);
	}
	else
	{
		F::BotUtils.InvalidateLLAP();
		F::BotUtils.LookAtPath(pCmd, Vec2(vLookTarget.x, vLookTarget.y), pLocal->GetEyePosition(), bSilent);
	}
}

void CNavEngine::FollowCrumbs(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static Timer tLastJump{};

	if (m_vCrumbs.empty())
	{
		if (m_tOffMeshTimer.Check(6000))
		{
			m_eCurrentPriority = PriorityListEnum::Patrol;
			SDK::WalkTo(pCmd, pLocal, m_vOffMeshTarget);

			if (!IsPayloadEscortPaceState(pLocal, pLocal->GetAbsOrigin())
				&& pLocal->OnSolid()
				&& NavRuntime::CanIssueNavJump(pWeapon, pCmd)
				&& tLastJump.Check(0.6f))
			{
				F::BotUtils.ForceJump();
				tLastJump.Update();
			}
		}
		else
		{
			ClearPathState();
			m_bRepathOnFail = false;
			m_eCurrentPriority = PriorityListEnum::None;
			DoLookAtPath(pLocal, pCmd, {}, false);
		}
		return;
	}

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	const Vector vLocalVelocity = pLocal->GetAbsVelocity();
	const bool bPayloadEscortPace = IsPayloadEscortPaceState(pLocal, vLocalOrigin);

	SampleFallSpeed(vLocalVelocity.z);

	// Standing still on a building → planarize height so we don't walk into the floor.
	bool bResetHeight = RecentlyAtRest();
	if (bResetHeight && !F::Ticks.m_bWarp && !F::Ticks.m_bDoubletap)
	{
		bResetHeight = false;
		Vector vEnd = vLocalOrigin; vEnd.z -= 100.0f;
		CGameTrace trace;
		CTraceFilterNavigation filter(pLocal);
		filter.m_iObject = OBJECT_DEFAULT;
		SDK::TraceHull(vLocalOrigin, vEnd, pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &filter, &trace);
		if (trace.DidHit() && trace.m_pEnt && trace.m_pEnt->IsBuilding())
			bResetHeight = true;
	}

	constexpr float kDefaultReachRadius = 50.f;
	constexpr float kDropReachRadius = 28.f;

	Vector vCrumbTarget{};
	Vector vMoveTarget{};
	Vector vMoveDir{};
	bool bDropCrumb = false;
	bool bHasMoveDir = false;
	float flReachRadius = kDefaultReachRadius;
	size_t nConsumed = 0;
	int iLoopLimit = 32;

	while (iLoopLimit-- > 0)
	{
		const size_t uRemaining = m_vCrumbs.size() - nConsumed;
		if (uRemaining == 0) break;

		auto& tActive = m_vCrumbs[nConsumed];
		if (m_tCurrentCrumb.m_pNavArea != tActive.m_pNavArea)
			m_tStuckSampleTimer.Update();
		m_tCurrentCrumb = tActive;

		bDropCrumb = tActive.m_bRequiresDrop;
		vMoveTarget = vCrumbTarget = tActive.m_vPos;

		if (bResetHeight)
		{
			vMoveTarget.z = vLocalOrigin.z;
			if (!bDropCrumb) vCrumbTarget.z = vMoveTarget.z;
		}

		vMoveDir = tActive.m_vApproachDir; vMoveDir.z = 0.f;
		float flDirLen = vMoveDir.Length();
		if (flDirLen < 0.01f && uRemaining > 1)
		{
			vMoveDir = m_vCrumbs[nConsumed + 1].m_vPos - tActive.m_vPos;
			vMoveDir.z = 0.f;
			flDirLen = vMoveDir.Length();
		}
		if (flDirLen < 0.01f && bDropCrumb && !m_vCurrentPathDir.IsZero())
		{
			vMoveDir = m_vCurrentPathDir; vMoveDir.z = 0.f;
			flDirLen = vMoveDir.Length();
		}
		if (flDirLen < 0.01f)
		{
			vMoveDir = tActive.m_vPos - vLocalOrigin; vMoveDir.z = 0.f;
			flDirLen = vMoveDir.Length();
		}

		bHasMoveDir = flDirLen > 0.01f;
		if (bHasMoveDir)
		{
			vMoveDir /= flDirLen;
			if (bDropCrumb)
			{
				float flPush = tActive.m_flApproachDistance;
				if (flPush <= 0.f) flPush = std::clamp(tActive.m_flDropHeight * 0.5f, PLAYER_WIDTH * 0.8f, PLAYER_WIDTH * 2.5f);
				else flPush = std::clamp(flPush, PLAYER_WIDTH * 0.8f, PLAYER_WIDTH * 2.5f);
				vMoveTarget += vMoveDir * flPush;
			}
		}
		else
		{
			vMoveDir = {};
		}
		m_vCurrentPathDir = vMoveDir;

		flReachRadius = bDropCrumb ? kDropReachRadius : kDefaultReachRadius;

		Vector vDelta = vCrumbTarget - vLocalOrigin;
		Vector vDeltaPlanar = vDelta; vDeltaPlanar.z = 0.f;
		const float flVerticalDelta = std::fabs(vDelta.z);
		const float flVerticalTol = std::clamp(PLAYER_JUMP_HEIGHT * 0.75f, 26.f, 42.f);

		if (!bDropCrumb && vDeltaPlanar.LengthSqr() < flReachRadius * flReachRadius && flVerticalDelta <= flVerticalTol)
		{
			m_tLastCrumb = tActive;
			nConsumed++;
			continue;
		}

		if (!bDropCrumb && uRemaining > 1)
		{
			Vector vNext = m_vCrumbs[nConsumed + 1].m_vPos - vLocalOrigin;
			const float flNextVertical = std::fabs(vNext.z);
			vNext.z = 0.f;
			if (vNext.LengthSqr() < 50.f * 50.f && flNextVertical <= PLAYER_JUMP_HEIGHT)
			{
				m_tLastCrumb = m_vCrumbs[nConsumed + 1];
				nConsumed++;
				continue;
			}
		}

		if (bDropCrumb)
		{
			constexpr float kDropSkipFloor = 18.f;
			bool bDone = false;

			const float flHeightBelow = vCrumbTarget.z - vLocalOrigin.z;
			const float flThreshold = std::max(kDropSkipFloor, tActive.m_flDropHeight * 0.5f);
			if (flHeightBelow >= flThreshold) bDone = true;

			if (!bDone && m_pLocalArea && m_pLocalArea != tActive.m_pNavArea && tActive.m_flDropHeight > kDropSkipFloor)
				bDone = true;

			if (!bDone && uRemaining > 1)
			{
				Vector vNext = m_vCrumbs[nConsumed + 1].m_vPos;
				vNext.z = vLocalOrigin.z;
				if (vNext.DistToSqr(vLocalOrigin) < std::max(kDefaultReachRadius, flReachRadius + 12.f) * std::max(kDefaultReachRadius, flReachRadius + 12.f))
					bDone = true;
			}

			if (bDone) { m_tLastCrumb = tActive; nConsumed++; continue; }
		}

		break;
	}

	if (nConsumed)
	{
		if (nConsumed >= m_vCrumbs.size())
			m_vCrumbs.clear();
		else
			m_vCrumbs.erase(m_vCrumbs.begin(), m_vCrumbs.begin() + nConsumed);

		if (m_vCrumbs.empty())
		{
			DoLookAtPath(pLocal, pCmd, {}, false);
			return;
		}
	}

	StuckPhase ePhase = StuckPhase::Idle;
	if (bPayloadEscortPace)
	{
		m_iNoProgressSamples = 0;
		m_iStuckJumpAttempts = 0;
		m_vLastStuckSamplePos = vLocalOrigin;
		m_flLastDistToCrumb = (vCrumbTarget - vLocalOrigin).Length2D();
	}
	else
	{
		ePhase = TickStuckSample(vLocalOrigin, vCrumbTarget);
	}

	if (ePhase != StuckPhase::Idle && !bDropCrumb)
	{
		const bool bCanJump = pWeapon && m_pLocalArea
			&& !(m_pLocalArea->m_iAttributeFlags & (NAV_MESH_NO_JUMP | NAV_MESH_STAIRS))
			&& (m_vCrumbs.size() < 2 || m_vCrumbs[0].m_vPos.z - m_vCrumbs[1].m_vPos.z > -PLAYER_JUMP_HEIGHT)
			&& [&] {
				const auto iWeaponID = pWeapon->GetWeaponID();
				if (iWeaponID == TF_WEAPON_SNIPERRIFLE
					|| iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC
					|| iWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP)
					return CanJumpIfScoped(pLocal, pWeapon);
				return true;
			}();

		switch (ePhase)
		{
		case StuckPhase::Nudge:
			if (bHasMoveDir)
			{
				Vector vSide(-vMoveDir.y, vMoveDir.x, 0.f);
				if (m_iNoProgressSamples % 2) vSide *= -1.f;
				vMoveTarget += vSide * (PLAYER_WIDTH * 0.4f);
			}
			break;

		case StuckPhase::Jump:
			if (bCanJump && NavRuntime::CanIssueNavJump(pWeapon, pCmd) && pLocal->OnSolid() && tLastJump.Check(0.5f))
			{
				F::BotUtils.ForceJump();
				m_iStuckJumpAttempts++;
				tLastJump.Update();
			}
			if (bHasMoveDir)
			{
				Vector vSide(-vMoveDir.y, vMoveDir.x, 0.f);
				if (m_iStuckJumpAttempts % 2) vSide *= -1.f;
				vMoveTarget += vSide * (PLAYER_WIDTH * 0.5f);
			}
			break;

		case StuckPhase::Fail:
			if (m_bRepathOnFail)
			{
				AbandonPath("Stuck (no progress)");
				return;
			}
			break;

		default:
			break;
		}
	}
	else if (bDropCrumb && ePhase == StuckPhase::Fail && m_bRepathOnFail)
	{
		AbandonPath("Stuck on drop");
		return;
	}

	DoLookAtPath(pLocal, pCmd, vMoveTarget, true);
	SDK::WalkTo(pCmd, pLocal, vMoveTarget);
}

void CNavEngine::Render()
{
	if (!Vars::Misc::Movement::NavEngine::Draw.Value || !IsReady())
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive()) return;

	F::Hazards.Render();

	if ((Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Blacklist) && m_pMap)
	{
		std::lock_guard lock(m_pMap->m_mutex);
		for (auto& [tKey, tEntry] : m_pMap->m_mVischeckCache)
		{
			if (tEntry.m_eVischeckState != VischeckStateEnum::NotVisible) continue;
			if (tEntry.m_iExpireTick != 0 && tEntry.m_iExpireTick <= I::GlobalVars->tickcount) continue;

			if (tEntry.m_tPoints.m_vCurrent.Length() > 0.f && tEntry.m_tPoints.m_vNext.Length() > 0.f)
			{
				H::Draw.RenderLine(tEntry.m_tPoints.m_vCurrent, tEntry.m_tPoints.m_vNext, Color_t(255, 0, 0, 255), false);
				H::Draw.RenderBox(tEntry.m_tPoints.m_vCurrent, Vector(-2.f, -2.f, -2.f), Vector(2.f, 2.f, 2.f), Vector(), Color_t(255, 0, 0, 255), false);
				H::Draw.RenderBox(tEntry.m_tPoints.m_vNext, Vector(-2.f, -2.f, -2.f), Vector(2.f, 2.f, 2.f), Vector(), Color_t(255, 0, 0, 255), false);
			}

			if (tKey.first == tKey.second && tKey.first)
			{
				H::Draw.RenderBox(tKey.first->m_vCenter, Vector(-6.f, -6.f, -2.f), Vector(6.f, 6.f, 2.f), Vector(), Color_t(255, 0, 0, 255), false);
				H::Draw.RenderWireframeBox(tKey.first->m_vCenter, Vector(-6.f, -6.f, -2.f), Vector(6.f, 6.f, 2.f), Vector(), Color_t(255, 0, 0, 255), false);
			}
		}
	}

	const Vector vOrigin = pLocal->GetAbsOrigin();
	if ((Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Area) && GetLocalNavArea(vOrigin))
	{
		auto vEdge = m_pLocalArea->GetNearestPoint(Vector2D(vOrigin.x, vOrigin.y));
		vEdge.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		H::Draw.RenderBox(vEdge, Vector(-4.f, -4.f, -1.f), Vector(4.f, 4.f, 1.f), Vector(), Color_t(255, 0, 0, 255), false);
		H::Draw.RenderWireframeBox(vEdge, Vector(-4.f, -4.f, -1.f), Vector(4.f, 4.f, 1.f), Vector(), Color_t(255, 0, 0, 255), false);

		H::Draw.RenderLine(m_pLocalArea->m_vNwCorner, m_pLocalArea->GetNeCorner(), Vars::Colors::NavbotArea.Value, true);
		H::Draw.RenderLine(m_pLocalArea->m_vNwCorner, m_pLocalArea->GetSwCorner(), Vars::Colors::NavbotArea.Value, true);
		H::Draw.RenderLine(m_pLocalArea->GetNeCorner(), m_pLocalArea->m_vSeCorner, Vars::Colors::NavbotArea.Value, true);
		H::Draw.RenderLine(m_pLocalArea->GetSwCorner(), m_pLocalArea->m_vSeCorner, Vars::Colors::NavbotArea.Value, true);
	}

	if ((Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Path) && !m_vCrumbs.empty())
		for (size_t i = 0; i + 1 < m_vCrumbs.size(); ++i)
			H::Draw.RenderLine(m_vCrumbs[i].m_vPos, m_vCrumbs[i + 1].m_vPos, Vars::Colors::NavbotPath.Value, false);

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::PossiblePaths)
	{
		for (auto& tPath : m_vPossiblePaths)
			H::Draw.RenderLine(tPath.first, tPath.second, Vars::Colors::NavbotPossiblePath.Value, false);
		for (auto& tPath : m_vRejectedPaths)
			H::Draw.RenderLine(tPath.first, tPath.second, Color_t(255, 0, 0, 255), false);
	}

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Walkable)
		for (auto& tPath : m_vDebugWalkablePaths)
			H::Draw.RenderLine(tPath.first, tPath.second, Vars::Colors::NavbotWalkablePath.Value, false);

	F::NavBotEngineer.Render();
}
