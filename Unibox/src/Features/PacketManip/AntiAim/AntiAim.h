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