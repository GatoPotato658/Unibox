#pragma once
#include "NavArea.h"
#include "TFConstants.h"
#include <boost/container_hash/hash.hpp>
#include <atomic>
#include <limits>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

inline constexpr float PLAYER_WIDTH = 48.0f;
inline constexpr float HALF_PLAYER_WIDTH = 24.0f;
inline constexpr float PLAYER_HEIGHT = 82.0f;
inline constexpr float PLAYER_DUCK_HEIGHT = 62.0f;
inline constexpr float PLAYER_STEP_HEIGHT = 18.0f;
inline constexpr float PLAYER_JUMP_HEIGHT = 72.0f;
inline constexpr float PLAYER_CROUCHED_JUMP_HEIGHT = 72.0f;
// TF's CTFBotLocomotion::GetDeathDropHeight() and tf_gamerules.cpp both use 1000; falls below that are survivable.
inline constexpr float PLAYER_DEATH_DROP_HEIGHT = 1000.0f;
// Source nav generation (nav.h HalfHumanWidth) builds areas for a 32-wide hull, so areas may reach 16u from walls; the TF hull is 48 wide.
inline constexpr float NAV_GEN_HALF_WIDTH = 16.0f;
inline constexpr float LADDER_APPROACH_DISTANCE = 32.0f;
inline constexpr float LADDER_DISMOUNT_DISTANCE = 24.0f;

static_assert(HALF_PLAYER_WIDTH == TFGame::HULL_HALF_WIDTH && PLAYER_WIDTH == 2.f * TFGame::HULL_HALF_WIDTH);
static_assert(PLAYER_HEIGHT == TFGame::HULL_HEIGHT && PLAYER_DUCK_HEIGHT == TFGame::HULL_DUCK_HEIGHT);
static_assert(PLAYER_STEP_HEIGHT == TFGame::STEP_HEIGHT);
static_assert(PLAYER_JUMP_HEIGHT <= TFGame::CROUCH_JUMP_REACH + 0.5f && PLAYER_JUMP_HEIGHT >= TFGame::CROUCH_JUMP_REACH - 0.5f);
#define TICKCOUNT_TIMESTAMP(seconds) (I::GlobalVars->tickcount + static_cast<int>((seconds) / I::GlobalVars->interval_per_tick))

class CNavFile
{
public:
	CNavFile() = default;
	explicit CNavFile(const char* szLevelname);
	bool Load(const char* szLevelname);
	bool Write(const char* szFilename = nullptr);
	void QueryOverlapping(const Vector& vPos, float flRadius, std::vector<CNavArea*>& vOut);

	std::vector<NavPlace_t> m_vPlaces;
	std::vector<CNavArea> m_vAreas;
	std::vector<NavLadder_t> m_vLadders;
	std::string m_sMapName;
	unsigned int m_uBspSize = 0;
	std::vector<char> m_vTailData;
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
	bool m_bStartInEnemySpawn = false;
	int m_iHighestCapturedPoint = -1;
};

namespace NavPolicy
{
	void Update();
	void Reset();
	uint32_t GetEpoch();
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
	bool m_bRequiresJump = false;
	float m_flDropHeight = 0.f;
	float m_flJumpHeight = 0.f;
	float m_flApproachDistance = 0.f;
	Vector m_vApproachDir = {};
	Vector m_vLanding = {};
};

struct CachedPathCrumb_t
{
	CNavArea* m_pNavArea = nullptr;
	Vector m_vPos = {};
	Vector m_vApproachDir = {};
	Vector m_vLanding = {};
	bool m_bRequiresDrop = false;
	bool m_bRequiresJump = false;
	bool m_bRequiresCrouch = false;
	bool m_bRequiresLadder = false;
	float m_flDropHeight = 0.f;
	float m_flJumpHeight = 0.f;
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
	bool m_bHasPoints = false;
	float m_flReachSpeed = 0.f;
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
	float m_flTickInterval = 1.f / 66.f;
	bool m_bIgnoreTraces = false;
	bool m_bCanJump = true;
	float m_flRunSpeed = 230.f;
	float m_flDeathDropHeight = PLAYER_DEATH_DROP_HEIGHT;
	float m_flHazardScale = 1.f;
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
	std::unordered_map<CNavArea*, CachedStucktime_t> m_mAreaStuckTime;
	std::unordered_set<std::pair<CNavArea*, CNavArea*>, boost::hash<std::pair<CNavArea*, CNavArea*>>> m_setWalkableBridges;

	explicit CMap(const char* sMapName)
		: m_navfile(sMapName), m_sMapName(sMapName)
	{
		m_eState = m_navfile.m_bOK ? NavStateEnum::Active : NavStateEnum::Unavailable;
	}

	int Solve(CNavArea* pStart, CNavArea* pEnd, const SolveContext& tCtx, std::vector<CNavArea*>& vOutPath, float* pflCost);

	void SolveCostField(CNavArea* pStart, const SolveContext& tCtx, std::vector<float>& vOutCost,
		float flMaxCost = std::numeric_limits<float>::max(), const std::vector<CNavArea*>* pTargets = nullptr);
	size_t GetAreaIndex(const CNavArea* pArea) const { return static_cast<size_t>(pArea - m_navfile.m_vAreas.data()); }

	static SolveContext BuildSolveContext(bool bIgnoreTraces = false);
	int SolveCrumbs(const Vector& vStart, CNavArea* pStartArea, const Vector& vEnd, CNavArea* pEndArea,
		const SolveContext& tCtx, std::vector<CachedPathCrumb_t>& vOutPath, float* pflCost);

	NavPoints_t DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea);
	DropdownHint_t HandleDropdown(const NavPoints_t& tPoints);

	bool HasDirectConnection(CNavArea* pFrom, CNavArea* pTo) const;

	enum class AreaBlock : uint8_t { None, Soft, Stuck };
	AreaBlock GetAreaBlock(CNavArea* pArea, int iTick) const;
	bool HasUsableExit(CNavArea* pArea, int iTick) const;
	void ShortenExitBlocks(CNavArea* pArea, int iExpireTick);
	void ClearDynamicBlocks();

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
		m_mAreaStuckTime.clear();
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
	void GetAdjacent(CNavArea* pCurrentArea, const SolveContext& tCtx, bool bSkipSpawn, std::vector<AdjacentEntry>& vOut);
	float EvaluateConnectionCost(CNavArea* pCurrentArea, CNavArea* pNextArea, const NavPoints_t& tPoints, const DropdownHint_t& tDropdown, int iTeam) const;
};
