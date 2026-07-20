#pragma once
#include "FileReader/CNavFile.h"
#include <boost/container_hash/hash.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <array>
#include <queue>
#include <mutex>

#define PLAYER_WIDTH				49.0f
#define HALF_PLAYER_WIDTH			(PLAYER_WIDTH / 2.0f)
#define PLAYER_HEIGHT				83.0f
#define PLAYER_CROUCHED_JUMP_HEIGHT	72.0f
#define PLAYER_JUMP_HEIGHT			50.0f
#define TICKCOUNT_TIMESTAMP(seconds) (I::GlobalVars->tickcount + static_cast<int>((seconds) / I::GlobalVars->interval_per_tick))

Enum(NavState, Unavailable, Active)
Enum(VischeckState, NotVisible = -1, NotChecked, Visible)

// Compat shim — backing map is never populated; CHazards owns real threat data.
Enum(BlacklistReason, Init = -1,
	Sentry, SentryMedium, SentryLow,
	Sticky,
	EnemyNormal, EnemyDormant, EnemyInvuln,
	BadBuildSpot
)

struct BlacklistReason_t
{
	BlacklistReasonEnum::BlacklistReasonEnum m_eValue = BlacklistReasonEnum::Init;
	int m_iTime = 0;

	BlacklistReason_t() = default;
	explicit BlacklistReason_t(BlacklistReasonEnum::BlacklistReasonEnum eReason) : m_eValue(eReason) {}
	BlacklistReason_t(BlacklistReasonEnum::BlacklistReasonEnum eReason, int iTime) : m_eValue(eReason), m_iTime(iTime) {}

	void operator=(BlacklistReasonEnum::BlacklistReasonEnum const& eReason) { m_eValue = eReason; }
};

struct NavPoints_t
{
	Vector m_vCurrent;
	Vector m_vCenter;
	Vector m_vCenterNext;
	Vector m_vNext;
};

struct DropdownHint_t
{
	Vector m_vAdjustedPos = {};
	bool m_bRequiresDrop = false;
	float m_flDropHeight = 0.f;
	float m_flApproachDistance = 0.f;
	Vector m_vApproachDir = {};
};

struct CachedPathCrumb_t
{
	CNavArea* m_pNavArea = nullptr;
	Vector m_vPos = {};
	Vector m_vApproachDir = {};
	bool m_bRequiresDrop = false;
	float m_flDropHeight = 0.f;
	float m_flApproachDistance = 0.f;
};

struct CachedConnection_t
{
	int m_iExpireTick = 0;
	VischeckStateEnum::VischeckStateEnum m_eVischeckState = VischeckStateEnum::NotChecked;
	float m_flCachedCost = std::numeric_limits<float>::max();
	DropdownHint_t m_tDropdown = {};
	NavPoints_t m_tPoints = {};
	bool m_bPassable = false;
	bool m_bStuckBlacklist = false;
	size_t m_uNavMeshHash = 0;
	std::vector<CachedPathCrumb_t> m_vCrumbs;
};

struct CachedStucktime_t
{
	int m_iExpireTick = 0;
	int m_iTimeStuck = 0;
};

// Filled on the main thread before each Solve so the worker never touches game globals.
// Hazard snapshot: finite cost = soft penalty, +inf = hard block.
struct SolveContext
{
	int m_iTeam = 0;
	int m_iTickcount = 0;
	int m_iVischeckCacheSeconds = 30;
	bool m_bIgnoreTraces = false;
	bool m_bCanJump = true;
	std::unordered_map<CNavArea*, float> m_mHazardCosts;
};

class CMap
{
public:
	CNavFile m_navfile;
	std::string m_sMapName;
	NavStateEnum::NavStateEnum m_eState = NavStateEnum::Unavailable;

	std::recursive_mutex m_mutex;

	std::unordered_map<std::pair<CNavArea*, CNavArea*>, CachedConnection_t, boost::hash<std::pair<CNavArea*, CNavArea*>>> m_mVischeckCache;
	std::unordered_map<std::pair<CNavArea*, CNavArea*>, CachedStucktime_t, boost::hash<std::pair<CNavArea*, CNavArea*>>> m_mConnectionStuckTime;

	std::unordered_map<CNavArea*, BlacklistReason_t> m_mFreeBlacklist;

	bool m_bSkipSpawn = false;

	explicit CMap(const char* sMapName)
		: m_navfile(sMapName), m_sMapName(sMapName)
	{
		m_eState = m_navfile.m_bOK ? NavStateEnum::Active : NavStateEnum::Unavailable;
		if (m_eState == NavStateEnum::Active)
			RefreshCrumbGraph(true);
	}

	// Caller must hold m_mutex — reads/writes m_mVischeckCache + m_mConnectionStuckTime.
	int Solve(CNavArea* pStart, CNavArea* pEnd, const SolveContext& tCtx, std::vector<CNavArea*>& vOutPath, float* pflCost);

	std::vector<CNavArea*> FindPath(CNavArea* pLocalArea, CNavArea* pDestArea, int* pOutResult = nullptr);

	// Must be called on the main thread; touches H::Entities / F::Hazards / F::NavEngine.
	static SolveContext BuildSolveContext();
	bool RefreshCrumbGraph(bool bForce = false);
	int SolveCrumbs(const Vector& vStart, CNavArea* pStartArea, const Vector& vEnd, CNavArea* pEndArea,
		const SolveContext& tCtx, std::vector<CachedPathCrumb_t>& vOutPath, float* pflCost);

	NavPoints_t DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea, bool bIsOneWay);
	DropdownHint_t HandleDropdown(const Vector& vCurrentPos, const Vector& vNextPos, bool bIsOneWay);
	const std::vector<CachedPathCrumb_t>* GetConnectionCrumbs(CNavArea* pFrom, CNavArea* pTo) const;

	bool IsOneWay(CNavArea* pFrom, CNavArea* pTo) const;
	bool HasDirectConnection(CNavArea* pFrom, CNavArea* pTo) const;

	float GetBlacklistPenalty(const BlacklistReason_t& tReason) const;

	void CollectAreasAround(const Vector& vOrigin, float flRadius, std::vector<CNavArea*>& vOutAreas);

	CNavArea* FindClosestNavArea(const Vector& vPos, bool bLocalOrigin);

	bool IsAreaValid(CNavArea* pArea) const
	{
		if (!pArea || m_navfile.m_vAreas.empty()) return false;
		const CNavArea* pBegin = &m_navfile.m_vAreas.front();
		const CNavArea* pEnd = &m_navfile.m_vAreas.back();
		return pArea >= pBegin && pArea <= pEnd;
	}

	void Reset()
	{
		std::lock_guard lock(m_mutex);
		m_mVischeckCache.clear();
		m_mConnectionStuckTime.clear();
	}

private:
	struct CrumbGraphEdge_t
	{
		size_t m_uTo = 0;
		float m_flCost = 0.f;
		bool m_bRequiresDrop = false;
		float m_flDropHeight = 0.f;
		float m_flApproachDistance = 0.f;
		Vector m_vApproachDir = {};
	};

	struct CrumbGraphNode_t
	{
		CNavArea* m_pNavArea = nullptr;
		Vector m_vPos = {};
		std::vector<CrumbGraphEdge_t> m_vEdges;
	};

	struct CrumbPathNode_t
	{
		float m_flG = std::numeric_limits<float>::max();
		float m_flF = std::numeric_limits<float>::max();
		size_t m_uParent = std::numeric_limits<size_t>::max();
		const CrumbGraphEdge_t* m_pIncoming = nullptr;
		uint32_t m_uQueryId = 0;
	};

	std::vector<CrumbGraphNode_t> m_vCrumbGraph;
	std::unordered_map<CNavArea*, std::vector<size_t>> m_mAreaCrumbNodes;
	std::vector<CrumbPathNode_t> m_vCrumbPathNodes;
	std::array<uint8_t, 20> m_aCrumbGraphSha = {};
	uint32_t m_uCrumbQueryId = 0;

	std::array<uint8_t, 20> ComputeNavMeshSha() const;
	void BuildCrumbGraph();
	size_t AddCrumbGraphNode(CNavArea* pArea, const Vector& vPos);
	void AddCrumbGraphEdge(size_t uFrom, size_t uTo, bool bBidirectional, bool bRequiresDrop = false,
		float flDropHeight = 0.f, float flApproachDistance = 0.f, const Vector& vApproachDir = {});
	size_t FindNearestCrumbGraphNode(CNavArea* pArea, const Vector& vPos) const;
	bool IsGraphEdgeUsable(const CrumbGraphNode_t& tFrom, const CrumbGraphEdge_t& tEdge, const SolveContext& tCtx,
		float& flCost) const;
	struct PathNode_t
	{
		float m_g = std::numeric_limits<float>::max();
		float m_f = std::numeric_limits<float>::max();
		CNavArea* m_pParent = nullptr;
		uint32_t m_iQueryId = 0;
		bool m_bInOpen = false;
	};

	std::vector<PathNode_t> m_vPathNodes;
	uint32_t m_iQueryId = 0;

	struct AdjacentEntry { CNavArea* m_pArea; float m_flCost; };
	void GetAdjacent(CNavArea* pCurrentArea, const SolveContext& tCtx, std::vector<AdjacentEntry>& vOut);
	size_t GetConnectionNavMeshHash(CNavArea* pFrom, CNavArea* pTo) const;
	void CacheConnectionCrumbs(CachedConnection_t& tEntry, CNavArea* pFrom, CNavArea* pTo, const NavPoints_t& tPoints, const DropdownHint_t& tDropdown) const;
	float EvaluateConnectionCost(CNavArea* pCurrentArea, CNavArea* pNextArea, const NavPoints_t& tPoints, const DropdownHint_t& tDropdown, int iTeam) const;
};
