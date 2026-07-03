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
	ClosestEnemy_t UpdateCloseEnemies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	bool HasMedigunTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void UpdateBestSlot(CTFPlayer* pLocal);

	EJumpState m_eJumpState = STATE_AWAITING_JUMP;

	struct LegitLook_t
	{
		Vec3 m_vAnchor = {};
		Vec3 m_vOffset = {};
		Vec3 m_vOffsetGoal = {};
		Vec3 m_vLastTarget = {};
		Vec3 m_vPathTarget = {};
		Vec3 m_vPathTargetGoal = {};
		Vec3 m_vGlanceCurrent = {};
		Vec3 m_vGlanceGoal = {};
		int m_iLastTarget = -1;
		float m_flLastSeen = 0.f;
		Vec3 m_vLastPos = {};
		float m_flPathTargetTime = 0.f;
		float m_flPathTargetScore = 0.f;
		float m_flNextOffset = 1.f;
		float m_flAcquireDuration = 0.f;
		float m_flEnemyBlend = 0.f;
		float m_flPhase = 0.f;
		float m_flGlanceDuration = 0.f;
		float m_flNextGlance = 0.f;
		bool m_bInitialized = false;
		bool m_bGlancing = false;
		Timer m_tOffsetTimer = {};
		Timer m_tAcquireTimer = {};
		Timer m_tGlanceTimer = {};
		Timer m_tGlanceCooldown = {};

		float m_flNextScan = 0.f;
		Timer m_tScanTimer = {};

		float m_flErrorX = 0.f;
		float m_flErrorY = 0.f;
		float m_flErrorVelocityX = 0.f;
		float m_flErrorVelocityY = 0.f;
		float m_flPitchVelocity = 0.f;
		float m_flYawVelocity = 0.f;
	};

	LegitLook_t m_tLLAP = {};

public:
	Vector m_vPredictedJumpPos = {};
	Vector m_vJumpPeakPos = {};
	std::vector<std::pair<Vector, Vector>> m_vWalkableSegments = {};

	int m_iCurrentSlot = -1;
	int m_iBestSlot = -1;
	ClosestEnemy_t m_tClosestEnemy = {};
	Vec3 m_vLastAngles = {};

	bool ShouldAssist(CTFPlayer* pLocal, int iEntIdx);
	ShouldTargetEnum::ShouldTargetEnum ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIdx);
	ShouldTargetEnum::ShouldTargetEnum ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx);

	bool GetDormantOrigin(int iIndex, Vector* pOut = nullptr);

	void SetSlot(CTFPlayer* pLocal, int iSlot);

	void DoSlowAim(Vec3& vWishAngles, float flSpeed, Vec3 vPreviousAngles);
	void LookAtPath(CUserCmd* pCmd, Vec2 vDest, Vec3 vLocalEyePos, bool bSilent);
	void LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth = true);
	void LookLegit(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vDest, bool bSilent);
	void InvalidateLLAP();

	bool IsSurfaceWalkable(const Vector& vNormal);
	bool SmartJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void HandleSmartJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void ForceJump() { if (m_eJumpState == STATE_AWAITING_JUMP) m_eJumpState = Vars::Misc::Movement::AutoCTap.Value ? STATE_CTAP : STATE_JUMP; }

	void AutoScope(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void AutoRev(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
};

ADD_FEATURE(CBotUtils, BotUtils);
