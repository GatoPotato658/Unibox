#pragma once
#include "../../SDK/SDK.h"
#include "../../Utils/Timer/Timer.h"

#define MAX_CONTROL_POINTS 8
#define MAX_PREVIOUS_POINTS 3
#define PUSH_POINT_CAP_TIME 1000.f

class CGameObjectiveController
{
	float m_flNextRefresh = 0.f;
	void RefreshGameType();

public:
	ETFGameType m_eGameMode = TF_GAMETYPE_UNDEFINED;
	bool m_bDoomsday = false;
	bool m_bHaarp = false;
	bool m_bPayloadHybrid = false;
	bool m_bVSH = false;
	bool m_bTugOfWar = false;
	bool m_bZombieInfection = false;

	void Update();
	void Reset();
	bool GetBattleAnchor(int iTeam, Vector& vOut) const;
};

struct FlagInfo
{
	CCaptureFlag* m_pFlag = nullptr;
	int m_iTeam = TEAM_UNASSIGNED;
	FlagInfo() = default;
	FlagInfo(CCaptureFlag* pFlag, int iTeam)
	{
		m_pFlag = pFlag;
		m_iTeam = iTeam;
	}
};

class CFlagController
{
	std::vector<FlagInfo> m_vFlags;
	std::unordered_map<int, Vector> m_mSpawnPositions;
public:
	FlagInfo GetFlag(int team);
	Vector GetPosition(CCaptureFlag* pFlag);
	bool GetPosition(int iTeam, Vector& vOut);
	bool GetSpawnPosition(int iTeam, Vector& vOut);
	int GetCarrier(CCaptureFlag* pFlag);
	int GetCarrier(int iTeam);
	int GetStatus(CCaptureFlag* pFlag);
	int GetStatus(int iTeam);
	void Init();
	void Update();
};

struct CaptureZone_t
{
	CCaptureZone* m_pZone = nullptr;
	int m_iTeam = TEAM_UNASSIGNED;
	Vector m_vMins = {};
	Vector m_vMaxs = {};
	Vector m_vCenter = {};

	bool Contains(const Vector& vPos, float flPad = 0.f) const;
};

namespace ObjectiveUtils
{
	Vector GetObjectiveOrigin(CBaseEntity* pEntity);
	Vector AdjustPosToNav(Vector vPos);
	bool GetTeamSpawnCenter(int iTeam, Vector& vOut);
	void CollectCaptureZones(std::vector<CaptureZone_t>& vOut);
	bool FindZoneStandPos(const CaptureZone_t& tZone, const Vector& vFrom, Vector& vOut);
	std::vector<float> GetPathCosts(const std::vector<Vector>& vPositions);
	int PickNearest(const std::vector<Vector>& vPositions, const Vector& vFrom, bool bPathCosts, float flMaxPathCost = FLT_MAX);
}

struct CPInfo
{
	int m_iIdx = -1;
	Vector m_vPos = {};
	bool m_bGotPos = false;
	std::array<bool, 2> m_bCanCap = { false, false };
	std::array<bool, 2> m_bCanCapSoon = { false, false };
	bool m_bPushPoint = false;
	CPInfo() = default;
};

class CCPController
{
	std::array<CPInfo, MAX_CONTROL_POINTS> m_aControlPointData;
	CBaseTeamObjectiveResource* m_pObjectiveResource = nullptr;
	Timer m_tCapStatusRefresh = {};
	int m_iIgnoredPoint = -1;
	void UpdateObjectiveResource();
	void UpdateControlPoints();

	bool TeamCanCapPoint(int iIndex, int iTeam);
	int GetPreviousPointForPoint(int iIndex, int iTeam, int iPrevIdx);
	int GetFarthestOwnedControlPoint(int iTeam);
	bool EvaluatePoint(int iIndex, int iTeam, bool bPending);
public:
	bool IsPointUseable(int iIndex, int iTeam);
	bool IsCapturing(int iIndex, int iTeam) const;
	bool GetClosestControlPoint(Vector vPos, int iTeam, Vector& vOut);
	bool GetClosestControlPointInfo(Vector vPos, int iTeam, std::pair<int, Vector>& tOut, bool bIncludePending = false);
	bool HasControlPoints() const;
	void Init();
	void Update();
};

class CPLController
{
	std::array<std::vector<CObjectCartDispenser*>, 2> m_aPayloads = {};
	int m_iTugPoint = -1;
public:
	CObjectCartDispenser* GetClosestPayload(Vector vPos, int iTeam);
	bool HasPayloads() const { return !m_aPayloads[0].empty() || !m_aPayloads[1].empty(); }
	int GetTugOwner() const;
	int GetTugPushers(int iTeam) const;
	void Init();
	void Update();
};

struct PasstimeGoalInfo
{
	CFuncPasstimeGoal* m_pGoal = nullptr;
	int m_iGoalType = CFuncPasstimeGoal::TYPE_HOOP;
	int m_iTeam = TEAM_UNASSIGNED;
	Vector m_vOrigin = {};
	Vector m_vMins = {};
	Vector m_vMaxs = {};
};

class CPasstimeController
{
	std::vector<CFuncPasstimeGoal*> m_vGoals = {};
	CPasstimeBall* m_pBall = nullptr;
	CTFPasstimeLogic* m_pLogic = nullptr;
	int GetGoalTeam(CFuncPasstimeGoal* pGoal) const;
	Vector GetThrowTargetPos(const PasstimeGoalInfo& tGoal, const Vector& vRelativePos);
public:
	void Init();
	void Update();
	CPasstimeBall* GetBall();
	CTFPasstimeLogic* GetLogic() { return m_pLogic; }
	int GetCarrier();
	float GetMaxPassRange() { return m_pLogic ? m_pLogic->m_flMaxPassRange() : FLT_MAX; }
	bool GetGoalInfo(int iScoringTeam, const Vector& vRelativePos, PasstimeGoalInfo& tOut);
	bool GetGoalPos(int iScoringTeam, const Vector& vRelativePos, Vector& vOut);
	bool GetBallPos(Vector& vOut);
	bool IsEndzoneGoal(int iGoalType) const { return iGoalType == CFuncPasstimeGoal::TYPE_ENDZONE; }
	bool IsPointInGoal(const PasstimeGoalInfo& tGoal, const Vector& vPoint) const;
};

class CDoomsdayController
{
public:
	CCaptureFlag* GetFlag();
	bool GetCapturePos(Vector& vOut);
	bool GetGoal(Vector& vOut);
	void Init();
	std::wstring m_sDoomsdayStatus = L"";
private:
	Vector m_vCachedCapturePos = {};
	bool m_bHasCachedCapturePos = false;
};

class CHaarpController
{
public:
	bool GetCapturePos(Vector& vOut);
	bool GetDefensePos(Vector& vOut);
	void Init();
	std::wstring m_sHaarpStatus = L"";
private:
	Vector m_vCachedCapturePos = {};
	bool m_bHasCachedCapturePos = false;
	Vector m_vCachedBluCapturePos = {};
	bool m_bHasCachedBluCapturePos = false;
};

Enum(PDGoal, None, Deliver, Collect, Hunt, Escort);

struct PDGoal_t
{
	PDGoalEnum::PDGoalEnum m_eKind = PDGoalEnum::None;
	Vector m_vPos = {};
	int m_iCarried = 0;
	int m_iTargetIdx = -1;
};

struct PDPickup_t
{
	CCaptureFlag* m_pFlag = nullptr;
	Vector m_vPos = {};
	int m_iPoints = 0;
};

class CPDController
{
	std::vector<PDPickup_t> m_vPickups = {};
	std::vector<CaptureZone_t> m_vZones = {};
	int m_iLogicIdx = -1;
	Timer m_tLogicScan = {};
	bool GetDeliveryGoal(CTFPlayer* pLocal, bool bPathCosts, PDGoal_t& tOut) const;
	bool GetCollectGoal(CTFPlayer* pLocal, bool bPathCosts, PDGoal_t& tOut) const;
public:
	void Init();
	void Update();
	bool GetGoal(CTFPlayer* pLocal, bool bPathCosts, PDGoal_t& tOut);
	int GetCarriedPoints(CTFPlayer* pPlayer) const;
	CTFPlayer* GetTeamLeader(int iTeam) const;
	const CaptureZone_t* GetZoneAt(const Vector& vPos, int iTeam) const;
};

Enum(RDGoal, None, Deliver, Collect, Attack);

struct RDGoal_t
{
	RDGoalEnum::RDGoalEnum m_eKind = RDGoalEnum::None;
	Vector m_vPos = {};
};

class CRDController
{
	std::vector<Vector> m_vPickups = {};
	std::vector<Vector> m_vPacks = {};
	std::vector<Vector> m_vRobots = {};
	std::vector<CaptureZone_t> m_vZones = {};
	Timer m_tScan = {};
	void ScanEntities();
public:
	void Init();
	void Update();
	bool GetGoal(CTFPlayer* pLocal, bool bPathCosts, RDGoal_t& tOut);
	const CaptureZone_t* GetZoneAt(const Vector& vPos, int iTeam) const;
};

Enum(MVMTask, None,
	Tank,
	Combat,
	Money,
	Frontline,
	Ammo,
	Health
)

class CMVMController
{
	bool m_bActive = false;
	Timer m_tAnchorRefresh = {};
	std::vector<Vector> m_vSpawnAnchors = {};
	bool IsSupportedClass(CTFPlayer* pLocal) const;
	bool PrimaryHasAmmo() const;
	bool DesiredCombatWeaponCanFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) const;
	bool GetTankTarget(CBaseEntity*& pOut) const;
	bool GetRobotTarget(CTFPlayer* pLocal, CBaseEntity*& pOut) const;
	bool GetMoneyTarget(CTFPlayer* pLocal, CBaseEntity*& pOut) const;
	bool GetFrontlineTarget(CTFPlayer* pLocal, Vector& vOut);
	void RefreshSpawnAnchors(CTFPlayer* pLocal);
	bool RunTank(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTank);
	bool RunCombat(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget);
	bool RunMoney(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseEntity* pMoney);
	bool RunFrontline(CTFPlayer* pLocal);
public:
	MVMTaskEnum::MVMTaskEnum m_eTask = MVMTaskEnum::None;
	void Update();
	void Reset();
	bool IsActive() const { return m_bActive; }
	bool WantsPrimary(CTFPlayer* pLocal) const;
	bool WantsScoutSecondary(CTFPlayer* pLocal) const;
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
};

class CNavArea;

struct ZIGoal_t
{
	bool m_bValid = false;
	bool m_bHold = false;
	Vector m_vPos = {};
	float m_flValue = 0.f;
	float m_flScore = 0.f;
	const wchar_t* m_sStatus = L"";
};

struct ZISpawnSample_t
{
	Vector m_vOrigin = {};
	float m_flScore = FLT_MAX;
};

class CZIController
{
	bool m_bActive = false;
	bool m_bZombie = false;
	bool m_bSurvivor = false;
	bool m_bSetup = false;
	int m_iSurvivorsAlive = 0;
	int m_iZombiesAlive = 0;

	Timer m_tPlan = {};
	ZIGoal_t m_tGoal = {};
	int m_iTargetIdx = -1;
	bool m_bHasHoldSpot = false;
	Vector m_vHoldSpot = {};

	std::vector<CNavArea*> m_vRallyCandidates = {};
	CNavArea* m_pRally = nullptr;
	const void* m_pRallyData = nullptr;
	size_t m_nRallyAreas = 0;
	size_t m_nRallyRooms = 0;
	Timer m_tRallyCheck = {};

	bool m_bPicking = false;
	bool m_bPickSelecting = false;
	bool m_bPickConfirmed = false;
	float m_flPickStart = 0.f;
	float m_flPickNextCycle = 0.f;
	std::vector<ZISpawnSample_t> m_vPickSamples = {};
	Vector m_vPickBest = {};

	void GetZombieRooms(std::vector<Vector>& vOut) const;
	void BuildRallyCandidates();
	bool GetRally(CTFPlayer* pLocal, Vector& vOut);
	bool PickHoldSpot(CTFPlayer* pLocal, const Vector& vAnchor, const std::vector<Vector>& vMates, Vector& vOut);
	void PlanZombie(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void PlanSurvivor(CTFPlayer* pLocal);
	void RunSpawnPicker(CTFPlayer* pLocal, CUserCmd* pCmd);

public:
	void Update();
	void Reset();
	void CreateMove(CTFPlayer* pLocal, CUserCmd* pCmd);
	bool IsActive() const { return m_bActive; }
	bool IsZombie() const { return m_bActive && m_bZombie; }
	bool IsSurvivor() const { return m_bActive && m_bSurvivor; }
	bool IsSpawning(CTFPlayer* pLocal) const;
	const ZIGoal_t& GetGoal() const { return m_tGoal; }
};

struct VSHZone_t
{
	Vector m_vFrom = {};
	Vector m_vTo = {};
	float m_flRadius = 0.f;
	float m_flInnerRadius = 0.f;
	bool m_bLane = false;
	bool m_bStrict = false;
};

Enum(VSHAbility, None, Charge, Slam);

struct VSHBoss_t
{
	CTFPlayer* m_pPlayer = nullptr;
	int m_iIdx = -1;
	int m_iTeam = TEAM_UNASSIGNED;
	bool m_bDormant = false;
	bool m_bHasPos = false;
	bool m_bWindup = false;
	bool m_bDashing = false;
	bool m_bSlamming = false;
	bool m_bPunchReady = false;
	Vector m_vOrigin = {};
};

class CVSHController
{
	VSHBoss_t m_tBoss = {};
	int m_iMercTeam = TEAM_UNASSIGNED;
	bool m_bLocalBoss = false;
	bool m_bInSetup = false;
	int m_iHuntIdx = -1;

	VSHAbilityEnum::VSHAbilityEnum m_eAbility = VSHAbilityEnum::None;
	int m_iPhase = 0;
	int m_iPhaseTicks = 0;
	int m_iWindupTicks = 0;
	int m_iAbilityTarget = -1;
	bool m_bSawDash = false;
	float m_flAbilityStart = 0.f;
	float m_flChargeReady = 0.f;
	float m_flSlamReady = 0.f;
	float m_flLastJump = 0.f;
	Timer m_tDecision = {};

	void EndAbility(bool bSuccess);
	bool TryStartCharge(CTFPlayer* pLocal, CTFPlayer* pTarget, float flDist);
	bool TryStartSlam(CTFPlayer* pLocal, CTFPlayer* pTarget);
	void TickCharge(CTFPlayer* pLocal, CTFPlayer* pTarget, CUserCmd* pCmd);
	void TickSlam(CTFPlayer* pLocal, CTFPlayer* pTarget, CUserCmd* pCmd);
public:
	void Update();
	void Reset();
	void RunBoss(CTFPlayer* pLocal, CUserCmd* pCmd);
	bool IsActive() const { return m_tBoss.m_iIdx != -1; }
	bool IsBossLocal() const { return m_bLocalBoss; }
	bool IsBoss(int iIdx) const { return iIdx == m_tBoss.m_iIdx; }
	bool IsMovementHeld() const;
	float GetStandoff(CTFPlayer* pLocal) const;
	void GetBossZones(CTFPlayer* pLocal, std::vector<VSHZone_t>& vOut) const;
	float GetHuntScore(CTFPlayer* pTarget, float flDist);
	void SetHuntTarget(int iIdx) { m_iHuntIdx = iIdx; }
	bool GetGatherPoint(CTFPlayer* pLocal, Vector& vOut) const;
	bool GetPatrolAnchor(CTFPlayer* pLocal, Vector& vOut) const;
};

Enum(MissionKind, None,
	CtfSteal, CtfCarry, CtfEscort,
	ControlPoint, Payload, Passtime, Doomsday, MVM,
	PlayerDestruction, RobotDestruction, ZombieInfection);

struct Mission_t
{
	MissionKindEnum::MissionKindEnum m_eKind = MissionKindEnum::None;
	Vector m_vPos = {};
	float m_flValue = 0.f;
	float m_flScore = 0.f;
	int m_iCarrierIdx = -1;
	bool m_bValid = false;
};

class CMissionBoard
{
private:
	Mission_t m_tMission = {};
	float m_flUrgency = 0.f;
	int m_iFriendliesNear = 0;
	int m_iEnemiesNear = 0;

	void SetMission(MissionKindEnum::MissionKindEnum eKind, const Vector& vPos, float flValue, float flScore, int iCarrierIdx, const wchar_t* sStatus);
	void UpdateCtf(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam);
	void UpdateCp(CTFPlayer* pLocal, int iOurTeam);
	void UpdatePayload(CTFPlayer* pLocal, int iOurTeam);
	void UpdatePasstime(CTFPlayer* pLocal, int iOurTeam);
	void UpdatePlayerDestruction(CTFPlayer* pLocal);
	void UpdateRobotDestruction(CTFPlayer* pLocal);
	void UpdateZombieInfection();
	void CountForces(CTFPlayer* pLocal, const Vector& vPos);

public:
	void Update(CTFPlayer* pLocal);
	void Reset();

	const Mission_t& GetMission() const { return m_tMission; }
	float GetUrgency() const { return m_flUrgency; }
	int GetFriendliesNear() const { return m_iFriendliesNear; }
	int GetEnemiesNear() const { return m_iEnemiesNear; }

	std::wstring m_sStatus = L"";
};

ADD_FEATURE(CMissionBoard, MissionBoard);
ADD_FEATURE(CGameObjectiveController, GameObjectiveController);
ADD_FEATURE(CFlagController, FlagController);
ADD_FEATURE(CCPController, CPController);
ADD_FEATURE(CPLController, PLController);
ADD_FEATURE(CPasstimeController, PasstimeController);
ADD_FEATURE(CDoomsdayController, DoomsdayController);
ADD_FEATURE(CHaarpController, HaarpController);
ADD_FEATURE(CPDController, PDController);
ADD_FEATURE(CRDController, RDController);
ADD_FEATURE(CMVMController, MVMController);
ADD_FEATURE(CVSHController, VSHController);
ADD_FEATURE(CZIController, ZIController);
