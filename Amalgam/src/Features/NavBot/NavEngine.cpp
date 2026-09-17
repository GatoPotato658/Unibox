#include "NavEngine.h"
#include "PathWorker.h"
#include "Hazards.h"
#include "Jobs/NavBotJobs.h"
#include "Objectives.h"
#include "../Configs/Configs.h"
#include "../Ticks/Ticks.h"
#include "../Misc/Misc.h"
#include "BotUtils.h"
#include "../FollowBot/FollowBot.h"

static Vector GetNearestPointOnArea(CNavArea* pArea, const Vector& vPos)
{
	if (!pArea) return vPos;
	const float flX = std::clamp(vPos.x, pArea->m_vNwCorner.x, pArea->m_vSeCorner.x);
	const float flY = std::clamp(vPos.y, pArea->m_vNwCorner.y, pArea->m_vSeCorner.y);
	return { flX, flY, pArea->GetZ(flX, flY) };
}

static float GetAreaVerticalOutside(CNavArea* pArea, const Vector& vPos)
{
	if (!pArea) return FLT_MAX;
	const float flBelow = std::max(pArea->m_flMinZ - vPos.z, 0.0f);
	const float flAbove = std::max(vPos.z - pArea->m_flMaxZ, 0.0f);
	return flBelow + flAbove;
}

static int GetTransientVischeckExpireTick()
{
	const float flSeconds = std::clamp(static_cast<float>(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value), 10.f, 500.f);
	return TICKCOUNT_TIMESTAMP(flSeconds);
}

static bool IsPayloadEscortPaceState(CTFPlayer* pLocal, const Vector& vLocalOrigin)
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

	if (auto pGameRules = I::TFGameRules(); pGameRules && pGameRules->m_bPlayingMannVsMachine())
	{
		bSetupTime = false;
		return false;
	}

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

	Vector vRight(-vDelta.y, vDelta.x, 0.f);
	vRight.Normalize();
	const Vector vOffset = vRight * HALF_PLAYER_WIDTH;

	Vector vStart = vFrom; vStart.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	Vector vEnd = vTo;     vEnd.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	if (std::fabs(vEnd.z - vStart.z) <= PLAYER_JUMP_HEIGHT)
		vEnd.z = vStart.z;

	CTraceFilterNavigation tFilter(pLocal);
	CGameTrace tTrace{};
	SDK::Trace(vStart - vOffset, vEnd - vOffset, nMask, &tFilter, &tTrace);
	if (tTrace.DidHit()) return false;
	SDK::Trace(vStart + vOffset, vEnd + vOffset, nMask, &tFilter, &tTrace);
	return !tTrace.DidHit();
}

bool CNavEngine::NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority, bool bShouldRepath, bool bIgnoreTraces)
{
	if (!m_pMap || !m_pPathWorker) return false;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal) return false;

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap) { m_sLastFailureReason = "Warping/Doubletapping"; return false; }
	if (!IsReady()) { m_sLastFailureReason = "Not ready"; return false; }
	if (ePriority < m_eCurrentPriority || (m_uPendingRequestId != 0 && ePriority < m_ePendingPriority))
	{
		m_sLastFailureReason = "Priority too low";
		return false;
	}
	if (!GetLocalNavArea()) { m_sLastFailureReason = "No local nav area"; return false; }

	CNavArea* pDestArea = FindClosestNavArea(vDestination, false);
	if (!pDestArea) { m_sLastFailureReason = "No destination nav area"; return false; }

	constexpr float flReuseRadiusSq = 160.f * 160.f;
	if (!m_bBypassFailedDestination
		&& m_iFailedDestinationTick > (I::GlobalVars ? I::GlobalVars->tickcount : 0)
		&& m_vFailedDestination.DistToSqr(vDestination) <= flReuseRadiusSq)
	{
		m_sLastFailureReason = "No solution found";
		return false;
	}

	const bool bSamePending = m_uPendingRequestId != 0
		&& ePriority == m_ePendingPriority
		&& bIgnoreTraces == m_bPendingIgnoreTraces
		&& m_vPendingDestination.DistToSqr(vDestination) <= flReuseRadiusSq;
	if (bSamePending)
	{
		m_bPendingRepathOnFail = bShouldRepath;
		return true;
	}

	const bool bCanReuse = IsPathing()
		&& !m_bRepathRequested
		&& !m_bBypassFailedDestination
		&& m_uPendingRequestId == 0
		&& ePriority == m_eCurrentPriority
		&& bIgnoreTraces == m_bIgnoreTraces
		&& m_vLastDestination.DistToSqr(vDestination) <= flReuseRadiusSq;
	if (bCanReuse)
	{
		m_vLastDestination = vDestination;
		m_bRepathOnFail = bShouldRepath;
		m_sLastFailureReason.clear();
		return true;
	}

	m_sLastFailureReason.clear();

	PathWorker::PathRequest tReq{};
	tReq.m_uRequestId = ++m_uNextRequestId;
	tReq.m_uWorldGeneration = m_uWorldGeneration;
	tReq.m_pStartArea = m_pLocalArea;
	tReq.m_pDestArea = pDestArea;
	tReq.m_vStart = pLocal->GetAbsOrigin();
	tReq.m_vDestination = vDestination;
	tReq.m_ePriority = ePriority;
	tReq.m_tCtx = CMap::BuildSolveContext();
	tReq.m_tCtx.m_bIgnoreTraces = bIgnoreTraces;

	m_ePriorityBeforePending = m_eCurrentPriority;
	m_bRepathBeforePending = m_bRepathOnFail;
	m_bIgnoreTracesBeforePending = m_bIgnoreTraces;
	m_vDestinationBeforePending = m_vLastDestination;
	m_bHadActivePathBeforePending = !m_vCrumbs.empty();
	m_bRecoveryRetryUsed = m_bBypassFailedDestination;
	m_pPathWorker->Submit(std::move(tReq));
	m_eCurrentPriority = ePriority;
	m_uPendingRequestId = m_uNextRequestId;
	m_vPendingDestination = vDestination;
	m_ePendingPriority = ePriority;
	m_bPendingRepathOnFail = bShouldRepath;
	m_bPendingIgnoreTraces = bIgnoreTraces;
	m_bRepathRequested = false;
	return true;
}

bool CNavEngine::StoreValidatedCrumbs(const std::vector<CachedPathCrumb_t>& vCrumbs, CTFPlayer* pLocal)
{
	if (vCrumbs.empty()) return false;

	std::vector<Crumb_t> vValidated;
	vValidated.reserve(vCrumbs.size());
	for (const auto& tCached : vCrumbs)
	{
		if (!tCached.m_pNavArea || !m_pMap->IsAreaValid(tCached.m_pNavArea))
		{
			m_sLastFailureReason = "Crumb graph contains an invalid area";
			return false;
		}

		Crumb_t tCrumb{};
		tCrumb.m_pNavArea = tCached.m_pNavArea;
		tCrumb.m_vPos = tCached.m_vPos;
		tCrumb.m_vApproachDir = tCached.m_vApproachDir;
		tCrumb.m_bRequiresDrop = tCached.m_bRequiresDrop;
		tCrumb.m_flDropHeight = tCached.m_flDropHeight;
		tCrumb.m_flApproachDistance = tCached.m_flApproachDistance;
		vValidated.push_back(tCrumb);
	}

	if (!m_bIgnoreTraces && !vValidated.empty())
	{
		const int iExpire = GetTransientVischeckExpireTick();
		std::lock_guard lock(m_pMap->m_mutex);
		bool bValid = true;

		for (size_t i = 0; i + 1 < vValidated.size(); ++i)
		{
			const auto& a = vValidated[i];
			const auto& b = vValidated[i + 1];
			if (a.m_bRequiresDrop) continue;

			const bool bSameArea = a.m_pNavArea && a.m_pNavArea == b.m_pNavArea;
			if (bSameArea) continue;
			if (b.m_bRequiresDrop) continue;
			if (a.m_pNavArea && b.m_pNavArea && m_pMap->HasDirectConnection(a.m_pNavArea, b.m_pNavArea)) continue;

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
			m_sLastFailureReason = "Path blocked by traces";
			return false;
		}
	}

	m_vCrumbs = std::move(vValidated);
	return true;
}

void CNavEngine::PollPathWorker()
{
	if (!m_pPathWorker) return;
	auto pLocal = H::Entities.GetLocal();

	auto RestorePendingState = [&]()
		{
			if (!m_bHadActivePathBeforePending) return;
			m_eCurrentPriority = m_ePriorityBeforePending;
			m_bRepathOnFail = m_bRepathBeforePending;
			m_bIgnoreTraces = m_bIgnoreTracesBeforePending;
			m_vLastDestination = m_vDestinationBeforePending;
			m_bRepathRequested = false;
		};
	auto FinalizeFailure = [&](const char* sReason, const Vector& vDestination)
		{
			m_sLastFailureReason = sReason;
			m_vFailedDestination = vDestination;
			m_iFailedDestinationTick = TICKCOUNT_TIMESTAMP(0.25f);
			RestorePendingState();
			if (!m_bHadActivePathBeforePending)
			{
				ClearPathState();
				m_eCurrentPriority = PriorityListEnum::None;
				m_bRepathOnFail = false;
				m_bIgnoreTraces = false;
			}
			m_bHadActivePathBeforePending = false;
			m_bRecoveryRetryUsed = false;
		};
	auto RetryFailure = [&](const char* sReason, const Vector& vDestination,
			PriorityListEnum::PriorityListEnum ePriority, bool bShouldRepath, bool bIgnoreTraces, bool bClearTransientCache)
		{
			if (!m_bRecoveryRetryUsed)
			{
				if (bClearTransientCache)
				{
					std::lock_guard lock(m_pMap->m_mutex);
					for (auto it = m_pMap->m_mVischeckCache.begin(); it != m_pMap->m_mVischeckCache.end();)
					{
						if (it->second.m_eVischeckState == VischeckStateEnum::NotVisible && !it->second.m_bStuckBlacklist)
							it = m_pMap->m_mVischeckCache.erase(it);
						else
							++it;
					}
				}

				RestorePendingState();
				m_bRecoveryRetryUsed = true;
				m_bBypassFailedDestination = true;
				const bool bSubmitted = NavTo(vDestination, ePriority, bShouldRepath, bIgnoreTraces);
				m_bBypassFailedDestination = false;
				if (bSubmitted) return true;
			}

			FinalizeFailure(sReason, vDestination);
			return false;
		};

	while (auto oResult = m_pPathWorker->Poll())
	{
		const auto& tResult = *oResult;

		if (tResult.m_uWorldGeneration != m_uWorldGeneration) continue;
		if (tResult.m_uRequestId != m_uPendingRequestId)      continue;
		m_uPendingRequestId = 0;
		m_ePendingPriority = PriorityListEnum::None;

		if (tResult.m_bCancelled)
		{
			m_sLastFailureReason = "Path cancelled";
			RestorePendingState();
			if (!m_bHadActivePathBeforePending)
				m_eCurrentPriority = PriorityListEnum::None;
			m_bHadActivePathBeforePending = false;
			continue;
		}

		if (!pLocal) { m_sLastFailureReason = "No local player"; continue; }

		if (tResult.m_iSolveResult == 1 || tResult.m_iSolveResult == 2)
		{
			const char* sReason = tResult.m_iSolveResult == 1 ? "No solution found" : "Pathing engine error";
			RetryFailure(sReason, m_vPendingDestination, tResult.m_ePriority,
				m_bPendingRepathOnFail, m_bPendingIgnoreTraces, true);
			continue;
		}

		if (!StoreValidatedCrumbs(tResult.m_vCrumbs, pLocal))
		{
			RetryFailure("Path blocked by traces", m_vPendingDestination, tResult.m_ePriority,
				m_bPendingRepathOnFail, m_bPendingIgnoreTraces, false);
			continue;
		}

		m_vLastDestination = m_vPendingDestination;
		m_bRepathOnFail = m_bPendingRepathOnFail;
		m_bIgnoreTraces = m_bPendingIgnoreTraces;
		m_bHadActivePathBeforePending = false;
		m_bRecoveryRetryUsed = false;
		m_vFailedDestination = {};
		m_iFailedDestinationTick = 0;
		m_eCurrentPriority = tResult.m_ePriority;
	}
}

float CNavEngine::GetPathCost(CNavArea* pStartArea, CNavArea* pDestinationArea)
{
	if (!m_pMap || !pStartArea || !pDestinationArea) return FLT_MAX;
	SolveContext tCtx = CMap::BuildSolveContext();
	std::lock_guard lock(m_pMap->m_mutex);
	std::vector<CNavArea*> vPath;
	float flCost = FLT_MAX;
	const int iResult = m_pMap->Solve(pStartArea, pDestinationArea, tCtx, vPath, &flCost);
	return iResult == 0 || iResult == 3 ? flCost : FLT_MAX;
}

float CNavEngine::GetPathCost(const Vector& vStart, const Vector& vDestination, bool bLocal)
{
	if (!IsNavMeshLoaded()) return FLT_MAX;
	CNavArea* pStart = bLocal ? GetLocalNavArea(vStart) : FindClosestNavArea(vStart, false);
	if (!pStart) return FLT_MAX;
	CNavArea* pDest = FindClosestNavArea(vDestination, false);
	if (!pDest) return FLT_MAX;
	SolveContext tCtx = CMap::BuildSolveContext();
	std::lock_guard lock(m_pMap->m_mutex);
	std::vector<CNavArea*> vPath;
	float flCost = FLT_MAX;
	const int iResult = m_pMap->Solve(pStart, pDest, tCtx, vPath, &flCost);
	return iResult == 0 || iResult == 3 ? flCost : FLT_MAX;
}

CNavArea* CNavEngine::GetLocalNavArea(const Vector& vLocalOrigin)
{
	static Timer tRefresh{};
	if (!m_pMap)
	{
		m_pLocalArea = nullptr;
		return nullptr;
	}

	const bool bAreaInvalid = !m_pMap->IsAreaValid(m_pLocalArea);
	const bool bNeedsRefresh = bAreaInvalid
		|| !m_pLocalArea->IsOverlapping(vLocalOrigin)
		|| std::fabs(GetNearestPointOnArea(m_pLocalArea, vLocalOrigin).z - vLocalOrigin.z) > 18.0f
		|| !CMap::CanFallToNavArea(vLocalOrigin, *m_pLocalArea);
	if (bNeedsRefresh || tRefresh.Run(0.10f))
		m_pLocalArea = FindClosestNavArea(vLocalOrigin, true);
	return m_pLocalArea;
}

void CNavEngine::VischeckPath()
{
	static Timer tVischeck{};
	if (m_vCrumbs.size() < 2 || m_bIgnoreTraces) return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal) return;

	const float flInterval = std::clamp(Vars::Misc::Movement::NavEngine::VischeckTime.Value, 0.5f, 3.f);
	if (!tVischeck.Run(flInterval)) return;

	std::lock_guard lock(m_pMap->m_mutex);
	const int iExpire = GetTransientVischeckExpireTick();
	const size_t nMax = std::min<size_t>(m_vCrumbs.size() - 1, 14);
	for (size_t i = 0; i < nMax; ++i)
	{
		const auto& a = m_vCrumbs[i];
		const auto& b = m_vCrumbs[i + 1];
		if (a.m_bRequiresDrop || b.m_bRequiresDrop) continue;
		if (a.m_pNavArea && a.m_pNavArea == b.m_pNavArea) continue;

		const auto tKey = std::pair<CNavArea*, CNavArea*>(a.m_pNavArea, b.m_pNavArea);
		auto it = m_pMap->m_mVischeckCache.find(tKey);
		const bool bConnected = a.m_pNavArea && b.m_pNavArea && m_pMap->HasDirectConnection(a.m_pNavArea, b.m_pNavArea);
		if (it != m_pMap->m_mVischeckCache.end() && it->second.m_iExpireTick > I::GlobalVars->tickcount)
		{
			if (!it->second.m_bPassable && !bConnected)
			{
				AbandonPath("Traceline blocked (cached)");
				break;
			}
			continue;
		}

		if (!IsPlayerPassableNavigation(pLocal, a.m_vPos, b.m_vPos))
		{
			if (bConnected)
				continue;
			auto& tEnt = m_pMap->m_mVischeckCache[tKey];
			tEnt.m_iExpireTick = iExpire;
			tEnt.m_eVischeckState = VischeckStateEnum::NotVisible;
			tEnt.m_bPassable = false;
			tEnt.m_flCachedCost = std::numeric_limits<float>::max();
			AbandonPath("Traceline blocked");
			break;
		}

		auto& tEnt = m_pMap->m_mVischeckCache[tKey];
		tEnt.m_iExpireTick = iExpire;
		tEnt.m_eVischeckState = VischeckStateEnum::Visible;
		tEnt.m_bPassable = true;
	}
}

void CNavEngine::CheckBlacklist(CTFPlayer* pLocal)
{
	static Timer tCheck{};
	if (!tCheck.Run(0.5f) || m_bIgnoreTraces) return;

	F::Hazards.SetIgnoreSentries(m_eCurrentPriority == PriorityListEnum::SnipeSentry
		|| m_eCurrentPriority == PriorityListEnum::Capture);

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

	m_uWorldGeneration++;
	if (m_pPathWorker) m_pPathWorker->CancelAll();
	NavPolicy::Reset();

	if (std::string sLevelName = I::EngineClient->GetLevelName(); !sLevelName.empty())
	{
		if (m_pMap) m_pMap->Reset();

		std::filesystem::path tNavPath = std::filesystem::current_path() / "tf" / sLevelName;
		tNavPath.replace_extension(".nav");
		const std::string sNavPath = tNavPath.string();
		if (bForced || !m_pMap || m_pMap->m_sMapName != sNavPath || m_pMap->m_eState != NavStateEnum::Active)
		{
			if (m_pPathWorker) m_pPathWorker->Stop();
			F::NavBotDanger.ResetSpawn();
			F::Hazards.Reset();
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

bool CNavEngine::IsReady(bool)
{
	static Timer tRestartTimer{};
	if (!Vars::Misc::Movement::NavEngine::Enabled.Value) { tRestartTimer.Update(); return false; }
	if (!tRestartTimer.Check(0.5f))                       return false;
	if (!I::EngineClient->IsInGame())                     return false;
	if (!m_pMap || m_pMap->m_eState != NavStateEnum::Active) return false;
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
	m_tCurrentCrumb = {};
	m_tLastCrumb = {};
	m_vCurrentPathDir = {};
	m_vLastLookTarget = {};
	m_iRecentFallSpeedIndex = 0;
	m_nRecentFallSpeedCount = 0;
	m_vLastStuckSamplePos = {};
	m_flLastDistToCrumb = FLT_MAX;
	m_pLastProgressArea = nullptr;
	m_tStuckFrom = {};
	m_tStuckTo = {};
	m_bHasStuckEdge = false;
	m_tStuckSampleTimer.Update();
	m_tLastProgressTimer.Update();
}

void CNavEngine::AbandonPath(const std::string& sReason)
{
	if (!m_pMap) return;

	m_sLastFailureReason = sReason;
	const bool bStuck = sReason.find("Stuck") != std::string::npos;
	const bool bOffPath = sReason == "Off track";
	if (bStuck)
		RecordStuckFailure();
	ClearPathState();
	m_uPendingRequestId = 0;
	if (m_pPathWorker) m_pPathWorker->CancelAll();
	if (bStuck || (m_bRepathOnFail && !bOffPath) || (bOffPath && Vars::Misc::Movement::NavEngine::OffPathRepath.Value))
	{
		if (SolveInline())
			m_bRepathRequested = false;
		else
		{
			m_bRepathRequested = true;
			const float flDelay = (sReason.find("Blacklisted") != std::string::npos
				|| sReason.find("Stuck") != std::string::npos) ? 0.45f : 0.2f;
			m_iNextRepathTick = std::max(m_iNextRepathTick, TICKCOUNT_TIMESTAMP(flDelay));
		}
	}
	else
	{
		m_eCurrentPriority = PriorityListEnum::None;
	}
}

bool CNavEngine::SolveInline()
{
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !m_pMap || !IsReady()) return false;
	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap) return false;
	if (!GetLocalNavArea()) return false;

	CNavArea* pDestArea = FindClosestNavArea(m_vLastDestination, false);
	if (!pDestArea) return false;

	std::vector<CachedPathCrumb_t> vCrumbs;
	{
		std::lock_guard lock(m_pMap->m_mutex);
		const int iResult = m_pMap->SolveCrumbs(pLocal->GetAbsOrigin(), m_pLocalArea, m_vLastDestination, pDestArea,
			CMap::BuildSolveContext(), vCrumbs, nullptr);
		if (iResult != 0 && iResult != 3) return false;
	}

	if (!StoreValidatedCrumbs(vCrumbs, pLocal) || m_vCrumbs.empty()) return false;

	m_uPendingRequestId = 0;
	return true;
}

void CNavEngine::RecordStuckFailure()
{
	if (!m_pMap) return;

	CNavArea* pFrom = m_bHasStuckEdge ? m_tStuckFrom.m_pNavArea : m_tLastCrumb.m_pNavArea;
	CNavArea* pTo = m_bHasStuckEdge ? m_tStuckTo.m_pNavArea : m_tCurrentCrumb.m_pNavArea;
	pFrom = pFrom ? pFrom : m_pLocalArea;
	pTo = pTo ? pTo : m_pLocalArea;
	if (!pFrom || !pTo)
	{
		m_bHasStuckEdge = false;
		return;
	}

	std::lock_guard lock(m_pMap->m_mutex);
	if (!m_pMap->IsAreaValid(pFrom) || !m_pMap->IsAreaValid(pTo))
	{
		m_bHasStuckEdge = false;
		return;
	}

	const int iNow = I::GlobalVars ? I::GlobalVars->tickcount : 0;
	const int iPenaltyExpire = TICKCOUNT_TIMESTAMP(std::max(Vars::Misc::Movement::NavEngine::StuckExpireTime.Value, 1));
	const int iBlacklistExpire = TICKCOUNT_TIMESTAMP(std::max(Vars::Misc::Movement::NavEngine::StuckBlacklistTime.Value, 1));
	const auto tKey = std::pair<CNavArea*, CNavArea*>(pFrom, pTo);
	auto& tStuck = m_pMap->m_mConnectionStuckTime[tKey];
	if (tStuck.m_iExpireTick != 0 && tStuck.m_iExpireTick <= iNow)
		tStuck.m_iTimeStuck = 0;
	tStuck.m_iTimeStuck++;
	tStuck.m_iExpireTick = iPenaltyExpire;

	if (tStuck.m_iTimeStuck >= 1)
	{
		auto& tEntry = m_pMap->m_mVischeckCache[tKey];
		tEntry.m_iExpireTick = iBlacklistExpire;
		tEntry.m_eVischeckState = VischeckStateEnum::NotVisible;
		tEntry.m_bPassable = false;
		tEntry.m_bStuckBlacklist = true;
		tEntry.m_flCachedCost = std::numeric_limits<float>::max();
	}

	if (tStuck.m_iTimeStuck >= 3)
	{
		auto& tArea = m_pMap->m_mVischeckCache[std::pair<CNavArea*, CNavArea*>(pTo, pTo)];
		tArea.m_iExpireTick = iBlacklistExpire;
		tArea.m_eVischeckState = VischeckStateEnum::NotVisible;
		tArea.m_bPassable = false;
		tArea.m_bStuckBlacklist = true;
		tArea.m_flCachedCost = std::numeric_limits<float>::max();
	}
	m_bHasStuckEdge = false;
}

void CNavEngine::ResetStuckProgress(const Vector& vLocalOrigin, const Vector& vCrumbTarget)
{
	m_vLastStuckSamplePos = vLocalOrigin;
	m_flLastDistToCrumb = (vCrumbTarget - vLocalOrigin).Length();
	m_pLastProgressArea = m_pLocalArea;
	m_tLastProgressTimer.Update();
}

void CNavEngine::CancelPath()
{
	ClearPathState();
	m_eCurrentPriority = PriorityListEnum::None;
	m_bIgnoreTraces = false;
	m_iNextRepathTick = 0;
	m_bRepathRequested = false;
	m_bRepathOnFail = false;
	m_uPendingRequestId = 0;
	m_vPendingDestination = {};
	m_ePendingPriority = PriorityListEnum::None;
	m_bPendingRepathOnFail = false;
	m_bPendingIgnoreTraces = false;
	m_ePriorityBeforePending = PriorityListEnum::None;
	m_bHadActivePathBeforePending = false;
	m_bRecoveryRetryUsed = false;
	m_bBypassFailedDestination = false;
	m_vFailedDestination = {};
	m_iFailedDestinationTick = 0;
	if (m_pPathWorker) m_pPathWorker->CancelAll();
}

void CNavEngine::UpdateRespawnRooms()
{
	if (m_vRespawnRooms.empty() || !m_pMap) return;

	auto IsAreaInRespawnRoom = [](const TriggerData_t& tRoom, const CNavArea& tArea) -> bool
		{
			constexpr float flSampleSpacing = 32.0f;
			const float flWidth = std::fabs(tArea.m_vSeCorner.x - tArea.m_vNwCorner.x);
			const float flHeight = std::fabs(tArea.m_vSeCorner.y - tArea.m_vNwCorner.y);
			const int iXSamples = std::max(static_cast<int>(std::ceil(flWidth / flSampleSpacing)), 1);
			const int iYSamples = std::max(static_cast<int>(std::ceil(flHeight / flSampleSpacing)), 1);

			for (int iX = 0; iX <= iXSamples; ++iX)
			{
				const float flX = std::lerp(tArea.m_vNwCorner.x, tArea.m_vSeCorner.x, static_cast<float>(iX) / iXSamples);
				for (int iY = 0; iY <= iYSamples; ++iY)
				{
					const float flY = std::lerp(tArea.m_vNwCorner.y, tArea.m_vSeCorner.y, static_cast<float>(iY) / iYSamples);
					if (tRoom.PointIsWithin({ flX, flY, tArea.GetZ(flX, flY) + 18.0f }))
						return true;
				}
			}

			return false;
		};

	std::unordered_set<CNavArea*> setSpawnAreas;
	for (const auto& tRoom : m_vRespawnRooms)
	{
		if (!tRoom.tData.m_pModel) continue;

		for (auto& tArea : m_pMap->m_navfile.m_vAreas)
		{
			if (IsAreaInRespawnRoom(tRoom.tData, tArea))
			{
				setSpawnAreas.insert(&tArea);
				const uint32_t uFlags = tRoom.m_iTeam == 0
					? (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED)
					: (tRoom.m_iTeam == TF_TEAM_BLUE ? TF_NAV_SPAWN_ROOM_BLUE : TF_NAV_SPAWN_ROOM_RED);
				tArea.m_iTFAttributeFlags |= uFlags;
			}
		}
	}

	for (auto& tArea : m_pMap->m_navfile.m_vAreas)
		if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE))
			setSpawnAreas.insert(&tArea);

	std::unordered_set<CNavArea*> setExitAreas;
	for (auto pArea : setSpawnAreas)
		for (auto& tConn : pArea->m_vConnections)
			if (tConn.m_pArea
				&& !(tConn.m_pArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)))
			{
				tConn.m_pArea->m_iTFAttributeFlags |= TF_NAV_SPAWN_ROOM_EXIT;
				if (setExitAreas.insert(tConn.m_pArea).second)
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
		std::vector<CachedPathCrumb_t> vRecoveryCrumbs;
		const SolveContext tContext = CMap::BuildSolveContext();
		{
			std::lock_guard lock(m_pMap->m_mutex);
			m_pMap->SolveCrumbs(vLocalOrigin, pArea, vTarget, pRecovery, tContext, vRecoveryCrumbs, nullptr);
		}
		for (const auto& tCached : vRecoveryCrumbs)
		{
			Crumb_t tCrumb{};
			tCrumb.m_pNavArea = tCached.m_pNavArea;
			tCrumb.m_vPos = tCached.m_vPos;
			tCrumb.m_vApproachDir = tCached.m_vApproachDir;
			tCrumb.m_bRequiresDrop = tCached.m_bRequiresDrop;
			tCrumb.m_flDropHeight = tCached.m_flDropHeight;
			tCrumb.m_flApproachDistance = tCached.m_flApproachDistance;
			m_vCrumbs.push_back(tCrumb);
		}
		m_eCurrentPriority = PriorityListEnum::Patrol;
		m_tOffMeshTimer.Update();
	}
}

void CNavEngine::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_bUnstucking = false;
	NavPolicy::Update();
	if (!pLocal)
	{
		CancelPath();
		return;
	}

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
		if (!NavTo(m_vLastDestination, m_eCurrentPriority, true, m_bIgnoreTraces))
			m_iNextRepathTick = std::max(m_iNextRepathTick, TICKCOUNT_TIMESTAMP(0.25f));
	}

	const bool bShouldCancelPath = (m_eCurrentPriority == PriorityListEnum::Engineer
		&& ((!Vars::Aimbot::AutoEngie::AutoRepair.Value && !Vars::Aimbot::AutoEngie::AutoUpgrade.Value)
		|| pLocal->m_iClass() != TF_CLASS_ENGINEER))
		|| (m_eCurrentPriority == PriorityListEnum::Capture
		&& !(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::CaptureObjectives))
		|| (m_eCurrentPriority == PriorityListEnum::EscapeSpawn
		&& !Vars::Misc::Movement::NavBot::EscapeSpawn.Value);
	if (bShouldCancelPath)
	{
		CancelPath();
		return;
	}

	if (!pCmd
		|| (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
		|| !IsReady(true))
		return;

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
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return StuckPhase::Idle;

	const float flSampleTime = std::clamp(Vars::Misc::Movement::NavEngine::StuckTime.Value, 0.1f, 0.9f);
	if (pLocal->OnSolid() && m_tStuckSampleTimer.Check(flSampleTime))
	{
		m_tStuckSampleTimer.Update();

		const float flDistToCrumb = (vCrumbTarget - vLocalOrigin).Length();
		const float flSpeed = pLocal->GetAbsVelocity().Length2D();
		const bool bAreaChanged = m_pLocalArea && m_pLastProgressArea && m_pLocalArea != m_pLastProgressArea;
		const bool bFirstSample = m_flLastDistToCrumb == FLT_MAX || m_vLastStuckSamplePos.IsZero();
		const bool bProgress = bFirstSample
			|| bAreaChanged
			|| flDistToCrumb < m_flLastDistToCrumb - std::max(8.f, flSpeed * flSampleTime * 0.18f);

		m_vLastStuckSamplePos = vLocalOrigin;
		m_flLastDistToCrumb = flDistToCrumb;

		if (bProgress)
		{
			m_pLastProgressArea = m_pLocalArea;
			m_bHasStuckEdge = false;
			m_tLastProgressTimer.Update();
			return StuckPhase::Idle;
		}
	}

	if (!m_tLastProgressTimer.Check(1.5f)) return StuckPhase::Idle;
	if (!m_tLastProgressTimer.Check(1.85f)) return StuckPhase::Jump;
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

	Vector vLookTarget = vMoveTarget;
	Vector vPathDirection = vMoveTarget - pLocal->GetAbsOrigin();
	vPathDirection.z = 0.f;
	const bool bHasPathDirection = vPathDirection.Normalize() > 24.f;
	if (!m_vLastLookTarget.IsZero())
	{
		Vector vLastDirection = m_vLastLookTarget - pLocal->GetAbsOrigin();
		vLastDirection.z = 0.f;
		const bool bLastForward = !bHasPathDirection || vLastDirection.Normalize() <= 24.f || vPathDirection.Dot(vLastDirection) >= 0.2f;
		if (bLastForward)
		{
			const float flBlend = std::clamp(I::GlobalVars->interval_per_tick * 15.f, 0.f, 1.f);
			vLookTarget = m_vLastLookTarget.Lerp(vLookTarget, flBlend);

			Vector vBlendedDirection = vLookTarget - pLocal->GetAbsOrigin();
			vBlendedDirection.z = 0.f;
			if (bHasPathDirection && vBlendedDirection.Normalize() > 24.f && vPathDirection.Dot(vBlendedDirection) < 0.2f)
				vLookTarget = vMoveTarget;
		}
		else
			m_vLastLookTarget = {};
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
	m_bUnstucking = false;

	if (m_vCrumbs.empty())
	{
		if (m_tOffMeshTimer.Check(6.0f))
		{
			m_eCurrentPriority = PriorityListEnum::Patrol;
			SDK::WalkTo(pCmd, pLocal, m_vOffMeshTarget);

			if (!IsPayloadEscortPaceState(pLocal, pLocal->GetAbsOrigin())
				&& pLocal->OnSolid()
				&& NavRuntime::CanIssueNavJump(pWeapon, pCmd))
				pCmd->buttons |= IN_JUMP;
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

	Vector vCrumbTarget{};
	Vector vMoveTarget{};
	Vector vMoveDir{};
	bool bDropCrumb = false;
	size_t nConsumed = 0;
	int iLoopLimit = 32;

	while (iLoopLimit-- > 0)
	{
		const size_t uRemaining = m_vCrumbs.size() - nConsumed;
		if (uRemaining == 0) break;

		auto& tActive = m_vCrumbs[nConsumed];
		const bool bCrumbChanged = m_tCurrentCrumb.m_pNavArea != tActive.m_pNavArea
			|| m_tCurrentCrumb.m_vPos.DistToSqr(tActive.m_vPos) > 1.f;
		m_tCurrentCrumb = tActive;

		bDropCrumb = tActive.m_bRequiresDrop;
		vMoveTarget = vCrumbTarget = tActive.m_vPos;
		if (bCrumbChanged)
			ResetStuckProgress(vLocalOrigin, vCrumbTarget);

		if (bResetHeight)
			vMoveTarget.z = vLocalOrigin.z;

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

		if (flDirLen > 0.01f)
			vMoveDir /= flDirLen;
		else
		{
			vMoveDir = {};
		}
		m_vCurrentPathDir = vMoveDir;

		if (!bDropCrumb && vLocalOrigin.DistToSqr(tActive.m_vPos) < kDefaultReachRadius * kDefaultReachRadius)
		{
			m_tLastCrumb = tActive;
			nConsumed++;
			continue;
		}

		if (!bDropCrumb && uRemaining > 1
			&& vLocalOrigin.DistToSqr(m_vCrumbs[nConsumed + 1].m_vPos) < kDefaultReachRadius * kDefaultReachRadius)
		{
			m_tLastCrumb = m_vCrumbs[nConsumed + 1];
			nConsumed += 2;
			continue;
		}

		if (bDropCrumb)
		{
			const bool bFallen = vLocalOrigin.z <= tActive.m_vPos.z - 18.f;
			const bool bLandedBelow = m_pLocalArea && tActive.m_pNavArea
				&& m_pLocalArea != tActive.m_pNavArea
				&& vLocalOrigin.z < tActive.m_pNavArea->m_flMinZ - 8.f;
			if (bFallen || bLandedBelow)
			{
				m_tLastCrumb = tActive;
				nConsumed++;
				continue;
			}
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
		ResetStuckProgress(vLocalOrigin, vCrumbTarget);
	}
	else
	{
		ePhase = TickStuckSample(vLocalOrigin, vCrumbTarget);
	}
	m_bUnstucking = ePhase != StuckPhase::Idle;

	if (ePhase == StuckPhase::Jump && NavRuntime::CanIssueNavJump(pWeapon, pCmd))
	{
		if (pLocal->OnSolid())
		{
			pCmd->buttons &= ~IN_DUCK;
			pCmd->buttons |= IN_JUMP;
		}
		else
			pCmd->buttons |= IN_DUCK;
	}

	if (ePhase == StuckPhase::Fail)
	{
		AbandonPath(bDropCrumb ? "Stuck on drop" : "Stuck (no progress)");
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
	{
		for (size_t i = 0; i + 1 < m_vCrumbs.size(); ++i)
			H::Draw.RenderLine(m_vCrumbs[i].m_vPos, m_vCrumbs[i + 1].m_vPos, Vars::Colors::NavbotPath.Value, false);
		for (const auto& tCrumb : m_vCrumbs)
			H::Draw.RenderBox(tCrumb.m_vPos, Vector(-3.f, -3.f, -3.f), Vector(3.f, 3.f, 3.f), Vector(), Vars::Colors::NavbotPath.Value, false);
	}

	F::NavBotEngineer.Render();
}
