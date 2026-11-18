#pragma once
#include "NavHull.h"
#include <array>
#include <deque>

namespace PathWorker { class CPathWorker; }

Enum(PriorityList, None,
	Patrol = 5,
	LowPrioGetHealth,
	StayNear,
	RunReload, RunSafeReload,
	SnipeSentry,
	Capture,
	GetAmmo,
	MeleeAttack,
	Engineer,
	GetHealth,
	EscapeSpawn, EscapeDanger,
	Followbot,
	Forced,
	MVMTank,
	MVMCombat,
	MVMMoney,
	MVMFrontline,
	MVMSniper,
	BuyBot
)

struct Crumb_t
{
	CNavArea* m_pNavArea = nullptr;
	Vector m_vPos = {};
	Vector m_vApproachDir = {};
	Vector m_vLanding = {};
	bool m_bRequiresDrop = false;
	bool m_bRequiresJump = false;
	bool m_bRequiresCrouch = false;
	bool m_bWallChecked = false;
	float m_flDropHeight = 0.f;
	float m_flJumpHeight = 0.f;
	float m_flApproachDistance = 0.f;

	bool IsTransition() const { return m_bRequiresDrop || m_bRequiresJump; }
};

struct RespawnRoom_t
{
	int m_iTeam = 0;
	TriggerData_t tData = {};
};

struct BlockedDoor_t
{
	int m_iEntIndex = 0;
	int m_iStrikes = 0;
	int m_iExpireTick = 0;
	bool m_bActive = false;
	Vector m_vClosedOrigin = {};
	Vector m_vClosedAngles = {};
	std::vector<CNavArea*> m_vAreas;
};

enum class StuckPhase : int { Idle = 0, Jump, Fail };

struct NavStats_t
{
	int m_iSubmitted = 0;
	int m_iHullRejects = 0;
	int m_iAbandons = 0;
	int m_iStuck = 0;
	int m_iShortcuts = 0;
	std::string m_sLastAbandon = "";
	float m_flLastAbandonTime = 0.f;
};

struct HullWalkKey_t
{
	int m_aCoords[6] = {};
	bool m_bCrouch = false;

	bool operator==(const HullWalkKey_t& tOther) const = default;
};

struct HullWalkKeyHash_t
{
	size_t operator()(const HullWalkKey_t& tKey) const
	{
		size_t uSeed = tKey.m_bCrouch;
		for (const int iCoord : tKey.m_aCoords)
			boost::hash_combine(uSeed, iCoord);
		return uSeed;
	}
};

struct HullWalkCacheEntry_t
{
	int m_iExpireTick = 0;
	bool m_bCrouch = false;
	std::vector<Vector> m_vProfile;
};

class CNavEngine
{
private:
	std::unique_ptr<CMap> m_pMap;
	std::deque<Crumb_t> m_vCrumbs;
	std::vector<RespawnRoom_t> m_vRespawnRooms;
	std::vector<CNavArea*> m_vRespawnRoomExitAreas;
	CNavArea* m_pLocalArea = nullptr;
	bool m_bLocalAreaSearchFailed = false;
	CNavArea* m_pStartAreaCache = nullptr;
	Vector m_vStartAreaCacheOrigin = {};
	int m_iStartAreaCacheTick = 0;
	bool m_bStartAreaCacheValid = false;
	std::vector<PathHullSegment_t> m_vHullDebug;
	std::unordered_map<HullWalkKey_t, HullWalkCacheEntry_t, HullWalkKeyHash_t> m_mHullWalkCache;
	NavStats_t m_tStats = {};
	int m_iHullRejects = 0;
	int m_iHullRejectExpireTick = 0;
	bool m_bHullRejected = false;
	uint32_t m_uPolicyEpoch = 0;
	int m_iDoorWaitStartTick = 0;
	int m_iDoorWaitEntIndex = 0;
	Vector m_vDoorWaitLastOrigin = {};
	Vector m_vDoorWaitLastAngles = {};
	std::vector<BlockedDoor_t> m_vBlockedDoors;
	Timer m_tDoorUpdateTimer = {};

	Timer m_tStuckSampleTimer = {};
	Timer m_tLastProgressTimer = {};
	int m_iJumpStartTick = 0;
	Vector m_vLastStuckSamplePos = {};
	Vector m_vLastShortcutCheck = {};
	float m_flLastDistToCrumb = FLT_MAX;
	CNavArea* m_pLastProgressArea = nullptr;
	int m_iStuckJumps = 0;

	Timer m_tOffMeshTimer = {};
	Timer m_tOffMeshRecoverTimer = {};
	Vector m_vOffMeshTarget = {};

	bool m_bRepathRequested = false;
	int m_iNextRepathTick = 0;
	bool m_bRepathOnFail = false;
	bool m_bUnstucking = false;
	int m_iLastBlacklistAbandonTick = 0;
	CNavArea* m_pHazardAbandonArea = nullptr;
	CNavArea* m_pAcceptedHazardArea = nullptr;
	int m_iAcceptedHazardExpireTick = 0;
	bool m_bRepathBeforePending = false;
	bool m_bIgnoreTracesBeforePending = false;
	Vector m_vDestinationBeforePending = {};
	bool m_bRecoveryRetryUsed = false;
	bool m_bBypassFailedDestination = false;
	bool m_bHoldingCrouch = false;
	int m_iNavJumpTick = 0;
	uint64_t m_uRouteSeed = 0;
	CNavArea* m_pVariationArea = nullptr;
	Vector m_vVariationBase = {};
	Vector m_vVariationDir = {};
	Vector m_vVariationTarget = {};
	float m_flVariationApplied = -1.f;
	int m_iVariationRetryTick = 0;
	bool m_bVariationTargetValid = false;

	bool m_bUpdatedRespawnRooms = false;

	Timer m_tSetupCheck = {};
	bool m_bSetupTime = false;
	bool m_bMapHasBlueSetupGate = false;
	bool m_bMapHasRedSetupGate = false;
	Timer m_tNoAreaRetry = {};
	Timer m_tVischeck = {};
	Timer m_tBlacklistCheck = {};
	Timer m_tRestartTimer = {};
	Timer m_tNavRetry = {};
	std::string m_sNavLevel = "";
	std::string m_sNavInstallPath = "";
	bool m_bNavLoadAttempted = false;
	bool m_bWasEnabled = false;

	std::array<Vector, 2> m_aPayloadPos = {};
	std::array<float, 2> m_aPayloadMoveTime = {};
	std::array<bool, 2> m_aSeenPayload = {};

	std::unique_ptr<PathWorker::CPathWorker> m_pPathWorker;
	uint64_t m_uNextRequestId = 1;
	uint64_t m_uPendingRequestId = 0;
	uint64_t m_uWorldGeneration = 1;
	Vector m_vPendingDestination = {};
	PriorityListEnum::PriorityListEnum m_ePendingPriority = PriorityListEnum::None;
	PriorityListEnum::PriorityListEnum m_ePriorityBeforePending = PriorityListEnum::None;
	bool m_bHadActivePathBeforePending = false;
	bool m_bPendingRepathOnFail = false;
	bool m_bPendingIgnoreTraces = false;
	Vector m_vFailedDestination = {};
	int m_iFailedDestinationTick = 0;
	int m_iNoSolutionCount = 0;
	Vector m_vStuckDestination = {};
	int m_iStuckDestinationCount = 0;
	int m_iStuckDestinationExpireTick = 0;

	std::array<float, 10> m_flRecentFallSpeeds = {};
	size_t m_iRecentFallSpeedIndex = 0;
	size_t m_nRecentFallSpeedCount = 0;

	void AbandonPath(const char* sReason, bool bStuck = false);
	void RecordStuckFailure();
	void EnsureLocalExit();
	bool HasDirectRoute(CTFPlayer* pLocal, const Vector& vFrom, const Vector& vTo);
	bool ShouldWaitForDoor(CTFPlayer* pLocal, const Vector& vCrumbTarget);
	void BlockDoor(CBaseEntity* pDoor);
	void UpdateBlockedDoors();
	void ReleaseBlockedDoor(BlockedDoor_t& tDoor);
	void BridgeSpawnIslands(CTFPlayer* pLocal);
	void ResetStuckProgress(const Vector& vLocalOrigin, const Vector& vCrumbTarget);
	void PollPathWorker(CTFPlayer* pLocal);
	bool StoreValidatedCrumbs(const std::vector<CachedPathCrumb_t>& vCrumbs, CTFPlayer* pLocal);
	bool HullCheckCrumbs(CTFPlayer* pLocal, std::deque<Crumb_t>& vCrumbs, float flMaxDistance, size_t& iFailed, bool& bGap, CBaseEntity*& pBlocker);
	bool CheckCrumbLink(CTFPlayer* pLocal, const Crumb_t& a, Crumb_t& b, bool bHullCheck, int iExpire, bool& bTraced);
	bool WalkPathSegment(ITraceFilter* pFilter, const Vector& vFrom, const Vector& vTo, bool& bCrouch, PathHullSegment_t& tSegment, CBaseEntity*& pHit);
	bool BlockHullFailure(const std::deque<Crumb_t>& vCrumbs, size_t iFailed, bool bGap, CBaseEntity* pBlocker);
	bool ConsumeHullReject();
	void KeepCrumbsOffWalls(CTFPlayer* pLocal, std::deque<Crumb_t>& vCrumbs, int iBudget);
	void UpdateRespawnRooms();
	void ClearPathState();
	void RecoverOffMesh(CTFPlayer* pLocal, CNavArea* pArea, const Vector& vLocalOrigin);
	void SampleFallSpeed(float flVerticalVelocity);
	bool RecentlyAtRest() const;
	StuckPhase TickStuckSample(const Vector& vLocalOrigin, const Vector& vCrumbTarget);
	void ClearStuckDestination();
	void ClearFailedDestination();
	void ResetFollowVariation();
	void ApplyFollowVariation(CTFPlayer* pLocal, const Crumb_t& tCrumb, const Crumb_t* pNext, const Vector& vLocalOrigin, Vector& vMoveTarget);
	void PressJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void UpdateLandingCrouch(CTFPlayer* pLocal, CUserCmd* pCmd);
	void FollowWithoutCrumbs(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void DoLookAtPath(CTFPlayer* pLocal, CUserCmd* pCmd, const Vector& vMoveTarget, bool bHaveTarget);
	bool IsPayloadEscortPaceState(CTFPlayer* pLocal, const Vector& vLocalOrigin);

public:
	std::string m_sLastFailureReason = "";
	bool m_bIgnoreTraces = false;

	PriorityListEnum::PriorityListEnum m_eCurrentPriority = PriorityListEnum::None;
	Crumb_t m_tCurrentCrumb = {};
	Crumb_t m_tLastCrumb = {};
	Vector m_vCurrentPathDir = {};
	Vector m_vLastDestination = {};
	Vector m_vLastLookTarget = {};

	CNavEngine();
	~CNavEngine();

	bool IsSetupTime();

	bool IsVectorVisibleNavigation(const Vector vFrom, const Vector vTo, unsigned int nMask = MASK_SHOT);
	bool IsPlayerPassableNavigation(CTFPlayer* pLocal, const Vector vFrom, Vector vTo,
		unsigned int nMask = MASK_PLAYERSOLID, bool* pbNeedsCrouch = nullptr, CBaseEntity** ppBlocker = nullptr);

	bool IsPathing() { return !m_vCrumbs.empty() || m_uPendingRequestId != 0 || m_bRepathRequested; }
	bool IsPriorityAllowed(PriorityListEnum::PriorityListEnum ePriority) const
	{
		return !(ePriority < m_eCurrentPriority || (m_uPendingRequestId != 0 && ePriority < m_ePendingPriority));
	}
	bool IsUnstucking() const { return m_bUnstucking; }
	bool IsControllingJumpInput() const { return m_bUnstucking || m_bHoldingCrouch || m_iNavJumpTick != 0 || (!m_vCrumbs.empty() && m_tCurrentCrumb.IsTransition()); }
	bool IsNavMeshLoaded() const;
	std::string GetNavFilePath() const { return m_pMap ? m_pMap->m_sMapName : ""; }
	std::string GetNavInstallPath();

	void ClearRespawnRooms() { m_vRespawnRooms.clear(); m_vRespawnRoomExitAreas.clear(); m_bUpdatedRespawnRooms = false; }
	void AddRespawnRoom(int iTeam, TriggerData_t tTrigger) { m_vRespawnRooms.emplace_back(iTeam, tTrigger); m_bUpdatedRespawnRooms = false; }
	const std::vector<RespawnRoom_t>& GetRespawnRooms() const { return m_vRespawnRooms; }
	const std::vector<CNavArea*>& GetRespawnRoomExitAreas() const { return m_vRespawnRoomExitAreas; }

	CNavArea* FindClosestNavArea(const Vector vOrigin, bool bLocalOrigin = true) { return m_pMap ? m_pMap->FindClosestNavArea(vOrigin, bLocalOrigin) : nullptr; }
	CNavArea* GetLocalNavArea() const { return m_pLocalArea; }
	CNavArea* GetLocalNavArea(const Vector& vLocalOrigin);
	CNavArea* GetPathStartArea(CTFPlayer* pLocal, const Vector& vLocalOrigin);
	CNavFile* GetNavFile() { return m_pMap ? &m_pMap->m_navfile : nullptr; }
	CMap* GetNavMap() { return m_pMap.get(); }

	const std::deque<Crumb_t>& GetCrumbs() const { return m_vCrumbs; }
	const std::vector<PathHullSegment_t>& GetHullDebug() const { return m_vHullDebug; }
	const NavStats_t& GetStats() const { return m_tStats; }

	bool IsReady();
	bool EnsureMapLoaded(bool bForced = false);
	void CancelPath();
	bool IsPathingFor(PriorityListEnum::PriorityListEnum ePriority) { return m_eCurrentPriority == ePriority && IsPathing(); }
	void CancelPathIf(PriorityListEnum::PriorityListEnum ePriority) { if (m_eCurrentPriority == ePriority) CancelPath(); }

	bool NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority = PriorityListEnum::Forced, bool bShouldRepath = true, bool bIgnoreTraces = false);

	float GetPathCost(CNavArea* pStartArea, CNavArea* pDestinationArea);
	bool GetPathAreas(CNavArea* pStartArea, CNavArea* pDestinationArea, std::vector<CNavArea*>& vOutAreas);
	float GetPathCost(const Vector& vStart, const Vector& vDestination, bool bLocal = true);
	bool GetPathCostField(CNavArea* pStartArea, std::vector<float>& vOutCost, float flMaxCost = FLT_MAX, const std::vector<CNavArea*>* pTargets = nullptr);
	float GetFieldCost(const std::vector<float>& vCost, CNavArea* pArea) const;

	const Vector& GetCurrentPathDir() const { return m_vCurrentPathDir; }

	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset(bool bForced = false);
	void Shutdown();

	void FollowCrumbs(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void VischeckPath();
	void CheckBlacklist(CTFPlayer* pLocal);
};

ADD_FEATURE(CNavEngine, NavEngine);
