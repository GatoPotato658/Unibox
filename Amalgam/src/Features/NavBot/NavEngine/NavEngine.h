#pragma once
#include "Map.h"
#include <memory>

namespace PathWorker { class CPathWorker; struct PathResult; }

// Read by JobSystem and the menu. DO NOT reorder existing entries.
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
	Forced
)

enum class PathSolveResult : int
{
	Success = 0,
	NoPath = 1,
	InvalidEndpoints = 2,
	StartEqualsEnd = 3,
};

struct Crumb_t
{
	CNavArea* m_pNavArea = nullptr;
	Vector m_vPos = {};
	Vector m_vApproachDir = {};
	bool m_bRequiresDrop = false;
	float m_flDropHeight = 0.f;
	float m_flApproachDistance = 0.f;
};

struct RespawnRoom_t
{
	int m_iTeam = 0;
	TriggerData_t tData = {};
};

enum class StuckPhase : int { Idle = 0, Nudge, Jump, Fail };

class CNavEngine
{
private:
	std::unique_ptr<CMap> m_pMap;
	std::vector<Crumb_t> m_vCrumbs;
	std::vector<RespawnRoom_t> m_vRespawnRooms;
	std::vector<CNavArea*> m_vRespawnRoomExitAreas;
	CNavArea* m_pLocalArea = nullptr;

	Timer m_tStuckSampleTimer = {};
	Vector m_vLastStuckSamplePos = {};
	float m_flLastDistToCrumb = FLT_MAX;
	int m_iNoProgressSamples = 0;
	int m_iStuckJumpAttempts = 0;

	Timer m_tOffMeshTimer = {};
	Vector m_vOffMeshTarget = {};

	bool m_bRepathRequested = false;
	int m_iNextRepathTick = 0;
	bool m_bRepathOnFail = false;
	bool m_bCurrentNavToLocal = false;
	int m_iLastBlacklistAbandonTick = 0;

	bool m_bUpdatedRespawnRooms = false;

	std::unique_ptr<PathWorker::CPathWorker> m_pPathWorker;
	uint64_t m_uNextRequestId = 1;
	uint64_t m_uPendingRequestId = 0;
	uint64_t m_uWorldGeneration = 1;
	uint64_t m_uHazardGenerationSeen = 0;
	int m_iLastSubmitTick = 0;

	std::array<float, 10> m_flRecentFallSpeeds = {};
	size_t m_iRecentFallSpeedIndex = 0;
	size_t m_nRecentFallSpeedCount = 0;

	void EmitConnectionCrumbs(CNavArea* pFrom, CNavArea* pTo);
	void EmitIntraAreaCrumbs(const Vector& vStart, const Vector& vDestination, CNavArea* pArea);
	void AbandonPath(const std::string& sReason);
	void PollPathWorker();
	bool BuildCrumbsFromResult(const PathWorker::PathResult& tResult, CTFPlayer* pLocal);
	void UpdateRespawnRooms();
	void ClearPathState();
	void ClearDebugPaths();
	void RecoverOffMesh(CTFPlayer* pLocal, CNavArea* pArea, const Vector& vLocalOrigin);
	void SampleFallSpeed(float flVerticalVelocity);
	bool RecentlyAtRest() const;
	StuckPhase TickStuckSample(const Vector& vLocalOrigin, const Vector& vCrumbTarget);
	void DoLookAtPath(CTFPlayer* pLocal, CUserCmd* pCmd, const Vector& vMoveTarget, bool bHaveTarget);

public:
	std::string m_sLastFailureReason = "";
	bool m_bIgnoreTraces = false;
	std::vector<std::pair<Vector, Vector>> m_vPossiblePaths = {};
	std::vector<std::pair<Vector, Vector>> m_vDebugWalkablePaths = {};
	std::vector<std::pair<Vector, Vector>> m_vRejectedPaths = {};

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
	bool IsPlayerPassableNavigation(CTFPlayer* pLocal, const Vector vFrom, Vector vTo, unsigned int nMask = MASK_PLAYERSOLID);

	bool IsPathing() { return !m_vCrumbs.empty() || m_uPendingRequestId != 0; }
	bool IsNavMeshLoaded() const { return m_pMap && m_pMap->m_eState == NavStateEnum::Active; }
	std::string GetNavFilePath() const { return m_pMap ? m_pMap->m_sMapName : ""; }
	bool HasRespawnRooms() const { return !m_vRespawnRooms.empty(); }

	void ClearRespawnRooms() { m_vRespawnRooms.clear(); m_vRespawnRoomExitAreas.clear(); m_bUpdatedRespawnRooms = false; }
	void AddRespawnRoom(int iTeam, TriggerData_t tTrigger) { m_vRespawnRooms.emplace_back(iTeam, tTrigger); }
	const std::vector<RespawnRoom_t>& GetRespawnRooms() const { return m_vRespawnRooms; }
	std::vector<CNavArea*>* GetRespawnRoomExitAreas() { return &m_vRespawnRoomExitAreas; }

	CNavArea* FindClosestNavArea(const Vector vOrigin, bool bLocalOrigin = true) { return m_pMap ? m_pMap->FindClosestNavArea(vOrigin, bLocalOrigin) : nullptr; }
	CNavArea* GetLocalNavArea() const { return m_pLocalArea; }
	CNavArea* GetLocalNavArea(const Vector& vLocalOrigin);
	CNavFile* GetNavFile() { return &m_pMap->m_navfile; }
	CMap* GetNavMap() { return m_pMap.get(); }

	std::vector<Crumb_t>* GetCrumbs() { return &m_vCrumbs; }

	// Compat shim — backing map is never populated; queries always report "not blacklisted".
	std::unordered_map<CNavArea*, BlacklistReason_t>* GetFreeBlacklist() { return &m_pMap->m_mFreeBlacklist; }
	std::unordered_map<CNavArea*, BlacklistReason_t> GetFreeBlacklist(BlacklistReason_t tReason)
	{
		std::unordered_map<CNavArea*, BlacklistReason_t> mReturnMap;
		for (auto& [pNav, tBlacklist] : m_pMap->m_mFreeBlacklist)
			if (tBlacklist.m_eValue == tReason.m_eValue)
				mReturnMap[pNav] = tBlacklist;
		return mReturnMap;
	}
	void ClearFreeBlacklist() const { m_pMap->m_mFreeBlacklist.clear(); }
	void ClearFreeBlacklist(BlacklistReason_t tReason)
	{
		std::erase_if(m_pMap->m_mFreeBlacklist, [&tReason](const auto& entry)
			{ return entry.second.m_eValue == tReason.m_eValue; });
	}

	bool IsReady(bool bRoundCheck = false);
	bool IsBlacklistIrrelevant();
	void CancelPath();

	bool NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority = PriorityListEnum::Forced, bool bShouldRepath = true, bool bNavToLocal = true, bool bIgnoreTraces = false);

	float GetPathCost(CNavArea* pStartArea, CNavArea* pDestinationArea);
	float GetPathCost(const Vector& vStart, const Vector& vDestination, bool bLocal = true);

	const Vector& GetCurrentPathDir() const { return m_vCurrentPathDir; }

	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset(bool bForced = false);
	void shutdown();
	void Render();

	void FollowCrumbs(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void VischeckPath();
	void CheckBlacklist(CTFPlayer* pLocal);
};

ADD_FEATURE(CNavEngine, NavEngine);
