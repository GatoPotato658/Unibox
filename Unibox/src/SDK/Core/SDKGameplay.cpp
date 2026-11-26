#include "../SDK.h"

#include "../../Features/EnginePrediction/EnginePrediction.h"
#include "../../Features/Ticks/Ticks.h"
#include "../../Features/Aimbot/AimbotProjectile/ProjProfiler.h"

#pragma warning (disable : 6385)

float SDK::GetGravity()
{
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	return sv_gravity->GetFloat();
}

bool SDK::FriendlyFire()
{
	static auto mp_friendlyfire = H::ConVars.FindVar("mp_friendlyfire");
	return mp_friendlyfire->GetBool();
}

bool SDK::IsLoopback()
{
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	return pNetChan && pNetChan->IsLoopback();
}

int SDK::GetRoundState()
{
	if (auto pGameRules = I::TFGameRules())
		return pGameRules->m_iRoundState();
	return 0;
}
int SDK::GetWinningTeam()
{
	if (auto pGameRules = I::TFGameRules())
		return pGameRules->m_iWinningTeam();
	return 0;
}

const char* SDK::GetClassByIndex(const int nClass, bool bLower)
{
	static const char* szClassesUpper[] = {
		"Unknown", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer"
	};
	static const char* szClassesLower[] = {
		"unknown", "scout", "sniper", "soldier", "demoman", "medic", "heavy", "pyro", "spy", "engineer"
	};

	if (!bLower)
		return nClass < 10 && nClass > 0 ? szClassesUpper[nClass] : szClassesUpper[0];
	else
		return nClass < 10 && nClass > 0 ? szClassesLower[nClass] : szClassesLower[0];
}

float SDK::AttribHookValue(float value, const char* name, void* econent, void* buffer, bool isGlobalConstString)
{
	return S::CAttributeManager_AttribHookValue_Float.Call<float>(value, name, econent, buffer, isGlobalConstString);
}

float SDK::MaxSpeed(CTFPlayer* pPlayer, bool bIncludeCrouch, bool bIgnoreSpecialAbility)
{
	static auto tf_whip_speed_increase = H::ConVars.FindVar("tf_whip_speed_increase");
	float flSpeed = pPlayer->CalculateMaxSpeed(bIgnoreSpecialAbility);

	if (flSpeed > 0.f && (pPlayer->InCond(TF_COND_SPEED_BOOST) || pPlayer->InCond(TF_COND_HALLOWEEN_SPEED_BOOST)))
		flSpeed += std::min(flSpeed * 0.4f, tf_whip_speed_increase ? tf_whip_speed_increase->GetFloat() : 105.f);
	if (bIncludeCrouch && pPlayer->IsDucking() && pPlayer->IsOnGround())
		flSpeed /= 3;

	return flSpeed;
}

EWeaponType SDK::GetWeaponType(CTFWeaponBase* pWeapon, EWeaponType* pSecondaryType)
{
	if (pSecondaryType)
		*pSecondaryType = EWeaponType::UNKNOWN;
	if (!pWeapon)
		return EWeaponType::UNKNOWN;

	if (pSecondaryType)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_BAT_WOOD:
		case TF_WEAPON_BAT_GIFTWRAP:
			if (pWeapon->HasPrimaryAmmoForShot())
				*pSecondaryType = EWeaponType::PROJECTILE;
			break;
		case TF_WEAPON_LASER_POINTER:
		{
			auto pOwnerEntity = pWeapon->m_hOwner().Get();
			auto pOwner = pOwnerEntity ? pOwnerEntity->As<CTFPlayer>() : nullptr;
			if (pOwner && pOwner->IsPlayer())
			{
				auto pSentryEntity = pOwner->GetObjectOfType(OBJ_SENTRYGUN);
				auto pSentryGun = pSentryEntity ? pSentryEntity->As<CObjectSentrygun>() : nullptr;
				if (pSentryGun && pSentryGun->m_bPlayerControlled() && !pSentryGun->IsDisabled() && pSentryGun->m_iUpgradeLevel() > 2 && pSentryGun->m_iAmmoRockets() != 0)
					*pSecondaryType = EWeaponType::PROJECTILE;
			}

			break;
		}
		case TF_WEAPON_MECHANICAL_ARM:
		{
			auto pOwnerEntity = pWeapon->m_hOwner().Get();
			auto pOwner = pOwnerEntity ? pOwnerEntity->As<CTFPlayer>() : nullptr;
			if (pOwner && pOwner->IsPlayer() && pOwner->m_iMetalCount() >= 65)
				*pSecondaryType = EWeaponType::PROJECTILE;
		}
		}
	}

	if (pWeapon->GetSlot() == EWeaponSlot::SLOT_MELEE || pWeapon->GetWeaponID() == TF_WEAPON_BUILDER)
		return EWeaponType::MELEE;

	switch (pWeapon->m_iItemDefinitionIndex())
	{
	case Soldier_s_TheBuffBanner:
	case Soldier_s_FestiveBuffBanner:
	case Soldier_s_TheBattalionsBackup:
	case Soldier_s_TheConcheror:
	case Scout_s_BonkAtomicPunch:
	case Scout_s_CritaCola:
		return EWeaponType::UNKNOWN;
	}

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_PDA_SPY_BUILD:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_ROCKETPACK:
		return EWeaponType::UNKNOWN;
	case TF_WEAPON_CLEAVER:
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_RAYGUN:
	case TF_WEAPON_FLAMETHROWER:
	case TF_WEAPON_FLAME_BALL:
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	case TF_WEAPON_DRG_POMSON:
	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_SYRINGEGUN_MEDIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_PASSTIME_GUN:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_GRAPPLINGHOOK:
		return EWeaponType::PROJECTILE;
	}

	return EWeaponType::HITSCAN;
}

static int UpdateThrowTick(CTFWeaponBase* pWeapon, const CUserCmd* pCmd, bool bStart, float flDelay)
{
	static CBaseHandle hWeapon;
	static int iLastCommand = -1;
	static int iThrowTick = -5;
	const int iCommand = pCmd->command_number;
	if (hWeapon != pWeapon->GetRefEHandle() || iCommand < iLastCommand || iCommand > iLastCommand + 1)
		iThrowTick = -5;
	else if (iCommand != iLastCommand)
		iThrowTick = std::max(iThrowTick - 1, -5);
	hWeapon = pWeapon->GetRefEHandle();
	iLastCommand = iCommand;

	if (bStart && iThrowTick == -5)
		iThrowTick = std::max(TIME_TO_TICKS(flDelay), 1) + 1;
	return iThrowTick;
}

int SDK::IsAttacking(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const CUserCmd* pCmd, bool bTickBase)
{
	if (!pLocal || !pWeapon || pCmd->weaponselect)
		return false;

	int iTickBase = bTickBase ? I::GlobalVars->tickcount : pLocal->m_nTickBase();
	float flTickBase = bTickBase ? I::GlobalVars->curtime : TICKS_TO_TIME(iTickBase);

	if (pWeapon->GetSlot() == SLOT_MELEE)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_KNIFE:
			return G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK;
		case TF_WEAPON_BAT_WOOD:
		case TF_WEAPON_BAT_GIFTWRAP:
		{
			static auto tf_scout_bat_launch_delay = H::ConVars.FindVar("tf_scout_bat_launch_delay");
			const int iThrowTick = UpdateThrowTick(pWeapon, pCmd,
				G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK2 && pWeapon->HasPrimaryAmmoForShot(),
				tf_scout_bat_launch_delay ? tf_scout_bat_launch_delay->GetFloat() : 0.1f);
			if (iThrowTick > -5)
				G::Throwing = G::CanSecondaryAttack = true;
			if (iThrowTick > 1)
				G::Throwing = 2;
			if (iThrowTick == 1)
				return true;
		}
		}

		return TIME_TO_TICKS(pWeapon->m_flSmackTime()) == iTickBase - 1;
	}

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_STICKY_BALL_LAUNCHER:
	case TF_WEAPON_GRENADE_STICKY_BALL:
	{
		float flCharge = pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? flTickBase - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
		const float flAmount = Math::RemapVal(flCharge, 0.f, SDK::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 0.f, 1.f);
		return !(pCmd->buttons & IN_ATTACK) && flAmount > 0.f || flAmount == 1.f;
	}
	case TF_WEAPON_CANNON:
	{
		float flMortar = SDK::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
		if (!flMortar)
			return G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK ? 1 : G::Reloading && pCmd->buttons & IN_ATTACK ? 2 : 0;

		float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f ? flMortar - (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - flTickBase) : 0.f;
		const float flAmount = Math::RemapVal(flCharge, 0.f, SDK::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon), 0.f, 1.f);
		return !(pCmd->buttons & IN_ATTACK) && flAmount > 0.f || flAmount == 1.f;
	}
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		return !(pCmd->buttons & IN_ATTACK) && pWeapon->As<CTFSniperRifle>()->m_flChargedDamage() > 0.f;
	case TF_WEAPON_COMPOUND_BOW:
		return !(pCmd->buttons & IN_ATTACK) && pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f;
	case TF_WEAPON_PARTICLE_CANNON:
		if (float flChargeBeginTime = pWeapon->As<CTFParticleCannon>()->m_flChargeBeginTime(); flChargeBeginTime > 0)
		{
			float flTotalChargeTime = flTickBase - flChargeBeginTime;
			if (flTotalChargeTime >= TF_PARTICLE_MAX_CHARGE_TIME)
				return 1;
		}
		break;
	case TF_WEAPON_CLEAVER: // we can randomly use attack2 to fire
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_JAR_GAS:
	{
		const int iAttack = pWeapon->GetWeaponID() == TF_WEAPON_CLEAVER ? IN_ATTACK | IN_ATTACK2 : IN_ATTACK;
		const int iThrowTick = UpdateThrowTick(pWeapon, pCmd,
			G::CanPrimaryAttack && pCmd->buttons & iAttack && pWeapon->HasPrimaryAmmoForShot(), 0.1f);
		if (iThrowTick > -5)
			G::Throwing = G::CanPrimaryAttack = true;
		if (iThrowTick > 1)
			G::Throwing = 2;
		return iThrowTick == 1;
	}
	case TF_WEAPON_PASSTIME_GUN:
	{
		static bool bWasCharging = false;
		const bool bCharging = pCmd->buttons & IN_ATTACK;
		if (bCharging)
			G::Throwing = G::CanPrimaryAttack = true;

		const bool bThrown = bWasCharging && !bCharging;
		bWasCharging = bCharging;
		return bThrown;
	}
	case TF_WEAPON_GRAPPLINGHOOK:
	{
		if (!G::CanPrimaryAttack || !(pCmd->buttons & IN_ATTACK) || pWeapon->As<CTFGrapplingHook>()->m_hProjectile())
			return false;

		static auto tf_grapplinghook_max_distance = H::ConVars.FindVar("tf_grapplinghook_max_distance");
		const float flGrappleDistance = tf_grapplinghook_max_distance->GetFloat();

		Vec3 vPos, vAngle; GetProjectileFireSetup(pLocal, pCmd->viewangles, { 23.5f, -8.f, -3.f }, vPos, vAngle, 2000.f);
		Vec3 vForward; Math::AngleVectors(vAngle, &vForward);

		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnly filter = {};
		Trace(vPos, vPos + vForward * flGrappleDistance, MASK_SOLID, &filter, &trace);
		return trace.DidHit() && !(trace.surface.flags & SURF_SKY);
	}
	case TF_WEAPON_MINIGUN:
		switch (pWeapon->As<CTFMinigun>()->m_iWeaponState())
		{
		case AC_STATE_FIRING:
		case AC_STATE_SPINNING:
			if (pWeapon->HasPrimaryAmmoForShot())
				return G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK ? 1 : G::Reloading && pCmd->buttons & IN_ATTACK ? 2 : 0;
		}
		return false;
	case TF_WEAPON_LUNCHBOX:
		if (G::PrimaryWeaponType == EWeaponType::PROJECTILE && G::CanSecondaryAttack && pWeapon->HasPrimaryAmmoForShot() && pCmd->buttons & IN_ATTACK2)
			return 1;
		return false;
	case TF_WEAPON_FLAMETHROWER:
		if (!SDK::AttribHookValue(0, "set_charged_airblast", pWeapon)
			? G::CanSecondaryAttack && pCmd->buttons & IN_ATTACK2
			: G::CanSecondaryAttack && G::LastUserCmd && G::LastUserCmd->buttons & IN_ATTACK2 && !(pCmd->buttons & IN_ATTACK2))
			return 1;
		break;
	case TF_WEAPON_FLAME_BALL:
		if (SDK::AttribHookValue(0, "set_charged_airblast", pWeapon))
			return false;
		else if (G::CanSecondaryAttack && pCmd->buttons & IN_ATTACK2)
			return 1;
		break;
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MECHANICAL_ARM:
		if (G::CanSecondaryAttack && pCmd->buttons & IN_ATTACK2)
			return 1;
		break;
	case TF_WEAPON_MEDIGUN:
		if (pWeapon->As<CWeaponMedigun>()->m_bHealing())
			return false;
		break;
	}

	switch (pWeapon->m_iItemDefinitionIndex())
	{
	case Soldier_m_TheBeggarsBazooka:
	{
		static bool bLoading = false, bFiring = false;

		bool bAmmo = pWeapon->HasPrimaryAmmoForShot();
		if (!bAmmo)
			bLoading = false,
			bFiring = false;
		else if (!bFiring)
			bLoading = true;

		if ((bFiring || bLoading && !(pCmd->buttons & IN_ATTACK)) && bAmmo)
		{
			bFiring = true;
			bLoading = false;
			return G::CanPrimaryAttack ? 1 : G::Reloading ? 2 : 0;
		}

		return false;
	}
	}

	return G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK ? 1 : G::Reloading && pCmd->buttons & IN_ATTACK ? 2 : 0;
}

void SDK::FixMovement(CUserCmd* pCmd, const Vec3& vCurAngle, const Vec3& vTargetAngle)
{
	bool bCurOOB = fabsf(Math::NormalizeAngle(vCurAngle.x)) > 90.f;
	bool bTargetOOB = fabsf(Math::NormalizeAngle(vTargetAngle.x)) > 90.f;

	Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove * (bCurOOB ? -1 : 1), pCmd->upmove };
	float flSpeed = vMove.Length2D();
	Vec3 vMoveAng = Math::VectorAngles(vMove);

	float flCurYaw = vCurAngle.y + (bCurOOB ? 180.f : 0.f);
	float flTargetYaw = vTargetAngle.y + (bTargetOOB ? 180.f : 0.f);
	float flYaw = Math::Deg2Rad(flTargetYaw - flCurYaw + vMoveAng.y);

	pCmd->forwardmove = cos(flYaw) * flSpeed;
	pCmd->sidemove = sin(flYaw) * flSpeed * (bTargetOOB ? -1 : 1);
}

void SDK::FixMovement(CUserCmd* pCmd, const Vec3& vTargetAngle)
{
	FixMovement(pCmd, pCmd->viewangles, vTargetAngle);
}

Vec3 SDK::ComputeMove(const CUserCmd* pCmd, CTFPlayer* pLocal, const Vec3& vFrom, const Vec3& vTo)
{
	const Vec3 vDiff = vTo - vFrom;
	if (!vDiff.Length2D())
		return {};

	Vec3 vSilent = vDiff.To2D();
	Vec3 vAngle = Math::VectorAngles(vSilent);
	const float flYaw = Math::Deg2Rad(vAngle.y - pCmd->viewangles.y);
	const float flPitch = Math::Deg2Rad(vAngle.x - pCmd->viewangles.x);

	Vec3 vMove = { cos(flYaw) * 450.f, -sin(flYaw) * 450.f, -cos(flPitch) * 450.f };
	if (!(I::EngineTrace->GetPointContents(pLocal->GetShootPos()) & CONTENTS_WATER)) // only apply upmove in water
		vMove.z = pCmd->upmove;

	return vMove;
}

void SDK::WalkTo(CUserCmd* pCmd, CTFPlayer* pLocal, const Vec3& vFrom, const Vec3& vTo, float flScale)
{
	const auto vResult = ComputeMove(pCmd, pLocal, vFrom, vTo);

	pCmd->forwardmove = vResult.x * flScale;
	pCmd->sidemove = vResult.y * flScale;
	pCmd->upmove = vResult.z * flScale;
}

void SDK::WalkTo(CUserCmd* pCmd, CTFPlayer* pLocal, Vec3 vTo, float flScale)
{
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	WalkTo(pCmd, pLocal, vLocalPos, vTo, flScale);
}

void SDK::GetProjectileFireSetup(CTFPlayer* pPlayer, const Vec3& vAngIn, Vec3 vOffset, Vec3& vPosOut, Vec3& vAngOut, float flForward, float flCutoff, bool bInterp, bool bAllowFlip, FireSetup_t* pSetup)
{
	if (bAllowFlip)
	{
		bool bFlipped = pPlayer->m_bFlipViewModels();
		if (auto pWeapon = pPlayer->m_hActiveWeapon()->As<CBaseCombatWeapon>())
		{
			bFlipped = pWeapon->m_bFlipViewModel() != bFlipped;
			if (auto pTFWeapon = pWeapon->As<CTFWeaponBase>())
			{
				const int iID = pTFWeapon->GetWeaponID();
				if (iID == TF_WEAPON_CROSSBOW || iID == TF_WEAPON_SHOTGUN_BUILDING_RESCUE)
					bFlipped = !bFlipped;
			}
		}
		if (bFlipped)
			vOffset.y *= -1.f;
	}

	const FireSetup_t tSetup = { bInterp ? pPlayer->GetEyePosition() : pPlayer->GetShootPos(), vOffset, flForward, flCutoff, 0.f, true };
	ApplyProjectileFireSetup(pPlayer, tSetup, vAngIn, vPosOut, vAngOut);
	if (pSetup)
		*pSetup = tSetup;
}

void SDK::ApplyProjectileFireSetup(CTFPlayer* pPlayer, const FireSetup_t& tSetup, const Vec3& vAngIn, Vec3& vPosOut, Vec3& vAngOut)
{
	const Vec3& vShootPos = tSetup.m_vShootPos;

	Vec3 vForward, vRight, vUp; Math::AngleVectors(vAngIn, &vForward, &vRight, &vUp);
	vPosOut = vShootPos + (vForward * tSetup.m_vOffset.x) + (vRight * tSetup.m_vOffset.y) + (vUp * tSetup.m_vOffset.z);

	if (tSetup.m_flForward)
	{
		Vec3 vEndPos = vShootPos + vForward * tSetup.m_flForward;

		if (tSetup.m_flCutoff < 1.f)
		{
			CGameTrace trace = {};
			CTraceFilterCollideable filter(pPlayer);
			filter.m_iType = SKIP_CHECK;
			Trace(vShootPos, vEndPos, MASK_SOLID, &filter, &trace);
			if (trace.DidHit() && trace.fraction > tSetup.m_flCutoff)
				vEndPos = trace.endpos;
		}

		vAngOut = Math::VectorAngles(vEndPos - vPosOut);
	}
	else
		vAngOut = vAngIn;

	vPosOut.z += tSetup.m_flHeight;
}

//Pasted from somewhere in the valves tf2 server code
float SDK::CalculateSplashRadiusDamageFalloff(CTFWeaponBase* pWeapon, CTFPlayer* pAttacker, CTFWeaponBaseGrenadeProj* pProjectile, float flRadius)
{
	float flFalloff{ 0.0f };
	const int dmgType = pProjectile->GetDamageType();

	if (dmgType & DMG_RADIUS_MAX)
		flFalloff = 0.0f;
	else if (dmgType & DMG_HALF_FALLOFF)
		flFalloff = 0.5f;
	else if (flRadius)
		flFalloff = pProjectile->m_flDamage() / flRadius;
	else
		flFalloff = 1.0f;

	if (pWeapon)
	{
		float flFalloffMod = 1.f;
		AttribHookValue(flFalloffMod, "mult_dmg_falloff", pWeapon);
		if (flFalloffMod != 1.f)
			flFalloff += flFalloffMod;
	}

	if (pAttacker && pAttacker->InCond(TF_COND_RUNE_PRECISION))
		flFalloff = 1.0f;
	return flFalloff;
}

float SDK::CalculateSplashRadiusDamage(CTFWeaponBase* pWeapon, CTFPlayer* pAttacker, CTFWeaponBaseGrenadeProj* pProjectile, float flRadius, float flDist, float& flDamageNoBuffs, bool bSelf)
{
	float flFalloff{ CalculateSplashRadiusDamageFalloff(pWeapon, pAttacker, pProjectile, flRadius) };
	float flDamage{ Math::RemapVal(flDist, 0.f, flRadius, pProjectile->m_flDamage(), pProjectile->m_flDamage() * flFalloff) };
	flDamageNoBuffs = flDamage;

	bool bCrit{ (pProjectile->GetDamageType() & DMG_CRITICAL) > 0 };
	if (bCrit || pAttacker->IsMiniCritBoosted())
	{
		float flDamageBonus{ 0.f };
		if (bCrit)
			flDamageBonus = (TF_DAMAGE_CRIT_MULTIPLIER - 1.f) * flDamage;
		else
			flDamageBonus = (TF_DAMAGE_MINICRIT_MULTIPLIER - 1.f) * flDamage;

		flDamage += flDamageBonus;
	}

	// Grenades & Pipebombs do less damage to ourselves.
	if (bSelf && pWeapon)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_PIPEBOMBLAUNCHER:
		case TF_WEAPON_GRENADELAUNCHER:
		case TF_WEAPON_CANNON:
		case TF_WEAPON_STICKBOMB:
			flDamage *= 0.75f;
			flDamageNoBuffs *= 0.75f;
			break;
		}
	}
	return flDamage;
}

bool SDK::WeaponDoesNotUseAmmo(int iWeaponID, int iDefIdx, bool bIncludeInfiniteAmmo)
{
	switch (iDefIdx)
	{
	case Soldier_s_TheBuffBanner:
	case Soldier_s_FestiveBuffBanner:
	case Soldier_s_TheBattalionsBackup:
	case Soldier_s_TheConcheror:
	case Demoman_s_TheTideTurner:
	case Demoman_s_TheCharginTarge:
	case Demoman_s_TheSplendidScreen:
	case Demoman_s_FestiveTarge:
	case Demoman_m_TheBootlegger:
	case Demoman_m_AliBabasWeeBooties:
	case Engi_s_TheWrangler:
	case Engi_s_FestiveWrangler:
	case Sniper_s_CozyCamper:
	case Sniper_s_DarwinsDangerShield:
	case Sniper_s_TheRazorback:
	case Pyro_s_ThermalThruster: return true;
	default:
	{
		switch (iWeaponID)
		{
		case TF_WEAPON_PARTICLE_CANNON:
		case TF_WEAPON_RAYGUN:
		case TF_WEAPON_DRG_POMSON: return bIncludeInfiniteAmmo;
		case TF_WEAPON_FLAREGUN_REVENGE:
		case TF_WEAPON_MEDIGUN:
		case TF_WEAPON_PDA:
		case TF_WEAPON_PDA_ENGINEER_BUILD:
		case TF_WEAPON_PDA_ENGINEER_DESTROY:
		case TF_WEAPON_PDA_SPY:
		case TF_WEAPON_PDA_SPY_BUILD:
		case TF_WEAPON_BUILDER:
		case TF_WEAPON_INVIS:
		case TF_WEAPON_LUNCHBOX:
		case TF_WEAPON_THROWABLE:
		case TF_WEAPON_JAR:
		case TF_WEAPON_JAR_GAS:
		case TF_WEAPON_JAR_MILK:
		case TF_WEAPON_GRAPPLINGHOOK: return true;
		default: return false;
		}
		break;
	}
	}
}

bool SDK::WeaponDoesNotUseAmmo(CTFWeaponBase* pWeapon, bool bIncludeInfiniteAmmo)
{
	return WeaponDoesNotUseAmmo(pWeapon->GetWeaponID(), pWeapon->m_iItemDefinitionIndex(), bIncludeInfiniteAmmo);
}

bool SDK::IsResistedByVaccinatorUber(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon)
{
	if (!pPlayer || !pWeapon)
		return false;

	const int iDamageType = pWeapon->GetDamageType();
	if (iDamageType & (DMG_BULLET | DMG_BUCKSHOT) && pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST)
		&& AttribHookValue(0, "mod_pierce_resists_absorbs", pWeapon) == 0)
		return true;
	if (iDamageType & DMG_BLAST && pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
		return true;
	return iDamageType & (DMG_BURN | DMG_IGNITE) && pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST);
}

void SDK::CanAttack(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const CUserCmd* pCmd, bool& bPrimary, bool& bSecondary, bool& bReloading)
{
	bPrimary = bSecondary = bReloading = false;

	if (pWeapon->GetMaxClip1() != WEAPON_NOCLIP && !pWeapon->m_bReloadsSingly())
	{	// dumb fix
		float flOldCurtime = I::GlobalVars->curtime;
		I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());
		pWeapon->CheckReload();
		I::GlobalVars->curtime = flOldCurtime;
	}

	bool bFoundSlot[SLOT_PDA2 + 1] = {};
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		auto pWeaponInSlot = pLocal->GetWeaponFromSlot(i);
		if (!pWeaponInSlot)
			continue;

		int iDefIndex = pWeaponInSlot->m_iItemDefinitionIndex(), iWeaponID = pWeaponInSlot->GetWeaponID();
		int iActualWeaponSlot = pWeaponInSlot->GetSlot();
		if (iActualWeaponSlot < SLOT_PRIMARY || iActualWeaponSlot > SLOT_PDA2)
			continue;

		bFoundSlot[iActualWeaponSlot] = true;
		G::SavedWepSlots[iActualWeaponSlot] = iActualWeaponSlot;
		G::SavedDefIndexes[iActualWeaponSlot] = iDefIndex;
		G::SavedWepIds[iActualWeaponSlot] = iWeaponID;

		if (iActualWeaponSlot < SLOT_MELEE)
		{
			G::AmmoInSlot[iActualWeaponSlot].m_iClip = pWeaponInSlot->IsEnergyWeapon() ? static_cast<int>(pWeaponInSlot->m_flEnergy()) : pWeaponInSlot->m_iClip1();
			G::AmmoInSlot[iActualWeaponSlot].m_iReserve = pLocal->GetAmmoCount(pWeaponInSlot->m_iPrimaryAmmoType());
			G::AmmoInSlot[iActualWeaponSlot].m_iMaxClip = pWeaponInSlot->m_pWeaponInfo() ? pWeaponInSlot->GetMaxClip1() : 0;
			G::AmmoInSlot[iActualWeaponSlot].m_iMaxReserve = pWeaponInSlot->m_iPrimaryAmmoType() > 0 ? pLocal->GetMaxAmmo(pWeaponInSlot->m_iPrimaryAmmoType()) : 0;
			G::AmmoInSlot[iActualWeaponSlot].m_bUsesAmmo = !SDK::WeaponDoesNotUseAmmo(iWeaponID, iDefIndex, false);
		}

		if (iActualWeaponSlot <= SLOT_MELEE)
			G::HasWeaponForSlot[iActualWeaponSlot] = true;
	}

	for (int i = 0; i <= SLOT_PDA2; i++)
	{
		if (bFoundSlot[i])
			continue;

		G::SavedDefIndexes[i] = -1;
		G::SavedWepIds[i] = -1;
		G::SavedWepSlots[i] = -1;
		if (i < SLOT_MELEE)
			G::AmmoInSlot[i] = WeaponAmmoInfo_t();
		if (i <= SLOT_MELEE)
			G::HasWeaponForSlot[i] = false;
	}

	bool bCanAttack = pLocal->CanAttack();
	{
		static int iStaticItemDefinitionIndex = 0;
		int iOldItemDefinitionIndex = iStaticItemDefinitionIndex;
		int iNewItemDefinitionIndex = iStaticItemDefinitionIndex = pWeapon->m_iItemDefinitionIndex();

		if (iNewItemDefinitionIndex != iOldItemDefinitionIndex || !bCanAttack || !pWeapon->m_iClip1())
			F::Ticks.m_iWait = -1;
	}
	if (bCanAttack)
	{
		bPrimary = pWeapon->CanPrimaryAttack();
		bSecondary = pWeapon->CanSecondaryAttack();

		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_FLAME_BALL:
			if (bPrimary)
			{
				// do this, otherwise it will be a tick behind
				float flFrametime = TICK_INTERVAL * 100;
				float flMeterMult = S::IHasGenericMeter_GetMeterMultiplier.Call<float>(pWeapon->m_pMeter());
				float flRate = SDK::AttribHookValue(1.f, "item_meter_charge_rate", pWeapon) - 1;
				float flMult = SDK::AttribHookValue(1.f, "mult_item_meter_charge_rate", pWeapon);
				if (flRate <= 0.f || flMult <= 0.f)
					break;

				float flTankPressure = pLocal->m_flTankPressure() + flFrametime * flMeterMult / (flRate * flMult);

				if (bPrimary && flTankPressure < 100.f)
					bPrimary = bSecondary = false;
			}
			break;
		case TF_WEAPON_MINIGUN:
			if (int iState = pWeapon->As<CTFMinigun>()->m_iWeaponState(); iState != AC_STATE_FIRING && iState != AC_STATE_SPINNING || !pWeapon->HasPrimaryAmmoForShot())
				bPrimary = false;
			break;
		case TF_WEAPON_FLAREGUN_REVENGE:
			if (pCmd->buttons & IN_ATTACK2)
				bPrimary = false;
			break;
		case TF_WEAPON_BAT_WOOD:
		case TF_WEAPON_BAT_GIFTWRAP:
			if (!pWeapon->HasPrimaryAmmoForShot())
				bSecondary = false;
			break;
		case TF_WEAPON_MEDIGUN:
		case TF_WEAPON_BUILDER:
			break;
		case TF_WEAPON_LASER_POINTER:
		{
			auto pSentryEntity = pLocal->GetObjectOfType(OBJ_SENTRYGUN);
			auto pSentry = pSentryEntity ? pSentryEntity->As<CObjectSentrygun>() : nullptr;
			if (!pSentry || !pSentry->m_bPlayerControlled() || pSentry->IsDisabled())
			{
				bPrimary = bSecondary = false;
				break;
			}
			if (G::WranglerSecondFireTime + 2.25f < I::GlobalVars->curtime)
			{
				int iLocalTeam = pLocal->m_iTeamNum();
				Vec3 vSentryPos = pSentry->GetAbsOrigin();
				for (auto pRocket : H::Entities.GetGroup(EntityEnum::WorldProjectile))
				{
					if (pRocket->m_iTeamNum() == iLocalTeam &&
						pRocket->m_hOwnerEntity().Get() == pSentry &&
						pRocket->GetAbsOrigin().DistTo(vSentryPos) <= 1000.f)
					{
						G::WranglerSecondFireTime = I::GlobalVars->curtime;
						break;
					}
				}
			}
			if (pSentry->m_iAmmoShells() <= 0) bPrimary = false;
			if (pSentry->m_iUpgradeLevel() <= 2 || pSentry->m_iAmmoRockets() <= 0)
			{
				bSecondary = false;
				G::WranglerSecondFireTime = 0.f;
			}
			else bSecondary = I::GlobalVars->curtime - G::WranglerSecondFireTime > 2.25f;

			break;
		}
		case TF_WEAPON_PARTICLE_CANNON:
			if (float flChargeBeginTime = pWeapon->As<CTFParticleCannon>()->m_flChargeBeginTime(); flChargeBeginTime > 0)
			{
				float flTotalChargeTime = TICKS_TO_TIME(pLocal->m_nTickBase()) - flChargeBeginTime;
				if (flTotalChargeTime < TF_PARTICLE_MAX_CHARGE_TIME)
				{
					bPrimary = bSecondary = false;
					break;
				}
			}
			[[fallthrough]];
		default:
			if (pWeapon->GetSlot() != SLOT_MELEE)
			{
				bool bAmmo = pWeapon->HasPrimaryAmmoForShot();
				bool bReload = pWeapon->IsInReload();
				if (!bAmmo && pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheBeggarsBazooka)
					bPrimary = bSecondary = false;
				if (bReload && bAmmo && !bPrimary)
					bReloading = true;
			}
		}
		if (bPrimary)
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_FLAMETHROWER:
			case TF_WEAPON_FLAME_BALL:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
				if (pLocal->IsUnderwater())
					bPrimary = bSecondary = false;
			}
		}
	}
}
