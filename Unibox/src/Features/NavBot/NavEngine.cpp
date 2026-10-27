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
#include <numeric>

static int GetTransientVischeckExpireTick()
{
	const float flSeconds = std::clamp(static_cast<float>(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value), 10.f, 500.f);
	return TICKCOUNT_TIMESTAMP(flSeconds);
}

static bool IsDoorEntity(CBaseEntity* pEntity)
{
	if (!pEntity)
		return false;

	const auto nClassID = pEntity->GetClassID();
	return nClassID == ETFClassID::CBaseDoor || nClassID == ETFClassID::CBasePropDoor;
}

static constexpr const char* kDestinationBlacklisted = "Destination blacklisted";

static int GetNativeBlockExpireTick(CBaseEntity* pBlocker)
{
	if (!pBlocker || IsDoorEntity(pBlocker))
		return TICKCOUNT_TIMESTAMP(2.f);

	return TICKCOUNT_TIMESTAMP(0.4f);
}

static int GetHullBlockExpireTick(CBaseEntity* pBlocker, bool bGap)
{
	if (!bGap && pBlocker && pBlocker->entindex() > 0)
		return GetNativeBlockExpireTick(pBlocker);

	return GetTransientVischeckExpireTick();
}

static void MarkConnection(CMap& tMap, CNavArea* pFrom, CNavArea* pTo, bool bPassable, int iExpireTick, bool bStuckBlacklist = false)
{
	auto& tEntry = tMap.m_mVischeckCache[std::pair<CNavArea*, CNavArea*>(pFrom, pTo)];
	tEntry.m_iExpireTick = iExpireTick;
	tEntry.m_eVischeckState = bPassable ? VischeckStateEnum::Visible : VischeckStateEnum::NotVisible;
	tEntry.m_bPassable = bPassable;
	tEntry.m_bStuckBlacklist = bStuckBlacklist;
	tEntry.m_flReachSpeed = 0.f;
	if (!bPassable)
		tEntry.m_flCachedCost = std::numeric_limits<float>::max();
}

class CTraceFilterDoors : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pHandleEntity, int) override
	{
		if (!pHandleEntity)
			return false;
		const auto nClassID = reinterpret_cast<CBaseEntity*>(pHandleEntity)->GetClassID();
		return nClassID == ETFClassID::CBaseDoor || nClassID == ETFClassID::CBasePropDoor;
	}
	TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }
};

static Crumb_t ToCrumb(const CachedPathCrumb_t& tCached)
{
	Crumb_t tCrumb{};
	tCrumb.m_pNavArea = tCached.m_pNavArea;
	tCrumb.m_vPos = tCached.m_vPos;
	tCrumb.m_vApproachDir = tCached.m_vApproachDir;
	tCrumb.m_vLanding = tCached.m_vLanding;
	tCrumb.m_bRequiresDrop = tCached.m_bRequiresDrop;
	tCrumb.m_bRequiresJump = tCached.m_bRequiresJump;
	tCrumb.m_bRequiresCrouch = tCached.m_bRequiresCrouch;
	tCrumb.m_bRequiresLadder = tCached.m_bRequiresLadder;
	tCrumb.m_flDropHeight = tCached.m_flDropHeight;
	tCrumb.m_flJumpHeight = tCached.m_flJumpHeight;
	tCrumb.m_flApproachDistance = tCached.m_flApproachDistance;
	return tCrumb;
}

bool CNavEngine::IsPayloadEscortPaceState(CTFPlayer* pLocal, const Vector& vLocalOrigin)
{
	if (!pLocal || F::GameObjectiveController.m_eGameMode != TF_GAMETYPE_ESCORT)
		return false;
	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::Capture)
		return false;

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
	if (iIdx < 0 || iIdx >= static_cast<int>(m_aPayloadPos.size())) return false;

	if (m_aSeenPayload[iIdx])
	{
		if (vPayloadPos.DistToSqr(m_aPayloadPos[iIdx]) >= flMoveThreshold * flMoveThreshold)
			m_aPayloadMoveTime[iIdx] = I::GlobalVars->curtime;
	}
	else
	{
		m_aSeenPayload[iIdx] = true;
	}
	m_aPayloadPos[iIdx] = vPayloadPos;

	return I::GlobalVars->curtime - m_aPayloadMoveTime[iIdx] <= flMoveGrace;
}

CNavEngine::CNavEngine()
	: m_pPathWorker(std::make_unique<PathWorker::CPathWorker>())
{
}

CNavEngine::~CNavEngine() = default;

bool CNavEngine::IsSetupTime()
{
	if (Vars::Misc::Movement::NavEngine::PathInSetup.Value)
		return false;

	if (!m_tSetupCheck.Run(0.5f))
		return m_bSetupTime;

	const auto pGameRules = I::TFGameRules();
	if (!pGameRules || pGameRules->m_bPlayingMannVsMachine())
	{
		m_bSetupTime = false;
		return false;
	}

	const bool bSetupState = pGameRules->m_bInSetup()
		|| pGameRules->m_iRoundState() == GR_STATE_PREROUND
		|| pGameRules->m_bInWaitingForPlayers();
	if (!bSetupState)
	{
		m_bSetupTime = false;
		return false;
	}

	const auto pLocal = H::Entities.GetLocal();
	const int iTeam = pLocal ? pLocal->m_iTeamNum() : 0;
	if (iTeam != TF_TEAM_BLUE && iTeam != TF_TEAM_RED)
	{
		m_bSetupTime = false;
		return false;
	}

	const bool bHasOwnGate = iTeam == TF_TEAM_BLUE ? m_bMapHasBlueSetupGate
		: m_bMapHasRedSetupGate;
	m_bSetupTime = pGameRules->m_iRoundState() == GR_STATE_PREROUND || bHasOwnGate;
	return m_bSetupTime;
}

bool CNavEngine::IsVectorVisibleNavigation(const Vector vFrom, const Vector vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterNavigation filter;
	SDK::Trace(vFrom, vTo, nMask, &filter, &trace);
	return trace.fraction == 1.0f;
}

bool CNavEngine::IsPlayerPassableNavigation(CTFPlayer* pLocal, const Vector vFrom, Vector vTo,
	unsigned int nMask, bool* pbNeedsCrouch, CBaseEntity** ppBlocker)
{
	if (!pLocal) return false;
	if (pbNeedsCrouch) *pbNeedsCrouch = false;
	if (ppBlocker) *ppBlocker = nullptr;

	const Vector vDelta = vTo - vFrom;
	if (vDelta.LengthSqr() < 64.f) return true;
	if (vTo.z - vFrom.z > PLAYER_JUMP_HEIGHT) return false;

	constexpr float flProbeHalfWidth = 12.f;
	const Vector vMins(-flProbeHalfWidth, -flProbeHalfWidth, PLAYER_STEP_HEIGHT);
	const Vector vMaxsStand(flProbeHalfWidth, flProbeHalfWidth, PLAYER_HEIGHT);
	const Vector vMaxsCrouch(flProbeHalfWidth, flProbeHalfWidth, PLAYER_DUCK_HEIGHT);

	CTraceFilterNavigation tFilter(pLocal);
	CGameTrace tTrace{};
	SDK::TraceHull(vFrom, vTo, vMins, vMaxsStand, nMask, &tFilter, &tTrace);
	if (!tTrace.startsolid && !tTrace.allsolid && tTrace.fraction >= 1.f)
		return true;

	CBaseEntity* pStandBlocker = tTrace.m_pEnt;
	SDK::TraceHull(vFrom, vTo, vMins, vMaxsCrouch, nMask, &tFilter, &tTrace);
	if (!tTrace.startsolid && !tTrace.allsolid && tTrace.fraction >= 1.f)
	{
		if (pbNeedsCrouch) *pbNeedsCrouch = true;
		return true;
	}

	if (ppBlocker)
		*ppBlocker = tTrace.m_pEnt ? tTrace.m_pEnt : pStandBlocker;
	return false;
}

bool CNavEngine::NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority, bool bShouldRepath, bool bIgnoreTraces)
{
	if (!m_pMap || !m_pPathWorker) return false;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal) return false;

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap) { m_sLastFailureReason = "Warping/Doubletapping"; return false; }
	if (!pLocal->IsAlive()) { m_sLastFailureReason = "Not alive"; return false; }
	if (NavRuntime::IsMovementLocked(pLocal)) { m_sLastFailureReason = "Movement locked"; return false; }
	if (!IsReady()) { m_sLastFailureReason = "Not ready"; return false; }
	if (!IsPriorityAllowed(ePriority))
	{
		m_sLastFailureReason = "Priority too low";
		return false;
	}
	CNavArea* pStartArea = GetPathStartArea(pLocal, pLocal->GetAbsOrigin());
	if (!pStartArea) { m_sLastFailureReason = "No local nav area"; return false; }

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
		m_bRepathOnFail = bShouldRepath;
		m_sLastFailureReason.clear();
		return true;
	}

	CNavArea* pDestArea = FindClosestNavArea(vDestination, false);
	if (!pDestArea) { m_sLastFailureReason = "No destination nav area"; return false; }

	int iDestBlockExpire = 0;
	if (pDestArea != pStartArea)
	{
		std::lock_guard lock(m_pMap->m_mutex);
		if (m_pMap->GetAreaBlock(pDestArea, I::GlobalVars->tickcount) == CMap::AreaBlock::Stuck)
		{
			if (const auto it = m_pMap->m_mVischeckCache.find(std::pair<CNavArea*, CNavArea*>(pDestArea, pDestArea)); it != m_pMap->m_mVischeckCache.end())
				iDestBlockExpire = it->second.m_iExpireTick;
		}
	}
	if (iDestBlockExpire)
	{
		m_sLastFailureReason = kDestinationBlacklisted;
		if (m_vFailedDestination.DistToSqr(vDestination) > flReuseRadiusSq)
			m_iNoSolutionCount = 0;
		m_vFailedDestination = vDestination;
		m_iFailedDestinationTick = iDestBlockExpire;
		return false;
	}

	m_sLastFailureReason.clear();

	PathWorker::PathRequest tReq{};
	tReq.m_uRequestId = ++m_uNextRequestId;
	tReq.m_uWorldGeneration = m_uWorldGeneration;
	tReq.m_pStartArea = pStartArea;
	tReq.m_pDestArea = pDestArea;
	tReq.m_vStart = pLocal->GetAbsOrigin();
	tReq.m_vDestination = vDestination;
	tReq.m_ePriority = ePriority;
	tReq.m_tCtx = CMap::BuildSolveContext(bIgnoreTraces);

	if (m_uPendingRequestId == 0)
	{
		m_ePriorityBeforePending = m_eCurrentPriority;
		m_bRepathBeforePending = m_bRepathOnFail;
		m_bIgnoreTracesBeforePending = m_bIgnoreTraces;
		m_vDestinationBeforePending = m_vLastDestination;
		m_bHadActivePathBeforePending = !m_vCrumbs.empty();
	}
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

		vValidated.push_back(ToCrumb(tCached));
	}

	auto pWeaponEntity = pLocal ? pLocal->m_hActiveWeapon().Get() : nullptr;
	if (pLocal && pLocal->OnSolid() && !vValidated.front().m_bRequiresJump
		&& NavRuntime::CanUseNavJump(pLocal, pWeaponEntity ? pWeaponEntity->As<CTFWeaponBase>() : nullptr))
	{
		const Vector vLocalOrigin = pLocal->GetAbsOrigin();
		const Crumb_t& tFirst = vValidated.front();
		const float flRise = tFirst.m_pNavArea->GetNearestPoint(Vector2D(vLocalOrigin.x, vLocalOrigin.y)).z - vLocalOrigin.z;
		if (flRise > PLAYER_STEP_HEIGHT && flRise <= TFGame::CROUCH_JUMP_REACH)
		{
			Crumb_t tJump{};
			tJump.m_pNavArea = m_pMap->IsAreaValid(m_pLocalArea) ? m_pLocalArea : tFirst.m_pNavArea;
			tJump.m_vPos = vLocalOrigin;
			tJump.m_vLanding = tFirst.m_vPos;
			tJump.m_bRequiresJump = true;
			tJump.m_flJumpHeight = flRise;
			tJump.m_vApproachDir = tFirst.m_vPos - vLocalOrigin;
			tJump.m_vApproachDir.z = 0.f;
			tJump.m_vApproachDir.Normalize();
			vValidated.insert(vValidated.begin(), tJump);
		}
	}

	if (!m_bIgnoreTraces && !vValidated.empty())
	{
		const int iExpire = GetTransientVischeckExpireTick();
		std::lock_guard lock(m_pMap->m_mutex);
		bool bValid = true;
		int iTraced = 0;

		for (size_t i = 0; i + 1 < vValidated.size() && iTraced < 128; ++i)
		{
			const auto& a = vValidated[i];
			const auto& b = vValidated[i + 1];
			if (a.m_bRequiresDrop || a.m_bRequiresJump || a.m_bRequiresLadder) continue;

			const bool bSameArea = a.m_pNavArea && a.m_pNavArea == b.m_pNavArea;
			if (bSameArea) continue;
			if (b.m_bRequiresDrop || b.m_bRequiresJump || b.m_bRequiresLadder) continue;
			const bool bNative = a.m_pNavArea && b.m_pNavArea && m_pMap->HasDirectConnection(a.m_pNavArea, b.m_pNavArea);

			const auto tKey = std::pair<CNavArea*, CNavArea*>(a.m_pNavArea, b.m_pNavArea);
			auto it = m_pMap->m_mVischeckCache.find(tKey);
			if (it != m_pMap->m_mVischeckCache.end() && it->second.m_iExpireTick > I::GlobalVars->tickcount)
			{
				if (it->second.m_eVischeckState == VischeckStateEnum::NotVisible)
				{
					if (bNative) continue;
					bValid = false;
					break;
				}
				if (it->second.m_eVischeckState == VischeckStateEnum::Visible)
					continue;
			}

			iTraced++;
			bool bNeedsCrouch = false;
			CBaseEntity* pBlocker = nullptr;
			const bool bPassable = IsPlayerPassableNavigation(pLocal, a.m_vPos, b.m_vPos, MASK_PLAYERSOLID, &bNeedsCrouch, &pBlocker);
			if (bNeedsCrouch)
				vValidated[i + 1].m_bRequiresCrouch = true;

			const int iBlockExpire = bNative ? GetNativeBlockExpireTick(pBlocker) : iExpire;
			MarkConnection(*m_pMap, a.m_pNavArea, b.m_pNavArea, bPassable, iBlockExpire);
			if (!bPassable)
			{
				if (bNative) continue;
				bValid = false;
				break;
			}
		}

		if (!bValid)
		{
			m_sLastFailureReason = "Path blocked by traces";
			return false;
		}
	}

	KeepCrumbsOffWalls(pLocal, vValidated, 8);

	if (Vars::Misc::Movement::NavEngine::HullCheck.Value && !m_bIgnoreTraces)
	{
		size_t iFailed = 0;
		bool bGap = false;
		CBaseEntity* pBlocker = nullptr;
		std::lock_guard lock(m_pMap->m_mutex);
		if (!HullCheckCrumbs(pLocal, vValidated, iFailed, bGap, pBlocker)
			&& vValidated[iFailed].m_pNavArea != vValidated[iFailed + 1].m_pNavArea
			&& ConsumeHullReject())
		{
			MarkConnection(*m_pMap, vValidated[iFailed].m_pNavArea, vValidated[iFailed + 1].m_pNavArea, false, GetHullBlockExpireTick(pBlocker, bGap));
			m_sLastFailureReason = bGap ? "Gap in path" : "Hull blocked";
			return false;
		}
	}
	else
		m_vHullDebug.clear();

	m_vCrumbs = std::move(vValidated);
	return true;
}

void CNavEngine::PollPathWorker(CTFPlayer* pLocal)
{
	if (!m_pPathWorker) return;

	auto RestorePendingState = [&]()
		{
			if (!m_bHadActivePathBeforePending) return;
			m_eCurrentPriority = m_ePriorityBeforePending;
			m_bRepathOnFail = m_bRepathBeforePending;
			m_bIgnoreTraces = m_bIgnoreTracesBeforePending;
			m_vLastDestination = m_vDestinationBeforePending;
			m_bRepathRequested = false;
		};
	auto FinalizeFailure = [&](const char* sReason, const Vector& vDestination, bool bNoSolution, int iExpireTick)
		{
			m_sLastFailureReason = sReason;
			if (iExpireTick)
				m_iFailedDestinationTick = iExpireTick;
			else if (bNoSolution)
			{
				m_iNoSolutionCount = m_vFailedDestination.DistToSqr(vDestination) <= 160.f * 160.f ? m_iNoSolutionCount + 1 : 1;
				const float flCooldown = std::min(0.25f * static_cast<float>(1 << std::min(m_iNoSolutionCount - 1, 5)), 5.f);
				m_iFailedDestinationTick = TICKCOUNT_TIMESTAMP(flCooldown);
			}
			else
				m_iFailedDestinationTick = TICKCOUNT_TIMESTAMP(0.25f);
			m_vFailedDestination = vDestination;
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
					EnsureLocalExit();
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
				if (m_sLastFailureReason == kDestinationBlacklisted)
				{
					FinalizeFailure(kDestinationBlacklisted, vDestination, false, m_iFailedDestinationTick);
					return false;
				}
			}

			FinalizeFailure(sReason, vDestination, bClearTransientCache, 0);
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
		if (m_vFailedDestination.DistToSqr(m_vPendingDestination) <= 160.f * 160.f)
		{
			m_vFailedDestination = {};
			m_iFailedDestinationTick = 0;
			m_iNoSolutionCount = 0;
		}
		m_eCurrentPriority = tResult.m_ePriority;
	}
}

float CNavEngine::GetPathCost(CNavArea* pStartArea, CNavArea* pDestinationArea)
{
	if (!m_pMap || !pStartArea || !pDestinationArea) return FLT_MAX;
	const SolveContext tCtx = CMap::BuildSolveContext();
	std::lock_guard lock(m_pMap->m_mutex);
	std::vector<CNavArea*> vPath;
	float flCost = FLT_MAX;
	const int iResult = m_pMap->Solve(pStartArea, pDestinationArea, tCtx, vPath, &flCost);
	return iResult == 0 || iResult == 3 ? flCost : FLT_MAX;
}

bool CNavEngine::GetPathAreas(CNavArea* pStartArea, CNavArea* pDestinationArea, std::vector<CNavArea*>& vOutAreas)
{
	vOutAreas.clear();
	if (!m_pMap || !pStartArea || !pDestinationArea)
		return false;
	const SolveContext tCtx = CMap::BuildSolveContext();
	std::lock_guard lock(m_pMap->m_mutex);
	const int iResult = m_pMap->Solve(pStartArea, pDestinationArea, tCtx, vOutAreas, nullptr);
	return iResult == 0 || iResult == 3;
}

float CNavEngine::GetPathCost(const Vector& vStart, const Vector& vDestination, bool bLocal)
{
	if (!IsNavMeshLoaded()) return FLT_MAX;
	CNavArea* pStart = bLocal ? GetLocalNavArea(vStart) : FindClosestNavArea(vStart, false);
	if (!pStart) return FLT_MAX;
	return GetPathCost(pStart, FindClosestNavArea(vDestination, false));
}

bool CNavEngine::GetPathCostField(CNavArea* pStartArea, std::vector<float>& vOutCost, float flMaxCost, const std::vector<CNavArea*>* pTargets)
{
	vOutCost.clear();
	if (!m_pMap || !m_pMap->IsAreaValid(pStartArea))
		return false;
	const SolveContext tCtx = CMap::BuildSolveContext();
	std::lock_guard lock(m_pMap->m_mutex);
	m_pMap->SolveCostField(pStartArea, tCtx, vOutCost, flMaxCost, pTargets);
	return true;
}

float CNavEngine::GetFieldCost(const std::vector<float>& vCost, CNavArea* pArea) const
{
	if (!m_pMap || !m_pMap->IsAreaValid(pArea))
		return FLT_MAX;
	const size_t uIdx = m_pMap->GetAreaIndex(pArea);
	return uIdx < vCost.size() ? vCost[uIdx] : FLT_MAX;
}

CNavArea* CNavEngine::GetLocalNavArea(const Vector& vLocalOrigin)
{
	if (!m_pMap)
	{
		m_pLocalArea = nullptr;
		return nullptr;
	}

	const bool bAreaInvalid = !m_pMap->IsAreaValid(m_pLocalArea);
	if (bAreaInvalid && m_bLocalAreaSearchFailed && !m_tNoAreaRetry.Check(0.10f))
	{
		m_pLocalArea = nullptr;
		return nullptr;
	}

	const bool bNeedsRefresh = bAreaInvalid
		|| !m_pLocalArea->IsOverlapping(vLocalOrigin, 4.f)
		|| std::fabs(m_pLocalArea->GetNearestPoint(Vector2D(vLocalOrigin.x, vLocalOrigin.y)).z - vLocalOrigin.z) > 18.0f;
	if (!bNeedsRefresh)
		return m_pLocalArea;

	m_pLocalArea = FindClosestNavArea(vLocalOrigin, true);
	m_bLocalAreaSearchFailed = !m_pLocalArea;
	if (m_bLocalAreaSearchFailed)
		m_tNoAreaRetry.Update();
	return m_pLocalArea;
}

bool CNavEngine::HullCheckCrumbs(CTFPlayer* pLocal, std::vector<Crumb_t>& vCrumbs, size_t& iFailed, bool& bGap, CBaseEntity*& pBlocker)
{
	m_vHullDebug.clear();
	iFailed = 0;
	bGap = false;
	pBlocker = nullptr;
	if (!pLocal || vCrumbs.size() < 2)
		return true;

	constexpr size_t nMaxSegments = 12;
	constexpr float flFloorSpacing = 24.f;
	constexpr float flStandTop = PLAYER_HEIGHT - PLAYER_STEP_HEIGHT;
	constexpr float flDuckTop = PLAYER_DUCK_HEIGHT - PLAYER_STEP_HEIGHT;
	const Vector vStepUp = { 0.f, 0.f, PLAYER_STEP_HEIGHT };
	const Vector vFloorDown = { 0.f, 0.f, PLAYER_STEP_HEIGHT + 2.f };
	constexpr float flComfortHalf = HALF_PLAYER_WIDTH + NAV_WALL_CLEARANCE;
	const Vector vMins = { -HALF_PLAYER_WIDTH, -HALF_PLAYER_WIDTH, 0.f };
	const Vector vFloorMins = { -HALF_PLAYER_WIDTH * 0.5f, -HALF_PLAYER_WIDTH * 0.5f, 0.f };
	const Vector vFloorMaxs = { HALF_PLAYER_WIDTH * 0.5f, HALF_PLAYER_WIDTH * 0.5f, 1.f };
	CTraceFilterNavigation tFilter(pLocal);
	auto Sweep = [&](const Vector& vFrom, const Vector& vTo, float flHalfWidth, float flTop, CGameTrace& tTrace)
		{
			SDK::TraceHull(vFrom, vTo, { -flHalfWidth, -flHalfWidth, 0.f }, { flHalfWidth, flHalfWidth, flTop }, MASK_PLAYERSOLID, &tFilter, &tTrace);
			return !tTrace.startsolid && !tTrace.allsolid && tTrace.fraction >= 1.f;
		};

	const size_t nSegments = std::min(vCrumbs.size() - 1, nMaxSegments);
	m_vHullDebug.reserve(nSegments);
	for (size_t i = 0; i < nSegments; ++i)
	{
		const Crumb_t& a = vCrumbs[i];
		Crumb_t& b = vCrumbs[i + 1];

		PathHullSegment_t tSegment{};
		tSegment.m_vStart = a.m_vPos + vStepUp;
		tSegment.m_vEnd = b.m_vPos + vStepUp;
		tSegment.m_vMins = vMins;
		tSegment.m_vMaxs = { HALF_PLAYER_WIDTH, HALF_PLAYER_WIDTH, b.m_bRequiresCrouch ? flDuckTop : flStandTop };
		if (a.m_bRequiresDrop || a.m_bRequiresJump || a.m_bRequiresLadder
			|| b.m_bRequiresDrop || b.m_bRequiresJump || b.m_bRequiresLadder)
		{
			tSegment.m_bSkipped = true;
			m_vHullDebug.push_back(tSegment);
			continue;
		}

		CGameTrace tTrace{};
		bool bCrouch = b.m_bRequiresCrouch;
		bool bNavClear = Sweep(tSegment.m_vStart, tSegment.m_vEnd, NAV_GEN_HALF_WIDTH, bCrouch ? flDuckTop : flStandTop, tTrace);
		if (!bNavClear && !bCrouch)
		{
			CGameTrace tDuckTrace{};
			if (Sweep(tSegment.m_vStart, tSegment.m_vEnd, NAV_GEN_HALF_WIDTH, flDuckTop, tDuckTrace))
				bNavClear = bCrouch = true;
		}

		CBaseEntity* pHullBlocker = nullptr;
		if (!bNavClear)
		{
			tSegment.m_bBlocked = true;
			tSegment.m_vHit = tTrace.endpos;
			pHullBlocker = tTrace.m_pEnt;
		}
		else
		{
			b.m_bRequiresCrouch = bCrouch;
			const float flTop = bCrouch ? flDuckTop : flStandTop;
			tSegment.m_vMaxs.z = flTop;

			auto TryNudge = [&](const CGameTrace& tHit, float flDistance)
				{
					Vector vPush = tHit.plane.normal;
					vPush.z = 0.f;
					if (tHit.startsolid || i + 2 >= vCrumbs.size() || !b.m_pNavArea || vPush.Normalize() <= 0.1f)
						return false;

					Vector vNudged = b.m_vPos + vPush * flDistance;
					vNudged.x = std::clamp(vNudged.x, b.m_pNavArea->m_vNwCorner.x, b.m_pNavArea->m_vSeCorner.x);
					vNudged.y = std::clamp(vNudged.y, b.m_pNavArea->m_vNwCorner.y, b.m_pNavArea->m_vSeCorner.y);
					vNudged.z = b.m_pNavArea->GetZ(vNudged.x, vNudged.y);

					CGameTrace tNudgeTrace{};
					if (!Sweep(tSegment.m_vStart, vNudged + vStepUp, HALF_PLAYER_WIDTH, flTop, tNudgeTrace))
						return false;

					b.m_vPos = vNudged;
					tSegment.m_vEnd = vNudged + vStepUp;
					tSegment.m_bAdjusted = true;
					return true;
				};

			CGameTrace tFullTrace{};
			if (!Sweep(tSegment.m_vStart, tSegment.m_vEnd, HALF_PLAYER_WIDTH, flTop, tFullTrace))
			{
				tSegment.m_vHit = tFullTrace.endpos;
				tSegment.m_bTight = !TryNudge(tFullTrace, flComfortHalf - NAV_GEN_HALF_WIDTH + 2.f);
			}

			CGameTrace tComfortTrace{};
			if (!tSegment.m_bTight && !Sweep(tSegment.m_vStart, tSegment.m_vEnd, flComfortHalf, flTop, tComfortTrace))
			{
				tSegment.m_vHit = tComfortTrace.endpos;
				tSegment.m_bSnug = !TryNudge(tComfortTrace, flComfortHalf - HALF_PLAYER_WIDTH + 2.f);
			}
		}

		const Vector vDelta = b.m_vPos - a.m_vPos;
		const int iSamples = std::max(static_cast<int>(std::ceil(vDelta.Length2D() / flFloorSpacing)), 1);
		for (int iSample = 0; iSample <= iSamples; ++iSample)
		{
			const Vector vPoint = a.m_vPos + vDelta * (static_cast<float>(iSample) / iSamples);
			SDK::TraceHull(vPoint + vStepUp, vPoint - vFloorDown, vFloorMins, vFloorMaxs, MASK_PLAYERSOLID, &tFilter, &tTrace);
			if (!tTrace.startsolid && !tTrace.allsolid && tTrace.fraction >= 1.f)
			{
				tSegment.m_bGap = true;
				if (!tSegment.m_bBlocked)
					tSegment.m_vHit = vPoint;
				break;
			}
		}

		m_vHullDebug.push_back(tSegment);

		const bool bCrossArea = a.m_pNavArea != b.m_pNavArea;
		if ((tSegment.m_bBlocked && bCrossArea) || tSegment.m_bGap)
		{
			iFailed = i;
			bGap = !(tSegment.m_bBlocked && bCrossArea);
			pBlocker = bGap ? nullptr : pHullBlocker;
			return false;
		}
	}

	return true;
}

void CNavEngine::KeepCrumbsOffWalls(CTFPlayer* pLocal, std::vector<Crumb_t>& vCrumbs, int iBudget)
{
	if (!Vars::Misc::Movement::NavEngine::HullCheck.Value || !pLocal || vCrumbs.size() < 2 || F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
		return;

	constexpr size_t nWindow = 16;
	constexpr int nProbes = 8;
	constexpr float flReach = HALF_PLAYER_WIDTH + NAV_WALL_CLEARANCE;
	constexpr float flMaxPush = flReach - NAV_GEN_HALF_WIDTH;
	const Vector vStepUp = { 0.f, 0.f, PLAYER_STEP_HEIGHT };
	const Vector vProbeMins = { -1.f, -1.f, 0.f };
	const Vector vProbeMaxs = { 1.f, 1.f, PLAYER_DUCK_HEIGHT - PLAYER_STEP_HEIGHT };
	const Vector vNavMins = { -NAV_GEN_HALF_WIDTH, -NAV_GEN_HALF_WIDTH, 0.f };
	CTraceFilterNavigation tFilter(pLocal);

	auto IsSpecial = [](const Crumb_t& tCrumb)
		{
			return tCrumb.m_bRequiresDrop || tCrumb.m_bRequiresJump || tCrumb.m_bRequiresLadder;
		};
	auto NavHullClear = [&](const Vector& vFrom, const Vector& vTo, bool bCrouch)
		{
			const Vector vMaxs = { NAV_GEN_HALF_WIDTH, NAV_GEN_HALF_WIDTH, (bCrouch ? PLAYER_DUCK_HEIGHT : PLAYER_HEIGHT) - PLAYER_STEP_HEIGHT };
			CGameTrace tTrace{};
			SDK::TraceHull(vFrom + vStepUp, vTo + vStepUp, vNavMins, vMaxs, MASK_PLAYERSOLID, &tFilter, &tTrace);
			return !tTrace.startsolid && !tTrace.allsolid && tTrace.fraction >= 1.f;
		};

	const size_t nEnd = std::min(vCrumbs.size() - 1, nWindow);
	for (size_t i = 0; i < nEnd && iBudget > 0; ++i)
	{
		Crumb_t& tCrumb = vCrumbs[i];
		if (tCrumb.m_bWallChecked)
			continue;

		tCrumb.m_bWallChecked = true;
		if (!tCrumb.m_pNavArea || IsSpecial(tCrumb) || (i > 0 && IsSpecial(vCrumbs[i - 1])))
			continue;

		iBudget--;
		const Vector vBase = tCrumb.m_vPos + vStepUp;
		Vector vPushDir = {};
		float flDepth = 0.f;
		for (int iProbe = 0; iProbe < nProbes; ++iProbe)
		{
			const float flYaw = static_cast<float>(iProbe) * (2.f * std::numbers::pi_v<float> / nProbes);
			const Vector vDir = { std::cos(flYaw), std::sin(flYaw), 0.f };

			CGameTrace tTrace{};
			SDK::TraceHull(vBase, vBase + vDir * flReach, vProbeMins, vProbeMaxs, MASK_PLAYERSOLID, &tFilter, &tTrace);
			if (tTrace.startsolid || tTrace.allsolid || tTrace.fraction >= 1.f)
				continue;

			const float flPenetration = flReach * (1.f - tTrace.fraction);
			vPushDir -= vDir * flPenetration;
			flDepth = std::max(flDepth, flPenetration);
		}

		if (vPushDir.Normalize() < 1.f || flDepth < 1.f)
			continue;

		CNavArea* pArea = tCrumb.m_pNavArea;
		Vector vMoved = tCrumb.m_vPos + vPushDir * std::min(flDepth, flMaxPush);
		vMoved.x = std::clamp(vMoved.x, pArea->m_vNwCorner.x, pArea->m_vSeCorner.x);
		vMoved.y = std::clamp(vMoved.y, pArea->m_vNwCorner.y, pArea->m_vSeCorner.y);
		vMoved.z = pArea->GetZ(vMoved.x, vMoved.y);
		if (vMoved.DistTo2DSqr(tCrumb.m_vPos) < 1.f)
			continue;

		const Vector vPrev = i > 0 ? vCrumbs[i - 1].m_vPos : pLocal->GetAbsOrigin();
		const Crumb_t& tNext = vCrumbs[i + 1];
		if (NavHullClear(vPrev, vMoved, tCrumb.m_bRequiresCrouch)
			&& (IsSpecial(tNext) || NavHullClear(vMoved, tNext.m_vPos, tNext.m_bRequiresCrouch)))
			tCrumb.m_vPos = vMoved;
	}
}

bool CNavEngine::ConsumeHullReject()
{
	const int iNow = I::GlobalVars->tickcount;
	if (m_iHullRejectExpireTick <= iNow || m_iHullRejectExpireTick > TICKCOUNT_TIMESTAMP(8.f))
		m_iHullRejects = 0;
	if (m_iHullRejects >= 3)
		return false;

	m_iHullRejects++;
	m_iHullRejectExpireTick = TICKCOUNT_TIMESTAMP(8.f);
	return true;
}

void CNavEngine::VischeckPath()
{
	if (!Vars::Misc::Movement::NavEngine::HullCheck.Value)
		m_vHullDebug.clear();
	if (m_vCrumbs.size() < 2 || m_bIgnoreTraces) return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal) return;

	const float flInterval = std::clamp(Vars::Misc::Movement::NavEngine::VischeckTime.Value, 0.5f, 3.f);
	if (!m_tVischeck.Check(flInterval)) return;

	std::unique_lock lock(m_pMap->m_mutex, std::try_to_lock);
	if (!lock) return;
	m_tVischeck.Update();

	const int iExpire = GetTransientVischeckExpireTick();
	const size_t nMax = std::min<size_t>(m_vCrumbs.size() - 1, 14);
	for (size_t i = 0; i < nMax; ++i)
	{
		const auto& a = m_vCrumbs[i];
		const auto& b = m_vCrumbs[i + 1];
		if (a.m_bRequiresDrop || b.m_bRequiresDrop || a.m_bRequiresJump || b.m_bRequiresJump
			|| a.m_bRequiresLadder || b.m_bRequiresLadder) continue;
		if (a.m_pNavArea && a.m_pNavArea == b.m_pNavArea) continue;

		const auto tKey = std::pair<CNavArea*, CNavArea*>(a.m_pNavArea, b.m_pNavArea);
		auto it = m_pMap->m_mVischeckCache.find(tKey);
		const bool bConnected = a.m_pNavArea && b.m_pNavArea && m_pMap->HasDirectConnection(a.m_pNavArea, b.m_pNavArea);
		if (it != m_pMap->m_mVischeckCache.end() && it->second.m_iExpireTick > I::GlobalVars->tickcount)
		{
			if (it->second.m_eVischeckState == VischeckStateEnum::NotVisible && !bConnected)
			{
				AbandonPath("Traceline blocked (cached)");
				return;
			}
			if (it->second.m_eVischeckState != VischeckStateEnum::NotChecked)
				continue;
		}

		bool bNeedsCrouch = false;
		CBaseEntity* pBlocker = nullptr;
		const bool bPassable = IsPlayerPassableNavigation(pLocal, a.m_vPos, b.m_vPos, MASK_PLAYERSOLID, &bNeedsCrouch, &pBlocker);
		if (bNeedsCrouch)
			m_vCrumbs[i + 1].m_bRequiresCrouch = true;

		const int iBlockExpire = bConnected ? GetNativeBlockExpireTick(pBlocker) : iExpire;
		MarkConnection(*m_pMap, a.m_pNavArea, b.m_pNavArea, bPassable, iBlockExpire);
		if (!bPassable)
		{
			if (bConnected) continue;
			AbandonPath("Traceline blocked");
			return;
		}
	}

	if (!Vars::Misc::Movement::NavEngine::HullCheck.Value)
		return;

	size_t iFailed = 0;
	bool bGap = false;
	CBaseEntity* pBlocker = nullptr;
	if (HullCheckCrumbs(pLocal, m_vCrumbs, iFailed, bGap, pBlocker) || !ConsumeHullReject())
		return;

	if (m_vCrumbs[iFailed].m_pNavArea != m_vCrumbs[iFailed + 1].m_pNavArea)
		MarkConnection(*m_pMap, m_vCrumbs[iFailed].m_pNavArea, m_vCrumbs[iFailed + 1].m_pNavArea, false, GetHullBlockExpireTick(pBlocker, bGap));

	auto vHullDebug = std::move(m_vHullDebug);
	AbandonPath(bGap ? "Gap in path" : "Hull blocked");
	if (m_vHullDebug.empty())
		m_vHullDebug = std::move(vHullDebug);
}

static float HazardAbandonThreshold(HazardKind eKind, bool bCapture, bool bSafeCapping)
{
	switch (eKind)
	{
	case HazardKind::Sentry: return 2500.f;
	case HazardKind::EnemyInvuln: return 1200.f;
	case HazardKind::Sticky: return (bCapture && !bSafeCapping) ? 2500.f : 900.f;
	case HazardKind::SentryMedium: return (bCapture && !bSafeCapping) ? 2500.f : 700.f;
	default: return std::numeric_limits<float>::infinity();
	}
}

void CNavEngine::CheckBlacklist(CTFPlayer* pLocal)
{
	if (!m_tBlacklistCheck.Run(0.5f) || m_bIgnoreTraces) return;

	const bool bCapture = m_eCurrentPriority == PriorityListEnum::Capture;
	const bool bSafeCapping = Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SafeCapping;
	F::Hazards.SetIgnoreSentries(m_eCurrentPriority == PriorityListEnum::SnipeSentry || (bCapture && !bSafeCapping));

	F::Hazards.UpdateBotStanding(m_pLocalArea);
	if (F::Hazards.BotStandingOnHazard() || pLocal->IsInvulnerable())
		return;

	const int iNow = I::GlobalVars->tickcount;
	if (iNow - m_iLastBlacklistAbandonTick < TIME_TO_TICKS(0.4f))
		return;

	std::unique_lock lock(m_pMap->m_mutex, std::try_to_lock);
	if (!lock) return;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	for (size_t i = 0; i < m_vCrumbs.size() && i < 20; ++i)
	{
		const auto& tCrumb = m_vCrumbs[i];
		Vector vAhead = tCrumb.m_vPos - vLocalOrigin; vAhead.z = 0.f;
		if (vAhead.LengthSqr() > 1800.f * 1800.f) break;

		const float flHazard = F::Hazards.GetCost(tCrumb.m_pNavArea);
		const Hazard_t* pHazard = F::Hazards.GetHazard(tCrumb.m_pNavArea);
		const float flThreshold = pHazard ? HazardAbandonThreshold(pHazard->m_eKind, bCapture, bSafeCapping) : (bCapture ? 4000.f : 2500.f);

		const char* sReason = nullptr;
		if (!std::isfinite(flHazard))
			sReason = "Hard-blocked area ahead";
		else if (flHazard >= flThreshold)
			sReason = "Hazardous area ahead";
		else if (tCrumb.m_pNavArea && tCrumb.m_pNavArea != m_pLocalArea
			&& m_pMap->GetAreaBlock(tCrumb.m_pNavArea, iNow) == CMap::AreaBlock::Stuck)
			sReason = "Area blacklisted (stuck)";

		if (sReason)
		{
			m_iLastBlacklistAbandonTick = iNow;
			AbandonPath(sReason);
			return;
		}
	}
}

void CNavEngine::Reset(bool bForced)
{
	CancelPath();
	m_vStuckDestination = {};
	m_iStuckDestinationCount = 0;
	m_iStuckDestinationExpireTick = 0;
	m_bIgnoreTraces = false;
	m_iNextRepathTick = 0;
	m_iLastBlacklistAbandonTick = 0;
	m_pLocalArea = nullptr;
	m_pStartAreaCache = nullptr;
	m_bStartAreaCacheValid = false;
	m_vHullDebug.clear();
	m_vBlockedDoors.clear();
	m_bLocalAreaSearchFailed = false;
	m_tOffMeshTimer.Update();
	m_vOffMeshTarget = {};
	m_tNoAreaRetry.Update();
	m_tVischeck.Update();
	m_tBlacklistCheck.Update();
	m_tSetupCheck.Update();
	m_bSetupTime = false;
	m_aPayloadPos = {};
	m_aPayloadMoveTime = {};
	m_aSeenPayload = {};

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

	m_bMapHasBlueSetupGate = false;
	m_bMapHasRedSetupGate = false;
	if (m_pMap && m_pMap->m_eState == NavStateEnum::Active)
	{
		for (const auto& tArea : m_pMap->m_navfile.m_vAreas)
		{
			if (tArea.m_iTFAttributeFlags & TF_NAV_BLUE_SETUP_GATE)
				m_bMapHasBlueSetupGate = true;
			if (tArea.m_iTFAttributeFlags & TF_NAV_RED_SETUP_GATE)
				m_bMapHasRedSetupGate = true;
			if (m_bMapHasBlueSetupGate && m_bMapHasRedSetupGate)
				break;
		}
	}
}

void CNavEngine::Shutdown()
{
	if (m_pPathWorker) m_pPathWorker->Stop();
}

bool CNavEngine::IsReady()
{
	if (!Vars::Misc::Movement::NavEngine::Enabled.Value) { m_tRestartTimer.Update(); return false; }
	if (!m_tRestartTimer.Check(0.5f))                       return false;
	if (!I::EngineClient->IsInGame())                     return false;
	if (!m_pMap || m_pMap->m_eState != NavStateEnum::Active) return false;
	return true;
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
	m_flLadderLastZ = 0.f;
	m_pLastProgressArea = nullptr;
	m_iStuckJumps = 0;
	m_iJumpStartTick = 0;
	m_iDoorWaitStartTick = 0;
	m_iDoorWaitEntIndex = 0;
	m_bHoldingCrouch = false;
	m_vHullDebug.clear();
	m_tStuckSampleTimer.Update();
	m_tLastProgressTimer.Update();
	m_tLadderProgressTimer.Update();
}

void CNavEngine::AbandonPath(const char* sReason, bool bStuck)
{
	if (!m_pMap) return;

	m_sLastFailureReason = sReason;
	bool bGiveUp = false;
	if (bStuck)
	{
		RecordStuckFailure();

		const int iNow = I::GlobalVars ? I::GlobalVars->tickcount : 0;
		if (m_iStuckDestinationCount > 0 && m_iStuckDestinationExpireTick > iNow
			&& m_vStuckDestination.DistToSqr(m_vLastDestination) <= 160.f * 160.f)
			m_iStuckDestinationCount++;
		else
		{
			m_iStuckDestinationCount = 1;
			m_vStuckDestination = m_vLastDestination;
		}
		m_iStuckDestinationExpireTick = TICKCOUNT_TIMESTAMP(std::max(Vars::Misc::Movement::NavEngine::StuckExpireTime.Value, 1)
			+ std::max(Vars::Misc::Movement::NavEngine::StuckBlacklistTime.Value, 1) * std::min(m_iStuckDestinationCount, 5));
		bGiveUp = m_iStuckDestinationCount >= 4;
	}
	ClearPathState();
	m_uPendingRequestId = 0;
	if (m_pPathWorker) m_pPathWorker->CancelAll();
	if (bGiveUp)
	{
		m_sLastFailureReason = "Repeated stuck failures";
		m_vFailedDestination = m_vLastDestination;
		m_iFailedDestinationTick = TICKCOUNT_TIMESTAMP(std::min(2.5f * (m_iStuckDestinationCount - 3), 15.f));
		m_bRepathRequested = false;
		m_bRepathOnFail = false;
		m_eCurrentPriority = PriorityListEnum::None;
	}
	else if (bStuck || m_bRepathOnFail)
	{
		if (SolveInline())
			m_bRepathRequested = false;
		else
		{
			m_bRepathRequested = true;
			m_iNextRepathTick = std::max(m_iNextRepathTick, TICKCOUNT_TIMESTAMP(bStuck ? 0.45f : 0.2f));
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
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	CNavArea* pStartArea = GetPathStartArea(pLocal, vLocalOrigin);
	if (!pStartArea) return false;

	CNavArea* pDestArea = FindClosestNavArea(m_vLastDestination, false);
	if (!pDestArea) return false;

	std::vector<CachedPathCrumb_t> vCrumbs;
	{
		std::lock_guard lock(m_pMap->m_mutex);
		if (pDestArea != pStartArea && m_pMap->GetAreaBlock(pDestArea, I::GlobalVars->tickcount) == CMap::AreaBlock::Stuck)
			return false;
		const int iResult = m_pMap->SolveCrumbs(vLocalOrigin, pStartArea, m_vLastDestination, pDestArea,
			CMap::BuildSolveContext(m_bIgnoreTraces), vCrumbs, nullptr);
		if (iResult != 0 && iResult != 3) return false;
	}

	if (!StoreValidatedCrumbs(vCrumbs, pLocal) || m_vCrumbs.empty()) return false;

	m_uPendingRequestId = 0;
	return true;
}

CNavArea* CNavEngine::GetPathStartArea(CTFPlayer* pLocal, const Vector& vLocalOrigin)
{
	if (!m_pMap || !pLocal)
		return m_pLocalArea;

	const bool bLocalValid = m_pMap->IsAreaValid(m_pLocalArea);
	if (bLocalValid)
	{
		const float flLocalRise = m_pLocalArea->GetNearestPoint(Vector2D(vLocalOrigin.x, vLocalOrigin.y)).z - vLocalOrigin.z;
		if (!pLocal->OnSolid() || (flLocalRise <= PLAYER_STEP_HEIGHT && m_pLocalArea->IsOverlapping(vLocalOrigin, HALF_PLAYER_WIDTH)))
		{
			m_bStartAreaCacheValid = false;
			return m_pLocalArea;
		}
	}
	else if (!pLocal->OnSolid())
		return nullptr;

	const int iNow = I::GlobalVars->tickcount;
	if (m_bStartAreaCacheValid && (!m_pStartAreaCache || m_pMap->IsAreaValid(m_pStartAreaCache))
		&& (iNow == m_iStartAreaCacheTick
			|| (iNow > m_iStartAreaCacheTick && iNow - m_iStartAreaCacheTick <= TIME_TO_TICKS(0.2f)
				&& m_vStartAreaCacheOrigin.DistToSqr(vLocalOrigin) <= 16.f * 16.f)))
		return m_pStartAreaCache;

	const float flTakeoffSpeed = std::max(pLocal->GetAbsVelocity().Length2D(), TFGame::AIR_SPEED_CAP);
	const Vector vMins = pLocal->m_vecMins();
	const Vector vMaxs = pLocal->m_vecMaxs();
	CTraceFilterNavigation filter(pLocal);

	std::lock_guard lock(m_pMap->m_mutex);
	std::vector<CNavArea*> vCandidates;
	m_pMap->CollectAreasAround(vLocalOrigin, 325.f, vCandidates);
	if (bLocalValid && std::find(vCandidates.begin(), vCandidates.end(), m_pLocalArea) == vCandidates.end())
		vCandidates.push_back(m_pLocalArea);

	struct StartCandidate_t { float m_flScore; CNavArea* m_pArea; Vector m_vNearest; };
	std::vector<StartCandidate_t> vScored;
	vScored.reserve(vCandidates.size());
	for (auto* pCand : vCandidates)
	{
		if (!pCand || m_pMap->GetAreaBlock(pCand, iNow) == CMap::AreaBlock::Stuck)
			continue;

		const Vector vNearest = pCand->GetNearestPoint(Vector2D(vLocalOrigin.x, vLocalOrigin.y));
		const float flRise = vNearest.z - vLocalOrigin.z;
		if (flRise > TFGame::CROUCH_JUMP_REACH || flRise < -PLAYER_JUMP_HEIGHT)
			continue;

		const float flPlanar = vNearest.DistTo2D(vLocalOrigin);
		if (flRise > PLAYER_STEP_HEIGHT && !TFGame::CanJumpAcross(flPlanar, flRise, flTakeoffSpeed / TFGame::TAKEOFF_SPEED_SCALE))
			continue;

		const float flUp = std::max(flRise, 0.f);
		float flScore = flPlanar * flPlanar + flRise * flRise * 8.f + flUp * flUp * 4.f;
		if (pCand == m_pLocalArea) flScore *= 0.9f;
		vScored.push_back({ flScore, pCand, vNearest });
	}

	constexpr size_t nMaxTraced = 12;
	const size_t nTraced = std::min(vScored.size(), nMaxTraced);
	std::partial_sort(vScored.begin(), vScored.begin() + nTraced, vScored.end(),
		[](const StartCandidate_t& a, const StartCandidate_t& b) { return a.m_flScore < b.m_flScore; });

	CNavArea* pResult = m_pLocalArea;
	for (size_t i = 0; i < nTraced; ++i)
	{
		const Vector& vNearest = vScored[i].m_vNearest;
		const float flTopZ = std::max(vLocalOrigin.z, vNearest.z) + PLAYER_STEP_HEIGHT;
		const Vector vRaised = { vLocalOrigin.x, vLocalOrigin.y, flTopZ };
		const Vector vAbove = { vNearest.x, vNearest.y, flTopZ };

		CGameTrace trace{};
		SDK::TraceHull(vLocalOrigin + Vector(0.f, 0.f, 1.f), vRaised, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &trace);
		if (trace.startsolid || trace.allsolid || trace.fraction < 1.f)
			continue;

		if (vNearest.DistTo2D(vLocalOrigin) > 1.f)
		{
			SDK::TraceHull(vRaised, vAbove, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &trace);
			if (trace.startsolid || trace.allsolid
				|| (trace.fraction < 1.f && trace.endpos.DistTo2DSqr(vAbove) > HALF_PLAYER_WIDTH * HALF_PLAYER_WIDTH * 0.25f))
				continue;
		}

		pResult = vScored[i].m_pArea;
		break;
	}

	m_pStartAreaCache = pResult;
	m_vStartAreaCacheOrigin = vLocalOrigin;
	m_iStartAreaCacheTick = iNow;
	m_bStartAreaCacheValid = true;
	return pResult;
}

void CNavEngine::RecordStuckFailure()
{
	if (!m_pMap) return;

	std::lock_guard lock(m_pMap->m_mutex);
	auto IsValid = [&](CNavArea* pArea) { return pArea && m_pMap->IsAreaValid(pArea); };

	const bool bTransitionActive = !m_vCrumbs.empty() && IsValid(m_vCrumbs.front().m_pNavArea)
		&& (m_vCrumbs.front().m_bRequiresLadder || m_vCrumbs.front().m_bRequiresJump || m_vCrumbs.front().m_bRequiresDrop);

	CNavArea* pFrom = nullptr;
	if (bTransitionActive)
		pFrom = m_vCrumbs.front().m_pNavArea;
	else if (IsValid(m_tLastCrumb.m_pNavArea))
		pFrom = m_tLastCrumb.m_pNavArea;
	else if (IsValid(m_pLocalArea))
		pFrom = m_pLocalArea;
	else if (!m_vCrumbs.empty() && IsValid(m_vCrumbs.front().m_pNavArea))
		pFrom = m_vCrumbs.front().m_pNavArea;
	if (!pFrom) return;

	size_t iScanStart = 0;
	if (!bTransitionActive && IsValid(m_pLocalArea) && m_pLocalArea != pFrom)
	{
		if (const auto it = std::ranges::find(m_vCrumbs, m_pLocalArea, &Crumb_t::m_pNavArea); it != m_vCrumbs.end())
		{
			pFrom = m_pLocalArea;
			iScanStart = static_cast<size_t>(std::distance(m_vCrumbs.begin(), it));
		}
	}

	CNavArea* pTo = nullptr;
	bool bNearTransition = true;
	for (size_t i = iScanStart; i < m_vCrumbs.size() && !pTo; ++i)
	{
		if (IsValid(m_vCrumbs[i].m_pNavArea) && m_vCrumbs[i].m_pNavArea != pFrom)
		{
			pTo = m_vCrumbs[i].m_pNavArea;
			if (!bTransitionActive)
			{
				auto pLocal = H::Entities.GetLocal();
				const bool bHasTransitionPos = i > 0 || IsValid(m_tLastCrumb.m_pNavArea);
				const Vector& vTransitionPos = i > 0 ? m_vCrumbs[i - 1].m_vPos : m_tLastCrumb.m_vPos;
				bNearTransition = !pLocal || !bHasTransitionPos
					|| pLocal->GetAbsOrigin().DistTo2DSqr(vTransitionPos) <= 128.f * 128.f;
			}
		}
	}
	if (!pTo && IsValid(m_tCurrentCrumb.m_pNavArea) && m_tCurrentCrumb.m_pNavArea != pFrom)
	{
		pTo = m_tCurrentCrumb.m_pNavArea;
		bNearTransition = true;
	}

	const int iNow = I::GlobalVars ? I::GlobalVars->tickcount : 0;
	const int iExpireTime = std::max(Vars::Misc::Movement::NavEngine::StuckExpireTime.Value, 1);
	const int iBlacklistTime = std::max(Vars::Misc::Movement::NavEngine::StuckBlacklistTime.Value, 1);
	auto Strike = [&](CachedStucktime_t& tStuck)
		{
			if (tStuck.m_iExpireTick != 0 && tStuck.m_iExpireTick <= iNow)
				tStuck.m_iTimeStuck = 0;
			tStuck.m_iTimeStuck++;
			tStuck.m_iExpireTick = TICKCOUNT_TIMESTAMP(iExpireTime);
			return tStuck.m_iTimeStuck;
		};

	if (!pTo)
	{
		Strike(m_pMap->m_mConnectionStuckTime[std::pair<CNavArea*, CNavArea*>(pFrom, pFrom)]);
		EnsureLocalExit();
		return;
	}

	auto& tEdgeStuck = m_pMap->m_mConnectionStuckTime[std::pair<CNavArea*, CNavArea*>(pFrom, pTo)];
	const int iEdgeStrikes = Strike(tEdgeStuck);
	if (!bNearTransition)
	{
		EnsureLocalExit();
		return;
	}

	const int iEdgeBlacklist = iBlacklistTime * std::min(iEdgeStrikes, 5);
	tEdgeStuck.m_iExpireTick = TICKCOUNT_TIMESTAMP(iEdgeBlacklist + iExpireTime);
	MarkConnection(*m_pMap, pFrom, pTo, false, TICKCOUNT_TIMESTAMP(iEdgeBlacklist), true);

	auto& tAreaStuck = m_pMap->m_mAreaStuckTime[pTo];
	const int iAreaStrikes = Strike(tAreaStuck);
	const int iAreaBlacklist = iBlacklistTime * std::min(iAreaStrikes, 5);
	tAreaStuck.m_iExpireTick = TICKCOUNT_TIMESTAMP(iAreaBlacklist + iExpireTime);
	if (iAreaStrikes >= 2 && pTo != m_pLocalArea)
		MarkConnection(*m_pMap, pTo, pTo, false, TICKCOUNT_TIMESTAMP(iAreaBlacklist), true);
	EnsureLocalExit();
}

void CNavEngine::EnsureLocalExit()
{
	if (!m_pMap || !m_pMap->IsAreaValid(m_pLocalArea)) return;

	std::lock_guard lock(m_pMap->m_mutex);
	if (!m_pMap->HasUsableExit(m_pLocalArea, I::GlobalVars->tickcount))
		m_pMap->ShortenExitBlocks(m_pLocalArea, TICKCOUNT_TIMESTAMP(5.f));
}

bool CNavEngine::ShouldWaitForDoor(CTFPlayer* pLocal, const Vector& vCrumbTarget)
{
	Vector vOrigin = pLocal->GetAbsOrigin();
	Vector vDir = vCrumbTarget - vOrigin; vDir.z = 0.f;
	if (vDir.Normalize() < 1.f)
	{
		m_iDoorWaitStartTick = 0;
		m_iDoorWaitEntIndex = 0;
		return false;
	}

	CTraceFilterDoors tFilter;
	CGameTrace tTrace{};
	SDK::TraceHull(vOrigin, vOrigin + vDir * 64.f, pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &tFilter, &tTrace);
	if (!tTrace.DidHit())
	{
		m_iDoorWaitStartTick = 0;
		m_iDoorWaitEntIndex = 0;
		return false;
	}

	const int iNow = I::GlobalVars->tickcount;
	const int iDoorEntIndex = tTrace.m_pEnt ? tTrace.m_pEnt->entindex() : 0;
	if (iDoorEntIndex != m_iDoorWaitEntIndex)
	{
		m_iDoorWaitStartTick = iNow;
		m_iDoorWaitEntIndex = iDoorEntIndex;
	}
	if (!m_iDoorWaitStartTick)
		m_iDoorWaitStartTick = iNow;
	if (tTrace.m_pEnt && tTrace.m_pEnt->GetAbsVelocity().LengthSqr() > 1.f
		&& iNow - m_iDoorWaitStartTick < TIME_TO_TICKS(8.f))
		return true;
	if (iNow - m_iDoorWaitStartTick < TIME_TO_TICKS(4.f))
		return true;

	BlockDoor(tTrace.m_pEnt);
	return false;
}

void CNavEngine::BlockDoor(CBaseEntity* pDoor)
{
	if (!m_pMap || !pDoor)
		return;

	const int iIndex = pDoor->entindex();
	auto it = std::ranges::find(m_vBlockedDoors, iIndex, &BlockedDoor_t::m_iEntIndex);
	if (it == m_vBlockedDoors.end())
	{
		m_vBlockedDoors.emplace_back();
		it = m_vBlockedDoors.end() - 1;
		it->m_iEntIndex = iIndex;
	}
	if (it->m_bActive)
		return;

	const Vector vOrigin = pDoor->GetAbsOrigin();
	const Vector vMins = vOrigin + pDoor->m_vecMins() - Vector(12.f, 12.f, 0.f);
	const Vector vMaxs = vOrigin + pDoor->m_vecMaxs() + Vector(12.f, 12.f, 0.f);

	std::lock_guard lock(m_pMap->m_mutex);

	std::vector<CNavArea*> vCandidates;
	const Vector vCenter = (vMins + vMaxs) * 0.5f;
	m_pMap->m_navfile.QueryOverlapping(vCenter, (vMaxs - vMins).Length() * 0.5f + 64.f, vCandidates);

	it->m_vAreas.clear();
	it->m_iStrikes++;
	it->m_vClosedOrigin = vOrigin;
	it->m_iExpireTick = TICKCOUNT_TIMESTAMP(30.f * std::min(it->m_iStrikes, 6));
	it->m_bActive = true;

	for (auto pArea : vCandidates)
	{
		const float flMinX = std::min(pArea->m_vNwCorner.x, pArea->m_vSeCorner.x), flMaxX = std::max(pArea->m_vNwCorner.x, pArea->m_vSeCorner.x);
		const float flMinY = std::min(pArea->m_vNwCorner.y, pArea->m_vSeCorner.y), flMaxY = std::max(pArea->m_vNwCorner.y, pArea->m_vSeCorner.y);
		if (flMaxX < vMins.x || flMinX > vMaxs.x || flMaxY < vMins.y || flMinY > vMaxs.y
			|| pArea->m_flMaxZ < vMins.z || pArea->m_flMinZ > vMaxs.z)
			continue;

		it->m_vAreas.push_back(pArea);
		MarkConnection(*m_pMap, pArea, pArea, false, it->m_iExpireTick, true);
	}

	if (Vars::Debug::Logging.Value)
		SDK::Output("NavEngine", std::format("Blocked closed door #{} ({} areas)", iIndex, it->m_vAreas.size()).c_str(), { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
}

void CNavEngine::ReleaseBlockedDoor(BlockedDoor_t& tDoor)
{
	tDoor.m_bActive = false;
	if (!m_pMap)
		return;

	std::lock_guard lock(m_pMap->m_mutex);
	for (auto pArea : tDoor.m_vAreas)
	{
		const auto it = m_pMap->m_mVischeckCache.find(std::pair<CNavArea*, CNavArea*>(pArea, pArea));
		if (it != m_pMap->m_mVischeckCache.end() && it->second.m_bStuckBlacklist && !it->second.m_bPassable)
			m_pMap->m_mVischeckCache.erase(it);
	}
	tDoor.m_vAreas.clear();
}

void CNavEngine::UpdateBlockedDoors()
{
	if (m_vBlockedDoors.empty() || !m_tDoorUpdateTimer.Run(0.25f))
		return;

	const int iNow = I::GlobalVars->tickcount;
	for (auto& tDoor : m_vBlockedDoors)
	{
		if (!tDoor.m_bActive)
			continue;

		auto pEntity = I::ClientEntityList->GetClientEntity(tDoor.m_iEntIndex);
		auto pDoor = pEntity ? pEntity->As<CBaseEntity>() : nullptr;
		const bool bGone = !pDoor;
		const bool bMoved = pDoor && !pDoor->IsDormant()
			&& (pDoor->GetAbsVelocity().LengthSqr() > 1.f
				|| pDoor->GetAbsOrigin().DistToSqr(tDoor.m_vClosedOrigin) > 8.f * 8.f);
		if (bGone || bMoved || iNow >= tDoor.m_iExpireTick)
			ReleaseBlockedDoor(tDoor);
	}

	std::erase_if(m_vBlockedDoors, [iNow](const BlockedDoor_t& tDoor)
		{
			return !tDoor.m_bActive && iNow >= tDoor.m_iExpireTick;
		});
}

void CNavEngine::ResetStuckProgress(const Vector& vLocalOrigin, const Vector& vCrumbTarget)
{
	m_vLastStuckSamplePos = vLocalOrigin;
	m_flLastDistToCrumb = (vCrumbTarget - vLocalOrigin).Length();
	m_pLastProgressArea = m_pLocalArea;
	m_iStuckJumps = 0;
	m_iDoorWaitStartTick = 0;
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
	m_iNoSolutionCount = 0;
	m_bStartAreaCacheValid = false;
	if (m_pPathWorker) m_pPathWorker->CancelAll();
}

void CNavEngine::UpdateRespawnRooms()
{
	if (!m_pMap) return;

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

	std::lock_guard lock(m_pMap->m_mutex);
	m_vRespawnRoomExitAreas.clear();
	for (auto& tArea : m_pMap->m_navfile.m_vAreas)
		tArea.m_iTFAttributeFlags &= ~TF_NAV_SPAWN_ROOM_EXIT;

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

	BridgeSpawnIslands(H::Entities.GetLocal());

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

	m_pMap->m_mVischeckCache.clear();
	m_pMap->m_mConnectionStuckTime.clear();
	m_pMap->m_mAreaStuckTime.clear();
	m_bUpdatedRespawnRooms = true;
}

void CNavEngine::BridgeSpawnIslands(CTFPlayer* pLocal)
{
	auto& vAreas = m_pMap->m_navfile.m_vAreas;
	const size_t nAreas = vAreas.size();
	if (!pLocal || !nAreas)
		return;

	constexpr float flMaxGap = 160.f;
	constexpr int iMaxPasses = 3;
	const CNavArea* pFirst = vAreas.data();
	auto IndexOf = [&](const CNavArea* pArea) { return static_cast<size_t>(pArea - pFirst); };

	auto Connect = [](CNavArea* pFrom, CNavArea* pTo)
		{
			if (std::ranges::any_of(pFrom->m_vConnections, [&](const NavConnect_t& tConn) { return tConn.m_pArea == pTo; }))
				return;

			NavConnect_t tConn{};
			tConn.m_uId = pTo->m_uId;
			tConn.m_pArea = pTo;
			pFrom->m_vConnections.push_back(tConn);

			const Vector vDelta = pTo->m_vCenter - pFrom->m_vCenter;
			const int iDir = std::fabs(vDelta.x) > std::fabs(vDelta.y) ? (vDelta.x > 0.f ? 1 : 3) : (vDelta.y > 0.f ? 2 : 0);
			pFrom->m_vConnectionsDir[iDir].push_back(tConn);
			pFrom->m_uConnectionCount++;
		};

	CTraceFilterNavigation tFloorFilter(pLocal);
	auto HasFloor = [&](const Vector& vFrom, const Vector& vTo)
		{
			const int iSamples = std::max(static_cast<int>(std::ceil((vTo - vFrom).Length2D() / 16.f)), 1);
			for (int iSample = 0; iSample <= iSamples; ++iSample)
			{
				const Vector vPoint = vFrom + (vTo - vFrom) * (static_cast<float>(iSample) / iSamples);
				CGameTrace tTrace{};
				SDK::Trace(vPoint + Vector(0.f, 0.f, PLAYER_STEP_HEIGHT), vPoint - Vector(0.f, 0.f, PLAYER_STEP_HEIGHT * 2.f),
					MASK_PLAYERSOLID, &tFloorFilter, &tTrace);
				if (tTrace.startsolid || tTrace.fraction >= 1.f || tTrace.endpos.z < vPoint.z - PLAYER_STEP_HEIGHT)
					return false;
			}
			return true;
		};

	int iBridges = 0;
	for (int iPass = 0; iPass < iMaxPasses; ++iPass)
	{
		std::vector<size_t> vParent(nAreas);
		std::iota(vParent.begin(), vParent.end(), size_t{ 0 });
		auto Find = [&](size_t i)
			{
				while (vParent[i] != i)
					i = vParent[i] = vParent[vParent[i]];
				return i;
			};

		for (size_t i = 0; i < nAreas; ++i)
			for (const auto& tConn : vAreas[i].m_vConnections)
				if (tConn.m_pArea && tConn.m_pArea >= pFirst && tConn.m_pArea < pFirst + nAreas)
					vParent[Find(i)] = Find(IndexOf(tConn.m_pArea));

		std::vector<size_t> vSize(nAreas, 0);
		std::vector<char> vSpawn(nAreas, 0);
		for (size_t i = 0; i < nAreas; ++i)
		{
			const size_t uRoot = Find(i);
			vSize[uRoot]++;
			if (vAreas[i].m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE))
				vSpawn[uRoot] = 1;
		}

		struct Bridge_t { CNavArea* m_pFrom = nullptr; CNavArea* m_pTo = nullptr; float m_flScore = FLT_MAX; Vector m_vFrom = {}; Vector m_vTo = {}; };
		std::unordered_map<size_t, std::unordered_map<size_t, Bridge_t>> mBridges;

		std::vector<CNavArea*> vNear;
		for (size_t i = 0; i < nAreas; ++i)
		{
			CNavArea* pArea = &vAreas[i];
			const size_t uRoot = Find(i);
			if (!vSpawn[uRoot])
				continue;

			const float flHalfDiag = (pArea->m_vSeCorner - pArea->m_vNwCorner).Length2D() * 0.5f;
			m_pMap->m_navfile.QueryOverlapping(pArea->m_vCenter, flHalfDiag + flMaxGap + 64.f, vNear);
			for (auto pOther : vNear)
			{
				const size_t uOtherRoot = Find(IndexOf(pOther));
				if (uOtherRoot == uRoot || vSize[uOtherRoot] <= vSize[uRoot])
					continue;

				Vector vFrom = pArea->GetNearestPoint(Vector2D(pOther->m_vCenter.x, pOther->m_vCenter.y));
				const Vector vTo = pOther->GetNearestPoint(Vector2D(vFrom.x, vFrom.y));
				vFrom = pArea->GetNearestPoint(Vector2D(vTo.x, vTo.y));

				const float flGap = (vFrom - vTo).Length2D();
				const float flDeltaZ = std::fabs(vFrom.z - vTo.z);
				if (flGap > flMaxGap || flDeltaZ > PLAYER_JUMP_HEIGHT)
					continue;

				const float flScore = flGap + flDeltaZ * 2.f;
				auto& tBest = mBridges[uRoot][uOtherRoot];
				if (flScore >= tBest.m_flScore || !IsPlayerPassableNavigation(pLocal, vFrom, vTo))
					continue;

				tBest = { pArea, pOther, flScore, vFrom, vTo };
			}
		}

		int iPassBridges = 0;
		for (auto& [uRoot, mTargets] : mBridges)
		{
			std::vector<Bridge_t> vBest;
			for (auto& [uTarget, tBridge] : mTargets)
				if (tBridge.m_pFrom)
					vBest.push_back(tBridge);
			std::ranges::sort(vBest, {}, &Bridge_t::m_flScore);

			for (size_t i = 0; i < vBest.size() && i < 2; ++i)
			{
				Connect(vBest[i].m_pFrom, vBest[i].m_pTo);
				Connect(vBest[i].m_pTo, vBest[i].m_pFrom);
				if (HasFloor(vBest[i].m_vFrom, vBest[i].m_vTo))
				{
					m_pMap->m_setWalkableBridges.emplace(vBest[i].m_pFrom, vBest[i].m_pTo);
					m_pMap->m_setWalkableBridges.emplace(vBest[i].m_pTo, vBest[i].m_pFrom);
				}
				iPassBridges++;
			}
		}

		iBridges += iPassBridges;
		if (!iPassBridges)
			break;
	}

	if (iBridges && Vars::Debug::Logging.Value)
		SDK::Output("NavEngine", std::format("Bridged {} isolated spawn connections", iBridges).c_str(), { 50, 255, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
}

void CNavEngine::RecoverOffMesh(CTFPlayer* pLocal, CNavArea* pArea, const Vector& vLocalOrigin)
{
	if (!m_tOffMeshRecoverTimer.Run(0.25f)) return;

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
		Vector vCandTarget = pCand->GetNearestPoint(Vector2D(vLocalOrigin.x, vLocalOrigin.y));
		const float flOutside = pCand->GetVerticalOutside(vLocalOrigin);
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
		std::vector<CachedPathCrumb_t> vRecoveryCrumbs;
		if (!pArea)
		{
			CachedPathCrumb_t tStart{};
			tStart.m_pNavArea = pRecovery;
			tStart.m_vPos = vLocalOrigin;
			vRecoveryCrumbs.push_back(tStart);

			CachedPathCrumb_t tEnd{};
			tEnd.m_pNavArea = pRecovery;
			tEnd.m_vPos = vTarget;
			vRecoveryCrumbs.push_back(tEnd);
		}
		else
		{
			const SolveContext tContext = CMap::BuildSolveContext();
			std::lock_guard lock(m_pMap->m_mutex);
			m_pMap->SolveCrumbs(vLocalOrigin, pArea, vTarget, pRecovery, tContext, vRecoveryCrumbs, nullptr);
		}
		if (vRecoveryCrumbs.empty())
			return;

		m_vCrumbs.clear();
		for (const auto& tCached : vRecoveryCrumbs)
			m_vCrumbs.push_back(ToCrumb(tCached));
		m_vLastDestination = vTarget;
		m_eCurrentPriority = PriorityListEnum::Patrol;
		m_tOffMeshTimer.Update();
	}
}

void CNavEngine::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_bUnstucking = false;
	NavPolicy::Update();
	if (const uint32_t uEpoch = NavPolicy::GetEpoch(); uEpoch != m_uPolicyEpoch)
	{
		m_uPolicyEpoch = uEpoch;
		if (m_pMap) m_pMap->ClearDynamicBlocks();
		m_vBlockedDoors.clear();
		m_vFailedDestination = {};
		m_iFailedDestinationTick = 0;
		m_iNoSolutionCount = 0;
		m_vStuckDestination = {};
		m_iStuckDestinationCount = 0;
		m_iStuckDestinationExpireTick = 0;
		if (IsPathing())
			CancelPath();
	}
	if (!pLocal)
	{
		CancelPath();
		return;
	}

	if (!Vars::Misc::Movement::NavEngine::Enabled.Value) m_bWasEnabled = false;
	else if (I::EngineClient->IsInGame() && !m_bWasEnabled)
	{
		m_bWasEnabled = true;
		Reset(true);
	}

	if (!m_bUpdatedRespawnRooms) UpdateRespawnRooms();

	UpdateBlockedDoors();
	PollPathWorker(pLocal);

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
		if (!NavTo(m_vLastDestination, m_eCurrentPriority, m_bRepathOnFail, m_bIgnoreTraces))
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
		|| !IsReady())
		return;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	CNavArea* pArea = GetLocalNavArea(vLocalOrigin);
	const bool bOnNavMesh = pArea
		&& pArea->IsOverlapping(vLocalOrigin)
		&& pArea->GetVerticalOutside(vLocalOrigin) <= 18.0f
		&& std::fabs(pArea->GetNearestPoint(Vector2D(vLocalOrigin.x, vLocalOrigin.y)).z - vLocalOrigin.z) < 18.0f;

	if (bOnNavMesh || IsPathing())
		m_tOffMeshTimer.Update();
	else
		RecoverOffMesh(pLocal, pArea, vLocalOrigin);

	if (Vars::Misc::Movement::NavEngine::VischeckEnabled.Value && !F::Ticks.m_bWarp && !F::Ticks.m_bDoubletap)
		VischeckPath();

	KeepCrumbsOffWalls(pLocal, m_vCrumbs, 2);
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

	if (!pLocal->OnSolid())
	{
		m_vLastStuckSamplePos = vLocalOrigin;
		m_flLastDistToCrumb = (vCrumbTarget - vLocalOrigin).Length();
		m_tLastProgressTimer.Update();
		return StuckPhase::Idle;
	}

	const float flSampleTime = std::clamp(Vars::Misc::Movement::NavEngine::StuckTime.Value, 0.1f, 0.9f);
	if (m_tStuckSampleTimer.Check(flSampleTime))
	{
		m_tStuckSampleTimer.Update();

		const float flDistToCrumb = (vCrumbTarget - vLocalOrigin).Length();
		const float flSpeed = pLocal->GetAbsVelocity().Length2D();
		bool bAreaAdvanced = false;
		if (m_pLocalArea && m_pLastProgressArea && m_pLocalArea != m_pLastProgressArea)
		{
			const size_t nScan = std::min<size_t>(m_vCrumbs.size(), 8);
			for (size_t i = 0; i < nScan && !bAreaAdvanced; ++i)
				bAreaAdvanced = m_vCrumbs[i].m_pNavArea == m_pLocalArea;
		}
		const bool bFirstSample = m_flLastDistToCrumb == FLT_MAX || m_vLastStuckSamplePos.IsZero();
		const float flMoved = bFirstSample ? 0.f : vLocalOrigin.DistTo2D(m_vLastStuckSamplePos);
		const bool bProgress = bFirstSample
			|| bAreaAdvanced
			|| flMoved >= std::max(4.f, flSpeed * flSampleTime * 0.45f)
			|| flDistToCrumb < m_flLastDistToCrumb - std::max(12.f, flSpeed * flSampleTime * 0.35f);

		m_vLastStuckSamplePos = vLocalOrigin;
		m_flLastDistToCrumb = flDistToCrumb;

		if (bProgress)
		{
			m_iStuckJumps = 0;
			m_pLastProgressArea = m_pLocalArea;
			m_tLastProgressTimer.Update();
			return StuckPhase::Idle;
		}
	}

	const float flDetectTime = std::max(Vars::Misc::Movement::NavEngine::StuckDetectTime.Value, flSampleTime);
	if (m_iStuckJumps > 0)
		return m_tLastProgressTimer.Check(std::max(flDetectTime * 0.5f, 0.3f)) ? StuckPhase::Fail : StuckPhase::Idle;
	if (!m_tLastProgressTimer.Check(flDetectTime)) return StuckPhase::Idle;
	if (!m_tLastProgressTimer.Check(flDetectTime + 0.35f)) return StuckPhase::Jump;
	return StuckPhase::Fail;
}

void CNavEngine::DoLookAtPath(CTFPlayer* pLocal, CUserCmd* pCmd, const Vector& vMoveTarget, bool bHaveTarget, bool bIncludePitch)
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

	if (bIncludePitch && bHaveTarget)
	{
		const Vec3 vEye = pLocal->GetEyePosition();
		const Vec3 vAngles = Math::CalcAngle(vEye, vMoveTarget);
		if (bLegit)
			F::BotUtils.LookLegit(pLocal, pCmd, vMoveTarget, bSilent);
		else
		{
			F::BotUtils.InvalidateLLAP();
			if (bSilent || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Off)
				pCmd->viewangles = vAngles;
			else
				F::BotUtils.LookAtPath(pCmd, vAngles, vEye, false, false);
		}
		m_vLastLookTarget = vMoveTarget;
		return;
	}

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
	const bool bHasPathDirection = vPathDirection.Length2D() > 24.f;
	if (bHasPathDirection)
		vPathDirection.Normalize();
	if (!m_vLastLookTarget.IsZero())
	{
		Vector vLastDirection = m_vLastLookTarget - pLocal->GetAbsOrigin();
		vLastDirection.z = 0.f;
		const bool bHasLastDirection = vLastDirection.Length2D() > 24.f;
		if (bHasLastDirection)
			vLastDirection.Normalize();
		const bool bLastForward = !bHasPathDirection || !bHasLastDirection || vPathDirection.Dot(vLastDirection) >= 0.2f;
		if (bLastForward)
		{
			const float flBlend = std::clamp(I::GlobalVars->interval_per_tick * 15.f, 0.f, 1.f);
			vLookTarget = m_vLastLookTarget.Lerp(vLookTarget, flBlend);

			Vector vBlendedDirection = vLookTarget - pLocal->GetAbsOrigin();
			vBlendedDirection.z = 0.f;
			if (bHasPathDirection && vBlendedDirection.Length2D() > 24.f && vPathDirection.Dot(vBlendedDirection.Normalized()) < 0.2f)
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
		const Vec3 vEye = pLocal->GetEyePosition();
		F::BotUtils.LookAtPath(pCmd, Vec2(vLookTarget.x, vLookTarget.y), vEye, bSilent);
	}
}

void CNavEngine::FollowCrumbs(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_bUnstucking = false;

	if (m_vCrumbs.empty())
	{
		if (m_iNavJumpTick && !pLocal->OnSolid())
		{
			pCmd->buttons |= IN_DUCK;
			m_bHoldingCrouch = true;
		}
		else
		{
			m_iNavJumpTick = 0;
			if (m_bHoldingCrouch)
			{
				pCmd->buttons &= ~IN_DUCK;
				m_bHoldingCrouch = false;
			}
		}

		if (m_uPendingRequestId != 0 || m_bRepathRequested)
		{
			DoLookAtPath(pLocal, pCmd, {}, false);
			return;
		}

		if (m_tOffMeshTimer.Check(6.0f) && !m_vOffMeshTarget.IsZero())
		{
			m_eCurrentPriority = PriorityListEnum::Patrol;
			SDK::WalkTo(pCmd, pLocal, m_vOffMeshTarget);

			if (!IsPayloadEscortPaceState(pLocal, pLocal->GetAbsOrigin())
				&& pLocal->OnSolid()
				&& NavRuntime::CanIssueNavJump(pWeapon, pCmd))
			{
				pCmd->buttons &= ~IN_DUCK;
				pCmd->buttons |= IN_JUMP;
				m_iNavJumpTick = I::GlobalVars->tickcount;
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

	constexpr float kReachRadius = 40.f;
	constexpr float kJumpReachRadius = HALF_PLAYER_WIDTH;
	constexpr float kLadderReachRadius = 56.f;

	Vector vCrumbTarget{};
	Vector vMoveTarget{};
	Vector vLookTarget{};
	Vector vMoveDir{};
	bool bDropCrumb = false;
	bool bJumpCrumb = false;
	bool bLadderCrumb = false;
	bool bLookPitch = false;
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
		bJumpCrumb = tActive.m_bRequiresJump;
		bLadderCrumb = tActive.m_bRequiresLadder;
		vMoveTarget = vCrumbTarget = tActive.m_vPos;
		vLookTarget = vMoveTarget;
		bLookPitch = false;
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

		if (bJumpCrumb)
		{
			const bool bReachedLanding = uRemaining > 1 && m_pLocalArea
				&& m_vCrumbs[nConsumed + 1].m_pNavArea == m_pLocalArea
				&& vLocalOrigin.z >= m_pLocalArea->GetNearestPoint(Vector2D(vLocalOrigin.x, vLocalOrigin.y)).z - PLAYER_STEP_HEIGHT;
			if (!pLocal->OnSolid() || bReachedLanding)
			{
				m_tLastCrumb = tActive;
				m_iJumpStartTick = 0;
				nConsumed++;
				continue;
			}
		}

		if (bLadderCrumb)
		{
			const bool bUp = tActive.m_vLanding.z > tActive.m_vPos.z + 8.f;
			const bool bAtEndHeight = bUp
				? vLocalOrigin.z >= tActive.m_vLanding.z - 8.f
				: vLocalOrigin.z <= tActive.m_vLanding.z + 8.f;
			const bool bNearEnd = vLocalOrigin.DistTo2D(tActive.m_vLanding) <= 128.f;
			const bool bReachedArea = uRemaining > 1 && m_pLocalArea && m_vCrumbs[nConsumed + 1].m_pNavArea == m_pLocalArea
				&& (bUp ? vLocalOrigin.z >= tActive.m_vLanding.z - 24.f : vLocalOrigin.z <= tActive.m_vLanding.z + 24.f);
			if (((bAtEndHeight && bNearEnd) || bReachedArea) && (pLocal->OnSolid() || bReachedArea))
			{
				m_tLastCrumb = tActive;
				nConsumed++;
				continue;
			}
		}

		if (!bDropCrumb && !bJumpCrumb && !bLadderCrumb && vLocalOrigin.DistToSqr(tActive.m_vPos) < kReachRadius * kReachRadius)
		{
			m_tLastCrumb = tActive;
			nConsumed++;
			continue;
		}

		if (!bDropCrumb && !bJumpCrumb && !bLadderCrumb && uRemaining > 1
			&& vLocalOrigin.DistToSqr(m_vCrumbs[nConsumed + 1].m_vPos) < kReachRadius * kReachRadius
			&& IsVectorVisibleNavigation(vLocalOrigin + Vector(0.f, 0.f, PLAYER_STEP_HEIGHT),
				m_vCrumbs[nConsumed + 1].m_vPos + Vector(0.f, 0.f, PLAYER_STEP_HEIGHT)))
		{
			m_tLastCrumb = m_vCrumbs[nConsumed + 1];
			nConsumed += 2;
			continue;
		}

		if (bDropCrumb)
		{
			const bool bFallen = vLocalOrigin.z <= tActive.m_vPos.z - 18.f;
			const bool bLandedBelow = !tActive.m_vLanding.IsZero() && m_pLocalArea && tActive.m_pNavArea
				&& m_pLocalArea != tActive.m_pNavArea
				&& vLocalOrigin.z <= tActive.m_vLanding.z + 24.f
				&& (pLocal->OnSolid() || vLocalOrigin.z < tActive.m_pNavArea->m_flMinZ - 8.f);
			if (bFallen || bLandedBelow)
			{
				m_tLastCrumb = tActive;
				nConsumed++;
				continue;
			}
			float flPush = 64.f;
			if (!tActive.m_vLanding.IsZero())
			{
				const float flLandingDist = (tActive.m_vLanding - vLocalOrigin).Length2D();
				flPush = std::clamp(flLandingDist + 16.f, 32.f, 64.f);
			}
			vMoveTarget = vLocalOrigin + vMoveDir * flPush;
			vMoveTarget.z = vLocalOrigin.z;
		}

		if (bLadderCrumb)
		{
			if (vLocalOrigin.DistTo2D(tActive.m_vPos) <= kLadderReachRadius)
			{
				vMoveTarget = vLocalOrigin + tActive.m_vApproachDir * 32.f;
				vMoveTarget.z = vLocalOrigin.z;
				vLookTarget = tActive.m_vLanding;
				bLookPitch = true;
			}
			else if (!pLocal->OnSolid())
			{
				vMoveTarget = { tActive.m_vLanding.x, tActive.m_vLanding.y, vLocalOrigin.z };
				vLookTarget = vMoveTarget;
			}
		}

		if (bJumpCrumb && vLocalOrigin.DistTo2D(tActive.m_vPos) <= kJumpReachRadius
			&& !tActive.m_vLanding.IsZero())
		{
			Vector vToLanding = tActive.m_vLanding - vLocalOrigin;
			vToLanding.z = 0.f;
			if (vToLanding.LengthSqr() > 1.f)
			{
				m_vCurrentPathDir = vToLanding.Normalized();
				vMoveTarget = { tActive.m_vLanding.x, tActive.m_vLanding.y, vLocalOrigin.z };
				vLookTarget = vMoveTarget;
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
			m_vStuckDestination = {};
			m_iStuckDestinationCount = 0;
			m_iStuckDestinationExpireTick = 0;
			if (m_iNavJumpTick && !pLocal->OnSolid())
			{
				pCmd->buttons |= IN_DUCK;
				m_bHoldingCrouch = true;
			}
			else if (m_bHoldingCrouch)
			{
				pCmd->buttons &= ~IN_DUCK;
				m_bHoldingCrouch = false;
			}
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

	if (m_tCurrentCrumb.m_bRequiresLadder)
	{
		if (std::fabs(vLocalOrigin.z - m_flLadderLastZ) > 8.f)
		{
			m_flLadderLastZ = vLocalOrigin.z;
			m_tLadderProgressTimer.Update();
		}
		else if (!pLocal->OnSolid() && m_tLadderProgressTimer.Check(2.f))
		{
			AbandonPath("Ladder stuck", true);
			return;
		}
	}
	else
	{
		m_flLadderLastZ = 0.f;
		m_tLadderProgressTimer.Update();
	}

	bool bJumpIssued = false;
	if (ePhase == StuckPhase::Jump && NavRuntime::CanIssueNavJump(pWeapon, pCmd))
	{
		m_iStuckJumps++;
		m_tLastProgressTimer.Update();
		m_tStuckSampleTimer.Update();
		m_flLastDistToCrumb = (vCrumbTarget - vLocalOrigin).Length();
		if (pLocal->OnSolid())
		{
			pCmd->buttons |= IN_JUMP;
			bJumpIssued = true;
		}
	}

	if (ePhase == StuckPhase::Fail && ShouldWaitForDoor(pLocal, vCrumbTarget))
	{
		m_tLastProgressTimer.Update();
		m_bUnstucking = false;
		ePhase = StuckPhase::Idle;
	}

	if (ePhase == StuckPhase::Fail)
	{
		AbandonPath(bDropCrumb ? "Stuck on drop" : "Stuck (no progress)", true);
		return;
	}

	if (m_tCurrentCrumb.m_bRequiresLadder)
		m_bUnstucking = true;

	const int iNow = I::GlobalVars->tickcount;
	if (m_iNavJumpTick && pLocal->OnSolid()
		&& (iNow < m_iNavJumpTick || iNow - m_iNavJumpTick > TIME_TO_TICKS(0.1f)))
		m_iNavJumpTick = 0;

	const bool bCrouchArea = m_tCurrentCrumb.m_bRequiresCrouch
		|| (m_pLocalArea && (m_pLocalArea->m_iAttributeFlags & NAV_MESH_CROUCH));
	const bool bJumpDuck = m_iNavJumpTick && !pLocal->OnSolid();
	if (bCrouchArea || bJumpDuck)
	{
		pCmd->buttons |= IN_DUCK;
		m_bHoldingCrouch = true;
	}
	else if (m_bHoldingCrouch && !m_bUnstucking)
	{
		pCmd->buttons &= ~IN_DUCK;
		m_bHoldingCrouch = false;
	}

	if (m_tCurrentCrumb.m_bRequiresJump && pLocal->OnSolid()
		&& NavRuntime::CanIssueNavJump(pWeapon, pCmd)
		&& vLocalOrigin.DistTo2D(m_tCurrentCrumb.m_vPos) <= kJumpReachRadius)
	{
		if (!m_iJumpStartTick)
			m_iJumpStartTick = iNow;
		else if (iNow - m_iJumpStartTick > TIME_TO_TICKS(0.35f))
		{
			AbandonPath("Jump failed", true);
			return;
		}

		pCmd->buttons |= IN_JUMP;
		bJumpIssued = true;
		m_bUnstucking = true;
	}
	else if (!m_tCurrentCrumb.m_bRequiresJump)
		m_iJumpStartTick = 0;

	if (bJumpIssued)
	{
		m_iNavJumpTick = iNow;
		if (!(bCrouchArea && pLocal->IsDucking()))
			pCmd->buttons &= ~IN_DUCK;
	}

	DoLookAtPath(pLocal, pCmd, vLookTarget, true, bLookPitch);
	SDK::WalkTo(pCmd, pLocal, vMoveTarget);
}
