#pragma once
#include "../../SDK/SDK.h"
#include "../../Utils/Timer/Timer.h"

struct ClosestEnemy_t
{
	int m_iEntIdx = -1;
	CTFPlayer* m_pPlayer = nullptr;
	Vector m_vOrigin = {};
	float m_flDist = FLT_MAX;
	float m_flDistZ = FLT_MAX;
};

Enum(ShouldTarget, Invalid = -1, DontTarget, Target);

enum EJumpState
{
	STATE_AWAITING_JUMP,
	STATE_CTAP,
	STATE_JUMP,
	STATE_ASCENDING,
	STATE_DESCENDING
};

class CBotUtils
{
private:
	std::unordered_map<int, bool> m_mAutoScopeCache;
	std::unordered_map<int, bool> m_mAutoRevCache;
	bool m_bScopeKeep = false;
	bool m_bScopeClearCache = false;
	Timer m_tScopeTimer = {};
	bool m_bRevKeep = false;
	bool m_bRevClearCache = false;
	Timer m_tRevTimer = {};
	ClosestEnemy_t UpdateCloseEnemies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	bool HasMedigunTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void UpdateBestSlot(CTFPlayer* pLocal);
	void DoSlowAim(Vec3& vWishAngles, float flSpeed, Vec3 vPreviousAngles);
	bool IsSurfaceWalkable(const Vector& vNormal);
	bool SmartJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoScope(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void AutoRev(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	EJumpState m_eJumpState = STATE_AWAITING_JUMP;

	struct LegitMotion_t
	{
		Vec3 m_vAngles = {};
		Vec3 m_vVelocity = {};
		float m_flLastTime = 0.f;
		bool m_bInitialized = false;
	};

	struct LegitAim_t
	{
		int m_iTarget = 0;
		int m_iPreviousTarget = 0;
		int m_iLastTick = -1;
		int m_iLastCommand = -1;
		int m_iCommitUntilTick = -1;
		Vec3 m_vExact = {};
		Vec3 m_vError = {};
		Vec3 m_vOvershoot = {};
		Vec3 m_vCommitAngles = {};
		float m_flLastSeen = 0.f;
		float m_flReactionEnd = 0.f;
		float m_flNoisePhase = 0.f;
		float m_flErrorBlend = 0.f;
		bool m_bReady = false;
		LegitMotion_t m_tMotion = {};
	};

	struct LegitLook_t
	{
		int m_iTarget = -1;
		int m_iLastTick = -1;
		int m_iLastCommand = -1;
		int m_iPathScanTick = -100;
		Vec3 m_vTargetPoint = {};
		Vec3 m_vLastSeenPoint = {};
		Vec3 m_vPathGoal = {};
		Vec3 m_vPathDest = {};
		Vec3 m_vPathDirection = {};
		Vec3 m_vThreatPoint = {};
		Vec3 m_vGlanceGoal = {};
		Vec3 m_vGlanceCurrent = {};
		int m_iThreat = -1;
		float m_flTargetSeen = 0.f;
		float m_flTargetScore = 0.f;
		float m_flPathScore = 0.f;
		float m_flTargetDwell = 0.f;
		float m_flReactionEnd = 0.f;
		float m_flThreatUntil = 0.f;
		float m_flPhase = 0.f;
		float m_flNextGlance = 0.f;
		float m_flGlanceUntil = 0.f;
		float m_flNextTargetScan = 0.f;
		float m_flNextThreatScan = 0.f;
		bool m_bTargetValid = false;
		bool m_bGlancing = false;
		LegitMotion_t m_tMotion = {};
	};

	LegitAim_t m_tLegitAim = {};
	LegitLook_t m_tLLAP = {};

	void AdvanceLegitMotion(LegitMotion_t& tMotion, const Vec3& vGoal, float flSpeed, float flAcceleration);
	float GetLegitReactionTime();
	bool IsLegitShotClear(CTFPlayer* pLocal, CBaseEntity* pTarget, const Vec3& vAngles);
	CBaseEntity* FindLegitThreat(CTFPlayer* pLocal, const Vec3& vEye);
	CBaseEntity* FindLegitAttention(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3& vEye, const Vec3& vOrigin, const Vec3& vPathDirection, bool bHasPathTarget, bool bAllowSteep, float* pScore = nullptr);
	Vec3 GetLegitPathGoal(CTFPlayer* pLocal, const Vec3& vEye, const Vec3& vOrigin, const Vec3& vDest, const Vec3& vPathDirection, bool bAllowSteep);
	Vec3 GetLegitActorPoint(CBaseEntity* pActor);

public:
	Vector m_vPredictedJumpPos = {};
	Vector m_vJumpPeakPos = {};
	Vector m_vDebugLook = {};
	float m_flDebugLookExpire = 0.f;

	int m_iCurrentSlot = -1;
	int m_iBestSlot = -1;
	ClosestEnemy_t m_tClosestEnemy = {};
	Vec3 m_vLastAngles = {};

	bool ShouldAssist(CTFPlayer* pLocal, int iEntIdx);
	ShouldTargetEnum::ShouldTargetEnum ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIdx);
	ShouldTargetEnum::ShouldTargetEnum ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx);

	bool GetDormantOrigin(int iIndex, Vector* pOut = nullptr);

	void SetSlot(CTFPlayer* pLocal, int iSlot);

	void LookAtPath(CUserCmd* pCmd, Vec2 vDest, Vec3 vLocalEyePos, bool bSilent);
	void LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth = true);
	void LookLegit(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vDest, bool bSilent, bool bFocus = false);
	void AimLegit(CTFPlayer* pLocal, CUserCmd* pCmd, int iTarget, const Vec3& vToAngle);
	bool IsLegitAimReady(int iTarget) const;
	bool IsLegitAimActive() const;
	void LockLegitAim(int iTarget, int iTicks);
	void InvalidateLLAP();

	void HandleSmartJump(CTFPlayer* pLocal, CUserCmd* pCmd);

	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
};

namespace NavRuntime
{
	bool IsMovementLocked(CTFPlayer* pLocal);
	bool IsMinigunJumpLocked(CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool CanUseNavJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	inline bool CanIssueNavJump(CTFWeaponBase* pWeapon, CUserCmd* pCmd) { return !IsMinigunJumpLocked(pWeapon, pCmd); }
}

ADD_FEATURE(CBotUtils, BotUtils);
