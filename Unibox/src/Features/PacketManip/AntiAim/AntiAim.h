#pragma once
#include "../../../SDK/SDK.h"

class CAntiAim
{
private:
	void FakeShotAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	float GetYawOffset(CTFPlayer* pEntity, bool bFake);
	float GetBaseYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake);
	float GetYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake);
	float GetPitch(float flCurPitch);
	void MinWalk(CTFPlayer* pLocal, CUserCmd* pCmd);
	void ResetState();
	bool CheckAndResetTime();
	int GetJitter(uint32_t uHash);
	void PlanAutoYaw(CTFPlayer* pLocal);
	void StepShown(CTFPlayer* pLocal, const Vec3& vSent, int iSteps);

	struct AutoTarget_t
	{
		Vec3 m_vEye = {};
		bool m_bSeesHead = false;
	};

	struct HeadScan_t
	{
		const mstudiohitboxset_t* m_pSet = nullptr;
		const mstudiobbox_t* m_pHead = nullptr;
		const mstudioposeparamdesc_t* m_pAimYaw = nullptr;
		Vec3 m_vOrigin = {};
		float m_flGoalFeet = 0.f;
		float m_flCurrentFeet = 0.f;
		bool m_bMoving = false;
		float m_flHeadYaw = 0.f;
		float m_flShownYaw = 0.f;
		Vec3 m_aPoints[9] = {};
		Vec3 m_aShownPoints[9] = {};
		Vec3 m_vFrontEye = {};
		int m_aExposed[24] = {};
		int m_aShownExposed[24] = {};
		matrix3x4 m_aBones[MAXSTUDIOBONES] = {};
	};

	struct ShownState_t
	{
		float m_flEyeYaw = 0.f;
		float m_flEyePitch = 0.f;
		float m_flGoalFeetYaw = 0.f;
		float m_flCurrentFeetYaw = 0.f;
		float m_flLastAimTurnTime = 0.f;
		bool m_bCurrentFeetYawInitialized = false;
		MultiPlayerPoseData_t m_tPoseData = {};
		QAngle m_vRender = {};
		std::array<float, 24> m_aPose = {};
		int m_iSequence = 0;
		float m_flCycle = 0.f;
	};

	bool ScanHead(CTFPlayer* pLocal);
	void CountExposure(const matrix3x4* aBones, const Vec3* aPoints, float flBuiltYaw, int* aExposed);
	float SettledHeadYaw(float flRealYaw, float flFakeYaw);
	int CountShotOverlap(const Vec3& vEye, float flHeadYaw, float flFakeYaw);
	int CountOverlap(float flHeadYaw, float flFakeYaw);

	std::vector<AutoTarget_t> m_vTargets = {};
	HeadScan_t m_tScan = {};
	ShownState_t m_tShown = {};
	bool m_bShownValid = false;
	bool m_bShownBones = false;
	matrix3x4 m_aShownBones[MAXSTUDIOBONES] = {};
	float m_flSentYaw = 0.f;
	int m_iPlannedChoke = 0;
	int m_iLastChoked = 0;
	bool m_bAutoValid = false;
	float m_flAutoReal = 0.f;
	float m_flAutoFake = 0.f;

public:
	static constexpr int AutoYawBins = 24;
	static constexpr int HeadPoints = 9;

	struct YawScore_t
	{
		int m_iRealExposed = 0;
		int m_iOverlap = 0;
		int m_iFakeExposed = 0;
	};

private:
	YawScore_t ScoreYaws(float flRealYaw, float flFakeYaw);
	float BestRealYaw(float flFakeYaw);
	float BestFakeYaw(float flRealYaw);
	std::unordered_map<uint32_t, bool> m_mJitter = {};
	float m_flUltraNextChange[2] = {};
	float m_flUltraYawOffset[2] = {};
	bool m_bUltraSpin[2] = {};
	float m_flUltraSpinSpeed[2] = {};
	bool m_bSideways = false;
	float m_flOmegaYaw = 0.f;
	float m_flTornadoYaw[2] = {};
	float m_flTornadoSpeed[2] = {};
	int m_iTornadoRetune[2] = {};
	float m_flHelixPhase[2] = {};
	int m_iQuantumShift[2] = {};
	float m_flQuantumYaw[2] = {};
	float m_flPitchUltraNext = 0.f;
	float m_flPitchUltra = 0.f;
	float m_flPitchUltraNextFake = 0.f;
	float m_flPitchUltraFake = 0.f;
	int m_iMoonwalkNext = 0;
	float m_flMoonwalkPitch = 0.f;
	bool m_bTimedFlipUp = false;
	float m_flTimedFlipNext = 0.f;
	bool m_bTimedFlipRandUp = false;
	float m_flTimedFlipRandNext = 0.f;
	bool m_bMinWalkVar = true;
	float m_flLastCurTime = 0.f;
	int m_iLastTick = 0;

public:
	bool AntiAimOn();
	bool YawOn();
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Draw(CTFPlayer* pLocal);
	void OnLevelInit();
	
	inline int AntiAimTicks() { return 2; }

	Vec2 vFakeAngles = {};
	Vec2 vRealAngles = {};
	std::vector<std::pair<Vec3, Vec3>> vEdgeTrace = {};
};

ADD_FEATURE(CAntiAim, AntiAim);