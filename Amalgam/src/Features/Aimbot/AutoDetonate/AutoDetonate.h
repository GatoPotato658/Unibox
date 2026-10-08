#pragma once
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"
#include <unordered_set>
#include <optional>

class CAutoDetonate
{
private:
	void PredictPlayer(CBaseEntity* pLocal, CBaseEntity* pTarget, float flLatency);
	void RestorePlayer(CBaseEntity* pTarget);

	void ApplyDamageDebuffs(CBaseEntity* pTarget, float& flDamage, float flDamageNoBuffs);
	float GetTotalDamageForTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget, std::unordered_set<CTFGrenadePipebombProjectile*> vStickies, std::unordered_map<int, Vec3> vPredictedStickyOrigins, std::unordered_map<int, float> vRadiuses, float& flDamageNoBuffs, bool bUseDist = true);
	float GetTotalDamageOfStickies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, std::vector<CTFGrenadePipebombProjectile*> vStickies, std::unordered_map<int, float> vRadiuses, float& flDamageNoBuffs);

	bool SkipTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget);
	bool CanKill(CBaseEntity* pTarget, float& flDamage, float flDamageNoBuffs, float& flMaxDamage, float flMaxDamageNoBuffs, int iMaxHealth);
	bool CanSee(CBaseEntity* pTarget, CBaseEntity* pProjectile, const Vec3 vProjectileOrigin, const float flRadius, Vec3* vOut = nullptr, Vec3* vCustomTargetPos = nullptr) const;

	bool AimAtSticky(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pSticky);

	bool FlareCheck(CTFPlayer* pLocal);
	bool StickyCheck(CTFPlayer* pLocal, CUserCmd* pCmd);

	std::optional<Vector> m_vRestore;
public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd);
};

ADD_FEATURE(CAutoDetonate, AutoDetonate);