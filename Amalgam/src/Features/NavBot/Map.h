#pragma once
#include "NavArea.h"
#include <boost/container_hash/hash.hpp>
#include <atomic>
#include <limits>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

inline constexpr float PLAYER_WIDTH = 48.0f;
inline constexpr float HALF_PLAYER_WIDTH = 24.0f;
inline constexpr float PLAYER_HEIGHT = 82.0f;
inline constexpr float PLAYER_DUCK_HEIGHT = 62.0f;
inline constexpr float PLAYER_STEP_HEIGHT = 18.0f;
inline constexpr float PLAYER_JUMP_HEIGHT = 72.0f;
inline constexpr float PLAYER_CROUCHED_JUMP_HEIGHT = 72.0f;
inline constexpr float PLAYER_DEATH_DROP_HEIGHT = 1000.0f;
#define TICKCOUNT_TIMESTAMP(seconds) (I::GlobalVars->tickcount + static_cast<int>((seconds) / I::GlobalVars->interval_per_tick))

class CNavFile
{
public:
	CNavFile() = default;
	explicit CNavFile(const char* szLevelname);
	bool Load(const char* szLevelname);
	bool Write(const char* szFilename = nullptr);
	void QueryOverlapping(const Vector& vPos, float flRadius, std::vector<CNavArea*>& vOut) const;

	std::vector<NavPlace_t> m_vPlaces;
	std::vector<CNavArea> m_vAreas;
	std::string m_sMapName;
	unsigned int m_uBspSize = 0;
	bool m_bHasUnnamedAreas{};
	bool m_bAnalyzed{};
	bool m_bOK = false;
private:
	void BuildSpatialIndex();
	void Clear();
	float m_flGridMinX = 0.f;
	float m_flGridMinY = 0.f;
	float m_flGridCell = 256.f;
	int m_nGridW = 0;
	int m_nGridH = 0;
	std::vector<std::vector<CNavArea*>> m_vGrid;
};

struct NavPolicyState
{
	int m_iTeam = 0;
	bool m_bInSetup = false;
	bool m_bIgnoreSetupGates = false;
	bool m_bRoundWon = false;
	int m_iHighestCapturedPoint = -1;
};

namespace NavPolicy
{
	void Update();
	void Reset();
	NavPolicyState Snapshot(int iTeam);
	bool IsAreaTraversable(const CNavArea& tArea, const NavPolicyState& tState);
	bool IsEnemySpawn(const CNavArea& tArea, int iTeam);
}

Enum(NavState, Unavailable, Active)
Enum(VischeckState, NotVisible = -1, NotChecked, Visible)

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
};

struct CachedStucktime_t
{
	int m_iExpireTick = 0;
	int m_iTimeStuck = 0;
};

struct SolveContext
{
	int m_iTeam = 0;
	int m_iTickcount = 0;
	int m_iVischeckCacheSeconds = 30;
	bool m_bIgnoreTraces = false;
	bool m_bCanJump = true;
	NavPolicyState m_tPolicy{};
	const std::atomic_bool* m_pCancel = nullptr;
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

	bool m_bSkipSpawn = false;

	explicit CMap(const char* sMapName)
		: m_navfile(sMapName), m_sMapName(sMapName)
	{
		m_eState = m_navfile.m_bOK ? NavStateEnum::Active : NavStateEnum::Unavailable;
	}

	int Solve(CNavArea* pStart, CNavArea* pEnd, const SolveContext& tCtx, std::vector<CNavArea*>& vOutPath, float* pflCost);

	static SolveContext BuildSolveContext();
	int SolveCrumbs(const Vector& vStart, CNavArea* pStartArea, const Vector& vEnd, CNavArea* pEndArea,
		const SolveContext& tCtx, std::vector<CachedPathCrumb_t>& vOutPath, float* pflCost);

	NavPoints_t DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea);
	DropdownHint_t HandleDropdown(const NavPoints_t& tPoints);

	bool HasDirectConnection(CNavArea* pFrom, CNavArea* pTo) const;

	void CollectAreasAround(const Vector& vOrigin, float flRadius, std::vector<CNavArea*>& vOutAreas);

	static bool CanFallToNavArea(const Vector& vPos, const CNavArea& tArea);
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
	struct PathNode_t
	{
		float m_g = std::numeric_limits<float>::max();
		float m_f = std::numeric_limits<float>::max();
		CNavArea* m_pParent = nullptr;
		uint32_t m_iQueryId = 0;
	};

	std::vector<PathNode_t> m_vPathNodes;
	uint32_t m_iQueryId = 0;

	struct AdjacentEntry { CNavArea* m_pArea; float m_flCost; };
	void GetAdjacent(CNavArea* pCurrentArea, const SolveContext& tCtx, std::vector<AdjacentEntry>& vOut);
	size_t GetConnectionNavMeshHash(CNavArea* pFrom, CNavArea* pTo) const;
	float EvaluateConnectionCost(CNavArea* pCurrentArea, CNavArea* pNextArea, const NavPoints_t& tPoints, const DropdownHint_t& tDropdown, int iTeam) const;
};
