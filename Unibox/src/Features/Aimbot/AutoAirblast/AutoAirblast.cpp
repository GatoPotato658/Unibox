#include "AutoAirblast.h"

#include "../AimbotProjectile/AimbotProjectile.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../Backtrack/Backtrack.h"

static inline bool ShouldTarget(CBaseEntity* pProjectile, CTFPlayer* pLocal)
{
	if (pProjectile->m_iTeamNum() == pLocal->m_iTeamNum() || pProjectile->m_MoveType() == MOVETYPE_NONE)
		return false;

	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_SentryRocket:
	case ETFClassID::CTFProjectile_BallOfFire:
	case ETFClassID::CTFProjectile_EnergyBall:
	case ETFClassID::CTFProjectile_MechanicalArmOrb:
	case ETFClassID::CTFProjectile_Flare:
	case ETFClassID::CTFProjectile_Arrow:
	case ETFClassID::CTFProjectile_HealingBolt:
		break;
	case ETFClassID::CTFGrenadePipebombProjectile:
	{
		auto pPipe = pProjectile->As<CTFGrenadePipebombProjectile>();
		switch (pPipe->m_iType())
		{
		case TF_GL_MODE_REMOTE_DETONATE:
			if (pPipe->m_bTouched())
				return false;
			break;
		case TF_GL_MODE_REMOTE_DETONATE_PRACTICE:
			return false;
		}
		break;
	}
	case ETFClassID::CTFStunBall:
	case ETFClassID::CTFProjectile_Cleaver:
	case ETFClassID::CTFProjectile_Jar:
	case ETFClassID::CTFProjectile_JarMilk:
	case ETFClassID::CTFProjectile_JarGas:
		if (pProjectile->As<CTFGrenadePipebombProjectile>()->m_bTouched())
			return false;
		break;
	default:
		return false;
	}

	if (auto pWeapon = F::ProjSim.GetEntities(pProjectile).first)
	{
		if (!SDK::AttribHookValue(1, "mult_dmg", pWeapon))
			return false;
	}

	return true;
}

static inline bool IsCritical(CBaseEntity* pProjectile)
{
	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_SentryRocket:
	case ETFClassID::CTFProjectile_BallOfFire:
	case ETFClassID::CTFProjectile_MechanicalArmOrb:
		return pProjectile->As<CTFProjectile_Rocket>()->m_bCritical();
	case ETFClassID::CTFProjectile_Flare:
		return pProjectile->As<CTFProjectile_Flare>()->m_bCritical();
	case ETFClassID::CTFProjectile_Arrow:
	case ETFClassID::CTFProjectile_HealingBolt:
		return pProjectile->As<CTFProjectile_Arrow>()->m_bCritical();
	case ETFClassID::CTFProjectile_EnergyBall:
		return false;
	default:
		return pProjectile->As<CTFWeaponBaseGrenadeProj>()->m_bCritical();
	}
}

bool CAutoAirblast::CanAirblastEntity(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pEntity, const Vec3& vAngle)
{
	auto flRadius = SDK::AttribHookValue(1, "deflection_size_multiplier", pWeapon) * 128.f;

	Vec3 vForward; Math::AngleVectors(vAngle, &vForward);
	Vec3 vOrigin = pLocal->GetShootPos() + vForward * flRadius;

	CBaseEntity* pTarget;
	for (CEntitySphereQuery sphere(vOrigin, flRadius);
		pTarget = sphere.GetCurrentEntity();
		sphere.NextEntity())
	{
		if (pTarget == pEntity)
			break;
	}

	return pTarget == pEntity && SDK::VisPosWorld(pLocal, pEntity, pLocal->GetShootPos(), pEntity->GetAbsOrigin());
}

void CAutoAirblast::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!(Vars::Aimbot::Projectile::AutoAirblast.Value & Vars::Aimbot::Projectile::AutoAirblastEnum::Enabled) || !G::CanSecondaryAttack)
		return;

	PROJ_FRAME("AutoAirblast", pWeapon ? pWeapon->GetWeaponID() : -1);

	const int iWeaponID = pWeapon->GetWeaponID();
	if (iWeaponID != TF_WEAPON_FLAMETHROWER && iWeaponID != TF_WEAPON_FLAME_BALL || SDK::AttribHookValue(0, "airblast_disabled", pWeapon))
		return;

	static auto tf_flamethrower_burstammo = H::ConVars.FindVar("tf_flamethrower_burstammo");
	int iAmmoPerShot = tf_flamethrower_burstammo->GetInt() * SDK::AttribHookValue(1, "mult_airblast_cost", pWeapon);
	int iAmmo = pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType());
	int iBuffType = SDK::AttribHookValue(0, "set_buff_type", pWeapon);
	int iChargedAirblast = SDK::AttribHookValue(0, "set_charged_airblast", pWeapon);
	if (iAmmo < iAmmoPerShot || iBuffType || iChargedAirblast || pLocal->m_nWaterLevel() == WL_Eyes || pLocal->IsTaunting())
		return;

	const Vec3 vEyePos = pLocal->GetShootPos();
	const bool bRedirect = Vars::Aimbot::Projectile::AutoAirblast.Value & Vars::Aimbot::Projectile::AutoAirblastEnum::Redirect;
	CBaseEntity* pBest = nullptr;
	Vec3 vBestOrigin = {};
	bool bBestCritical = false;
	float flBestDistance = std::numeric_limits<float>::max();

	float flLatency = std::max(F::Backtrack.GetReal() - 0.03f, 0.f);
	for (auto pProjectile : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (!ShouldTarget(pProjectile, pLocal))
			continue;

		Vec3 vOrigin;
		if (!SDK::PredictOrigin(vOrigin, pProjectile->m_vecOrigin(), F::ProjSim.GetVelocity(pProjectile), flLatency))
			continue;

		if (!(Vars::Aimbot::Projectile::AutoAirblast.Value & Vars::Aimbot::Projectile::AutoAirblastEnum::IgnoreFOV)
			&& !F::AimbotGlobal.InAimFOV(Math::CalcFov(I::EngineClient->GetViewAngles(), Math::CalcAngle(vEyePos, vOrigin))))
			continue;

		const bool bCritical = IsCritical(pProjectile);
		const float flDistance = vEyePos.DistToSqr(vOrigin);
		if (bCritical != bBestCritical ? bBestCritical : flDistance >= flBestDistance)
			continue;

		Vec3 vRestoreOrigin = pProjectile->GetAbsOrigin();
		pProjectile->SetAbsOrigin(vOrigin);
		const bool bCanBlast = CanAirblastEntity(pLocal, pWeapon, pProjectile, bRedirect ? Math::CalcAngle(vEyePos, vOrigin) : pCmd->viewangles);
		pProjectile->SetAbsOrigin(vRestoreOrigin);
		if (!bCanBlast)
			continue;

		pBest = pProjectile;
		vBestOrigin = vOrigin;
		bBestCritical = bCritical;
		flBestDistance = flDistance;
	}

	if (!pBest)
		return;

	if (bRedirect)
	{
		Vec3 vRestoreOrigin = pBest->GetAbsOrigin();
		pBest->SetAbsOrigin(vBestOrigin);
		if (!F::AimbotProjectile.AutoAirblast(pLocal, pWeapon, pCmd, pBest))
		{
			Vec3 vAngles = Math::CalcAngle(vEyePos, vBestOrigin);
			SDK::FixMovement(pCmd, vAngles);
			pCmd->viewangles = vAngles;
			G::PSilentAngles = true;
		}
		pBest->SetAbsOrigin(vRestoreOrigin);
	}

	G::Attacking = true;
	pCmd->buttons |= IN_ATTACK2;
}