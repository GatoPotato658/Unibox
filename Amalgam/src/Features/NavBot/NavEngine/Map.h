#pragma once
#include "FileReader/CNavFile.h"
#include <boost/container_hash/hash.hpp>
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
