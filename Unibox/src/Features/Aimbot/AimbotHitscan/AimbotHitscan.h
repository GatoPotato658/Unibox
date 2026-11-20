#pragma once
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"

struct HitscanHitbox_t
{
	const mstudiobbox_t* m_pBox = nullptr;
	std::optional<Vec3> m_vPos = std::nullopt;

	int m_iHitbox = 0;
};

class CAimbotHitscan
{
private:
	std::vector<HitscanHitbox_t> GetHitboxes(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget);
	int GetHitboxPriority(int nHitbox, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget);
	void GetHitboxInfo(HitscanHitbox_t& tHitbox, CBaseEntity* pTarget, matrix3x4* aBones = nullptr, const matrix3x4** pTransform = nullptr, Vec3* pMins = nullptr, Vec3* pMaxs = nullptr);
	float GetMultipointScale(CBaseEntity* pTarget, CTFWeaponBase* pWeapon, int nHitbox);
	int CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const CUserCmd* pCmd);

	bool Aim(const Vec3& vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
	void Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod = Vars::Aimbot::General::AimType.Value);
	bool ShouldFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, const Target_t& tTarget);

	Vec3 m_vEyePos = {};

	matrix3x4 m_mMatrix = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
	matrix3x4 m_mHullMatrix = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };

	std::optional<TickRecord> m_tForwardRecord = {};
	TickRecord* BuildForwardRecord(CTFPlayer* pTarget, const TickRecord* pBaseRecord, const CUserCmd* pCmd);

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	void BacktrackToCrosshair(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
};

ADD_FEATURE(CAimbotHitscan, AimbotHitscan);
