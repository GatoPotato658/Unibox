#pragma once
// Do not include ../NavBotCore.h — that header includes this one (cycle).
#include "../BotUtils.h"
#include "../NavEngine/NavEngine.h"
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

	SupplyData_t* m_pOriginalSelfPtr = nullptr;
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

struct FocusPoint_t
{
	bool m_bDefensive = false;
	bool m_bBack = false;
	float flTime = FLT_MAX;
	Vector m_vPos = {};
	CNavArea* m_pArea = nullptr;
};

namespace NavJobUtils
{
	auto FindClosestTargetEnemy(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> ClosestEnemy_t;
	auto TryNavToAreaScores(std::vector<NavAreaScore_t>& vAreaScores, PriorityListEnum::PriorityListEnum ePriority, bool bLowestScoreFirst = true, size_t nMaxAttempts = 0) -> bool;
}

class CNavBotJobSystem
{
public:
	void RefreshSharedState(CTFPlayer* pLocal);
	auto Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> NavBotJobResult_t;
	void Reset();

private:
	auto TryEscapeSpawn(CTFPlayer* pLocal) -> bool;
	auto TryEscapeProjectiles(CTFPlayer* pLocal) -> bool;
	auto TryEscapeDanger(CTFPlayer* pLocal) -> bool;
	auto TryGetHealth(CUserCmd* pCmd, CTFPlayer* pLocal, bool bLowPrio) -> bool;
	auto TryGetAmmo(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool;
	auto TryEngineer(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool;
	auto TryRunReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TrySafeReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TryMelee(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool;
	auto TryCapture(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TrySnipeSentry(CTFPlayer* pLocal) -> bool;
	auto TryStayNear(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TryGroupWithOthers(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
	auto TryRoam(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool;
};

class CNavBotCapture
{
private:
	Timer m_tCaptureClaimRefresh{};
	std::optional<int> m_iCurrentCapturePointIdx;
	std::optional<Vector> m_vCurrentCaptureSpot;
	std::optional<Vector> m_vCurrentCaptureCenter;
	std::optional<Vector> m_vLastClaimedCaptureSpot;

public:
	bool m_bOverwriteCapture = false;
	bool m_bWalkTo = false;

	std::wstring m_sCaptureStatus = L"";

private:
	bool ShouldAvoidPlayer(int iIndex);
	void ClaimCaptureSpot(const Vector& vSpot, int iPointIdx);
	void ReleaseCaptureSpotClaim();

public:
	bool GetPayloadGoal(const CHandle<CTFPlayer> hLocal, const Vector vLocalOrigin, int iOurTeam, Vector& vOut);
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
	std::vector<Vector> m_vFailedSpots;
private:
	bool BuildingNeedsToBeSmacked(CBaseObject* pBuilding);
	bool BlacklistedFromBuilding(CNavArea* pArea);
	bool NavToSentrySpot(Vector vLocalOrigin);
	bool BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bDispenser);
	bool SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding);

	bool GetFocusPoint(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bDefensive, FocusPoint_t& tOut);
public:
	bool IsEngieMode(CTFPlayer* pLocal);
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy);

	void RefreshBuildingSpots(CTFPlayer* pLocal, ClosestEnemy_t& tClosestEnemy, bool bForce = false);
	void RefreshLocalBuildings(CTFPlayer* pLocal);
	void Reset();
	void Render();

	BuildingSpot_t m_tCurrentBuildingSpot = {};
	CObjectSentrygun* m_pMySentryGun;
	CObjectDispenser* m_pMyDispenser;
	float m_flDistToSentry = FLT_MAX;
	float m_flDistToDispenser = FLT_MAX;

	EngineerTaskStageEnum::EngineerTaskStageEnum m_eTaskStage = EngineerTaskStageEnum::None;
};

class CNavBotDanger
{
private:
	CNavArea* m_pSpawnExitArea = nullptr;
	CNavArea* m_pEscapeTargetArea = nullptr;
	Timer m_tEscapeRefresh{};
	CNavArea* m_pProjectileTargetArea = nullptr;
public:
	bool EscapeDanger(CTFPlayer* pLocal);
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

	bool GetSuppliesData(CTFPlayer* pLocal, bool& bClosestTaken, bool bHealth = false);
	bool GetDispensersData(CTFPlayer* pLocal);

	bool ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio = false);
	bool ShouldSearchAmmo(CTFPlayer* pLocal);
	bool GetSupply(CUserCmd* pCmd, CTFPlayer* pLocal, Vector vLocalOrigin, SupplyData_t* pSupplyData, const int iPriority);

	void UpdateTakenState();
public:
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iFlags);

	void AddCachedSupplyOrigin(Vector vOrigin, bool bHealth);
	void ResetCachedOrigins();
	void ResetTemp();
};

class CNavBotGroup
{
private:
	bool GetFormationOffset(CTFPlayer* pLocal, int iPositionIndex, Vector& vOut);

	int m_iPositionInFormation = -1;
	float m_flFormationDistance = 120.0f;
	Timer m_tUpdateFormationTimer;
	Timer m_tFormationNavTimer{};
	std::vector<std::pair<uint32_t, Vector>> m_vLocalBotPositions;
public:
	void UpdateLocalBotPositions(CTFPlayer* pLocal);
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
};

class CNavBotMelee
{
public:
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, ClosestEnemy_t tClosestEnemy);
};

class CNavBotReload
{
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool RunSafe(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	int GetReloadWeaponSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy);

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
	void* m_pLastMap = nullptr;

	int m_iConsecutiveFails = 0;
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void Reset();
	bool m_bDefending = false;
};

class CNavBotSnipe
{
private:
	bool IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bShortRangeClass, bool bFixSentryZ = true);
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
	int IsStayNearTargetValid(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex);
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

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
