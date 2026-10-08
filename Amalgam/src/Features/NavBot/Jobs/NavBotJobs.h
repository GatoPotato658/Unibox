#pragma once

#include "../BotUtils.h"
#include "../NavEngine.h"
#include "../Hazards.h"
#include <optional>

struct NavBotJobResult_t
{
	bool m_bHasJob = false;
	bool m_bRunReload = false;
	bool m_bRunSafeReload = false;
};

struct NavAreaScore_t
{
	CNavArea* m_pArea = nullptr;
	float m_flScore = 0.f;
};

Enum(GetSupply,
	Health = 1 << 0,
	Ammo = 1 << 1,
	Forced = 1 << 2,
	LowPrio = 1 << 3
);

struct SupplyData_t
{
	bool m_bDispenser = false;
	float m_flRespawnTime = 0.f;
	Vector m_vOrigin = {};

	int m_iCacheIndex = -1;
	bool m_bHealthCache = false;
};

Enum(EngineerTaskStage, None,
	BuildSentry, BuildDispenser,
	SmackSentry, SmackDispenser
)

struct BuildingSpot_t
{
	float m_flCost = FLT_MAX;
	Vector m_vPos = {};
};

struct FailedSpot_t
{
	Vector m_vPos = {};
	float m_flExpire = 0.f;
};

struct FocusPoint_t
{
	bool m_bDefensive = false;
	bool m_bBack = false;
	float m_flTime = FLT_MAX;
	Vector m_vPos = {};
	CNavArea* m_pArea = nullptr;
};

namespace NavJobUtils
{
	auto TryNavToAreaScores(std::vector<NavAreaScore_t>& vAreaScores, PriorityListEnum::PriorityListEnum ePriority, bool bLowestScoreFirst = true, size_t nMaxAttempts = 0) -> bool;

	inline bool IsSpawnArea(const CNavArea* pArea)
	{
		return pArea && (pArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE));
	}

	inline bool IsShortRangeClass(CTFPlayer* pLocal)
	{
		return pLocal->m_iClass() == TF_CLASS_SCOUT || pLocal->m_iClass() == TF_CLASS_PYRO;
	}

	inline bool IsBeingHealed(CTFPlayer* pLocal)
	{
		return pLocal->InCond(TF_COND_HEALTH_BUFF);
	}

	inline bool HasPreference(int iFlag)
	{
		return (Vars::Misc::Movement::NavBot::Preferences.Value & iFlag) != 0;
	}

	inline bool HasBlacklist(int iFlag)
	{
		return (Vars::Misc::Movement::NavBot::Blacklist.Value & iFlag) != 0;
	}

	inline Vector NormalizePlanar(Vector vDirection)
	{
		vDirection.z = 0.f;
		const float flLength = vDirection.Length();
		return flLength > 0.01f ? vDirection / flLength : Vector();
	}
}

namespace NavJobTuning
{
	inline constexpr float HEALTH_START = 0.64f;
	inline constexpr float HEALTH_START_LOW_PRIO = 0.80f;
	inline constexpr float HEALTH_RESUME = 0.90f;
	inline constexpr float HEALTH_RESUME_LOW_PRIO = 0.92f;
}

namespace NavAreaUtils
{
	bool FindClosestHidingSpot(CNavArea* pArea, const Vector& vVischeckPoint, int iMaxDepth, std::pair<CNavArea*, int>& tOut, bool bVischeck = true, int iStartDepth = 0);
}

class CNavBotJobSystem
{
public:
	void RefreshSharedState(CTFPlayer* pLocal);
	auto Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> NavBotJobResult_t;
	void Reset();

private:
	bool m_bDangerLatch = false;
	Timer m_tDangerCommit{};

	auto GetEscapeDangerScore(CTFPlayer* pLocal) -> float;
};

struct CapturePlan_t
{
	bool m_bGotTarget = false;
	bool m_bOverwrite = false;
	bool m_bWalkTo = false;
	Vector m_vTarget = {};
	std::wstring m_sStatus = L"";
};

class CNavBotCapture
{
private:
	Timer m_tCaptureClaimRefresh{};
	std::optional<int> m_iCurrentCapturePointIdx;
	std::optional<Vector> m_vCurrentCaptureSpot;
	std::optional<Vector> m_vCurrentCaptureCenter;
	std::optional<Vector> m_vLastClaimedCaptureSpot;

	Timer m_tCaptureTimer{};
	Timer m_tPlanRefresh{};
	CapturePlan_t m_tCachedPlan{};
	Vector m_vPreviousTarget = {};

public:
	bool m_bOverwriteCapture = false;
	bool m_bWalkTo = false;

	std::wstring m_sCaptureStatus = L"";

private:
	bool IsThreatPlayer(int iIndex);
	void ClaimCaptureSpot(const Vector& vSpot, int iPointIdx);
	void ReleaseCaptureSpotClaim();
	bool FindCoverNearPoint(const Vector& vPoint, float flRadius, Vector& vOut) const;
	bool GetObjectiveGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut);
	bool GetCachedGoal(CTFPlayer* pLocal, bool (CNavBotCapture::*pfnGoal)(CTFPlayer*, Vector&), Vector& vOut);
	bool GetStagedPointGoal(CTFPlayer* pLocal, int iOurTeam, Vector& vOut);
	bool GetPlayerDestructionGoal(CTFPlayer* pLocal, Vector& vOut);
	bool GetRobotDestructionGoal(CTFPlayer* pLocal, Vector& vOut);
	bool GetZombieInfectionGoal(Vector& vOut);
	void LookAtObjective(CUserCmd* pCmd, CTFPlayer* pLocal, const Vec3& vTarget, bool bTargetValid);
	void HoldObjective(CUserCmd* pCmd, CTFPlayer* pLocal, const Vector& vTarget);

public:
	static bool CanCaptureObjective();
	bool GetPayloadGoal(const CHandle<CTFPlayer> hLocal, const Vector vLocalOrigin, int iOurTeam, Vector& vOut);
	bool GetTugOfWarGoal(CTFPlayer* pLocal, int iOurTeam, Vector& vOut);
	bool GetControlPointGoal(const Vector vLocalOrigin, int iOurTeam, Vector& vOut);
	bool GetCtfGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut);
	bool GetPasstimeGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut);
	bool GetDoomsdayGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut);
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void Reset();
};

class CNavBotEngineer
{
private:
	int m_iBuildAttempts = 0;
	float m_flBuildYaw = 0.0f;
	std::vector<BuildingSpot_t>  m_vBuildingSpots;
	FocusPoint_t m_tCurrentFocusPoint = {};
	std::vector<FailedSpot_t> m_vFailedSpots;
private:
	bool IsBuildSpotFailed(const Vector& vPos) const;
	void MarkSpotFailed(const Vector& vPos);
	bool NavToBuildingSpot();
	bool BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bDispenser);
	bool SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding);

	bool GetFocusPoint(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bDefensive, FocusPoint_t& tOut);
public:
	bool IsEngieMode(CTFPlayer* pLocal);
	bool BuildingNeedsToBeSmacked(CBaseObject* pBuilding);
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy);

	void RefreshBuildingSpots(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bForce = false);
	void RefreshLocalBuildings(CTFPlayer* pLocal);
	void Reset();
	void Render();

	BuildingSpot_t m_tCurrentBuildingSpot = {};
	CObjectSentrygun* m_pMySentryGun = nullptr;
	CObjectDispenser* m_pMyDispenser = nullptr;
	float m_flDistToSentry = FLT_MAX;
	float m_flDistToDispenser = FLT_MAX;

	EngineerTaskStageEnum::EngineerTaskStageEnum m_eTaskStage = EngineerTaskStageEnum::None;
};

class CNavBotDanger
{
private:
	CNavArea* m_pEscapeTargetArea = nullptr;
	Timer m_tEscapeRefresh{};
	CNavArea* m_pProjectileTargetArea = nullptr;
	int m_iSpawnExitAttempt = 0;
public:
	std::wstring m_sDangerStatus = {};
public:
	static bool GetProjectileThreatRange(CBaseEntity* pEntity, int iLocalTeam, float& flOutRange);
	bool EscapeDanger(CTFPlayer* pLocal);
	const Hazard_t* GetHazardAhead(CTFPlayer* pLocal) const;
	bool EscapeProjectiles(CTFPlayer* pLocal);
	bool EscapeSpawn(CTFPlayer* pLocal);
	void ResetSpawn();
};

class CNavBotSupplies
{
private:
	std::vector<SupplyData_t> m_vCachedHealthOrigins;
	std::vector<SupplyData_t> m_vCachedAmmoOrigins;
	std::vector<SupplyData_t> m_vTempDispensers;
	std::vector<SupplyData_t> m_vTempMain;

	bool m_bWasForce = false;
	bool m_bHasRememberedDispenser = false;
	Vector m_vRememberedDispenser = {};
	Timer m_tRememberedDispenser{};
	Timer m_tStickyLock{};
	Timer m_tCooldown{};
	Timer m_tRepathCooldown{};

	bool GetSuppliesData(CTFPlayer* pLocal, bool& bClosestTaken, bool bAmmo);
	bool GetDispensersData(CTFPlayer* pLocal);

	bool ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio = false);
	bool ShouldSearchAmmo(CTFPlayer* pLocal);
	bool GetSupply(CUserCmd* pCmd, CTFPlayer* pLocal, Vector vLocalOrigin, SupplyData_t* pSupplyData, PriorityListEnum::PriorityListEnum ePriority);

	void UpdateTakenState();
public:
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iFlags);
	float GetAmmoNeed(bool bActive) const;

	void AddCachedSupplyOrigin(Vector vOrigin, bool bHealth);
	void ResetCachedOrigins();
	void Reset();
};

class CNavBotGroup
{
private:
	bool GetFormationOffset(CTFPlayer* pLeader, int iPositionIndex, Vector& vOut);

	int m_iPositionInFormation = -1;
	float m_flFormationDistance = 120.0f;
	int m_iConsecutiveFailures = 0;
	Vector m_vLastTargetPos = {};
	Timer m_tUpdateFormationTimer{};
	Timer m_tFormationNavTimer{};
	std::vector<uint32_t> m_vLocalBotUserIds;
public:
	void UpdateLocalBots(CTFPlayer* pLocal);
	bool Run(CTFPlayer* pLocal);
	void Reset();
};

class CNavBotMelee
{
private:
	int m_iVisibilityTarget = -1;
	bool m_bTargetVisible = false;
	Timer m_tVisibility{};
public:
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, const ClosestEnemy_t& tClosestEnemy);
	void Reset();
};

class CNavBotReload
{
public:
	bool Run();
	bool RunSafe();
	int GetReloadWeaponSlot(CTFPlayer* pLocal, const ClosestEnemy_t& tClosestEnemy);
	bool HasTask() const { return G::Reloading || (m_iLastReloadSlot >= SLOT_PRIMARY && m_iLastReloadSlot <= SLOT_SECONDARY); }

	int m_iLastReloadSlot = -1;
};

class CNavBotRoam
{
private:
	std::vector<CNavArea*> m_vVisitedAreas;
	std::unordered_set<CNavArea*> m_sConnectedAreas;

	CNavArea* m_pCurrentTargetArea = nullptr;
	CNavArea* m_pDefendSpotArea = nullptr;
	CNavArea* m_pLastConnectedSeed = nullptr;
	const void* m_pLastMap = nullptr;
	const void* m_pLastAreaData = nullptr;
	size_t m_nLastAreaCount = 0;

	Timer m_tRoamTimer{};
	Timer m_tVisitedAreasClear{};
	Timer m_tConnectedAreasRefresh{};

	int m_iConsecutiveFails = 0;

	bool GetDefendTarget(CTFPlayer* pLocal, Vector& vOut);
	bool RunDefend(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vector& vTarget);
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void Reset();
	bool m_bDefending = false;
};

class CNavBotSnipe
{
private:
	bool IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bShortRangeClass);
	bool TryToSnipe(int iEntIdx, bool bShortRangeClass);
public:
	bool Run(CTFPlayer* pLocal);

	int m_iTargetIdx = -1;
};

class CNavBotStayNear
{
private:
	bool StayNearTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex);
	bool IsAreaValidForStayNear(Vector vEntOrigin, CNavArea* pArea, bool bFixLocalZ = true);
	bool IsStayNearTargetValid(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex);
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void Reset();

	int m_iStayNearTargetIdx = -1;
	std::wstring m_sFollowTargetName = {};
};

class CNavBotMVMSniper
{
private:
	enum class EState
	{
		ToEntrance,
		CampExit,
		CampDispenser
	};

	CBaseObject* FindClosestTeleporter(CTFPlayer* pLocal, int iObjectMode);
	CBaseObject* FindClosestDispenser(CTFPlayer* pLocal);
	bool CampAt(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseEntity* pAnchor);

	EState m_eState = EState::ToEntrance;
	int m_iEntranceIdx = -1;
	int m_iCampIdx = -1;
	float m_flLastEntranceDist = FLT_MAX;
	float m_flOnEntranceSince = 0.f;
	float m_flScanClock = 0.f;
	float m_flPairClock = 0.f;

public:
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal);
	void Reset();
};

ADD_FEATURE(CNavBotCapture, NavBotCapture);
ADD_FEATURE(CNavBotEngineer, NavBotEngineer);
ADD_FEATURE(CNavBotDanger, NavBotDanger);
ADD_FEATURE(CNavBotSupplies, NavBotSupplies);
ADD_FEATURE(CNavBotGroup, NavBotGroup);
ADD_FEATURE(CNavBotMelee, NavBotMelee);
ADD_FEATURE(CNavBotReload, NavBotReload);
ADD_FEATURE(CNavBotRoam, NavBotRoam);
ADD_FEATURE(CNavBotSnipe, NavBotSnipe);
ADD_FEATURE(CNavBotStayNear, NavBotStayNear);
ADD_FEATURE(CNavBotMVMSniper, NavBotMVMSniper);
