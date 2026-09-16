#pragma once
#include "../../SDK/SDK.h"

#define MAX_CONTROL_POINTS 8
#define MAX_PREVIOUS_POINTS 3

class CGameObjectiveController
{
public:
	ETFGameType m_eGameMode = TF_GAMETYPE_UNDEFINED;
	bool m_bDoomsday = false;
	bool m_bHaarp = false;
	void Update();
	void Reset();
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

struct CPInfo
{
	int m_iIdx = -1;
	Vector m_vPos = {};
	bool m_bGotPos = false;
	std::array<bool, 2> m_bCanCap = { false, false };
	CPInfo() = default;
};

class CCPController
{
	std::array<CPInfo, MAX_CONTROL_POINTS> m_aControlPointData;
	CBaseTeamObjectiveResource* m_pObjectiveResource = nullptr;
	void UpdateObjectiveResource();
	void UpdateControlPoints();

	struct PointIgnore
	{
		std::string m_sMapName;
		int m_iPointIdx;
		PointIgnore(std::string sName, int iIndex) : m_sMapName{ std::move(sName) }, m_iPointIdx{ iIndex } {};
	};
	std::array<PointIgnore, 1> m_aIgnorePoints{ PointIgnore("cp_steel", 4) };

	bool TeamCanCapPoint(int iIndex, int iTeam);
	int GetPreviousPointForPoint(int iIndex, int iTeam, int iPrevIdx);
	int GetFarthestOwnedControlPoint(int iTeam);
public:
	bool IsPointUseable(int iIndex, int iTeam);
	bool GetClosestControlPoint(Vector vPos, int iTeam, Vector& vOut);
	bool GetClosestControlPointInfo(Vector vPos, int iTeam, std::pair<int, Vector>& tOut);
	void Init();
	void Update();
};

class CPLController
{
	std::array<std::array<CObjectCartDispenser*, MAX_EDICTS>, 2> m_aPayloads = {};
	std::array<size_t, 2> m_aPayloadCounts = {};
public:
	CObjectCartDispenser* GetClosestPayload(Vector vPos, int iTeam);
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
	void Update();
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
	void Update();
	std::wstring m_sHaarpStatus = L"";
private:
	Vector m_vCachedCapturePos = {};
	bool m_bHasCachedCapturePos = false;
	Vector m_vCachedBluCapturePos = {};
	bool m_bHasCachedBluCapturePos = false;
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

ADD_FEATURE(CGameObjectiveController, GameObjectiveController);
ADD_FEATURE(CFlagController, FlagController);
ADD_FEATURE(CCPController, CPController);
ADD_FEATURE(CPLController, PLController);
ADD_FEATURE(CPasstimeController, PasstimeController);
ADD_FEATURE(CDoomsdayController, DoomsdayController);
ADD_FEATURE(CHaarpController, HaarpController);
ADD_FEATURE(CMVMController, MVMController);
