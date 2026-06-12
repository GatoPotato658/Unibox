#include "Aimbot.h"

#include "AimbotHitscan/AimbotHitscan.h"
#include "AimbotProjectile/AimbotProjectile.h"
#include "AimbotMelee/AimbotMelee.h"
#include "AutoDetonate/AutoDetonate.h"
#include "AutoAirblast/AutoAirblast.h"
#include "AutoHeal/AutoHeal.h"
#include "AutoRocketJump/AutoRocketJump.h"
#include "../Misc/Misc.h"
#include "../Visuals/Visuals.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"

bool CAimbot::ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pWeapon || !pLocal->CanAttack()
		|| (pWeapon->GetWeaponID() != TF_WEAPON_PASSTIME_GUN || !pLocal->m_bHasPasstimeBall()) && !SDK::AttribHookValue(1, "mult_dmg", pWeapon)
		|| pCmd->weaponselect)
		return false;

	return true;
}

float CAimbot::GetSmoothStrength(const Vec3& vCurAngle, const Vec3& vToAngle) const
{
	float flStrength = std::clamp(Vars::Aimbot::General::AssistStrength.Value / 100.f, 0.f, 1.f);
	if (!flStrength)
		return 0.f;

	float flAimFOV = std::max(Vars::Aimbot::General::AimFOV.Value, 1.f);
	float flFovRatio = std::clamp(Math::CalcFov(vCurAngle, vToAngle) / flAimFOV, 0.f, 1.f);
	float flCloseRatio = 1.f - flFovRatio;
	float flCurve = 1.f;

	switch (Vars::Aimbot::General::SmoothCurve.Value)
	{
	case Vars::Aimbot::General::SmoothCurveEnum::FastStart:
		flCurve = 1.f - flCloseRatio * flCloseRatio;
		break;
	case Vars::Aimbot::General::SmoothCurveEnum::FastEnd:
		flCurve = 1.f - flFovRatio * flFovRatio;
		break;
	case Vars::Aimbot::General::SmoothCurveEnum::SlowStart:
		flCurve = flCloseRatio * flCloseRatio;
		break;
	case Vars::Aimbot::General::SmoothCurveEnum::SlowEnd:
		flCurve = flFovRatio * flFovRatio;
		break;
	}

	const float flCurveAmount = std::clamp(Vars::Aimbot::General::SmoothCurveAmount.Value / 100.f, 0.f, 2.f);
	flCurve = 1.f - (1.f - flCurve) * flCurveAmount;

	float flVelocityScale = 1.f;
	if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::SmoothVelocity && G::AimTarget.m_iEntIndex > 0)
	{
		if (auto pTarget = I::ClientEntityList->GetClientEntity(G::AimTarget.m_iEntIndex)->As<CBaseEntity>())
		{
			const float flSpeed = pTarget->GetAbsVelocity().Length2D();
			const float flSpeedRatio = std::clamp(flSpeed / 320.f, 0.f, 1.75f);
			flVelocityScale = std::clamp(0.65f + flSpeedRatio * 0.4f, 0.35f, 1.35f);
		}
	}

	return std::clamp(flStrength * std::clamp(flCurve, 0.05f, 1.f) * flVelocityScale, 0.f, 1.f);
}

void CAimbot::RunAimbot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSecondaryType)
{
	m_bRunningSecondary = bSecondaryType;
	EWeaponType eWeaponType = !m_bRunningSecondary ? G::PrimaryWeaponType : G::SecondaryWeaponType;

	bool bOriginal;
	if (m_bRunningSecondary)
		bOriginal = G::CanPrimaryAttack, G::CanPrimaryAttack = G::CanSecondaryAttack;

	switch (eWeaponType)
	{
	case EWeaponType::HITSCAN: F::AimbotHitscan.Run(pLocal, pWeapon, pCmd); break;
	case EWeaponType::PROJECTILE: F::AimbotProjectile.Run(pLocal, pWeapon, pCmd); break;
	case EWeaponType::MELEE: F::AimbotMelee.Run(pLocal, pWeapon, pCmd); break;
	}

	if (m_bRunningSecondary)
		G::CanPrimaryAttack = bOriginal;
}

void CAimbot::RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (F::AimbotProjectile.m_iLastTickCancel)
	{
		pCmd->weaponselect = F::AimbotProjectile.m_iLastTickCancel;
		F::AimbotProjectile.m_iLastTickCancel = 0;
	}

	m_eRanType = EWeaponType::UNKNOWN;
	if (abs(G::AimTarget.m_iTickCount - I::GlobalVars->tickcount) > G::AimTarget.m_iDuration)
		G::AimTarget = {};
	if (abs(G::AimPoint.m_iTickCount - I::GlobalVars->tickcount) > G::AimPoint.m_iDuration)
		G::AimPoint = {};

	F::AutoRocketJump.Run(pLocal, pWeapon, pCmd);
	if (!ShouldRun(pLocal, pWeapon, pCmd))
		return;

	F::AutoDetonate.Run(pLocal, pCmd);
	F::AutoAirblast.Run(pLocal, pWeapon, pCmd);
	F::AutoHeal.Run(pLocal, pWeapon, pCmd);
	if (pWeapon->GetWeaponID() == TF_WEAPON_LASER_POINTER)
	{
		RunAimbot(pLocal, pWeapon, pCmd, true);
		if (F::AimbotProjectile.m_iSentryGunLock == 0)
			RunAimbot(pLocal, pWeapon, pCmd);
	}
	else
	{
		RunAimbot(pLocal, pWeapon, pCmd);
		RunAimbot(pLocal, pWeapon, pCmd, true);
	}

	auto iWeaponID = pWeapon->GetWeaponID();
	if (m_eRanType == EWeaponType::UNKNOWN && (!Vars::Aimbot::General::AimType.Value || (G::CanPrimaryAttack || G::CanSecondaryAttack) && (!(pCmd->buttons & IN_ATTACK) || (iWeaponID != TF_WEAPON_COMPOUND_BOW && iWeaponID != TF_WEAPON_PIPEBOMBLAUNCHER && iWeaponID != TF_WEAPON_CANNON))) || pWeapon->GetWeaponID() == TF_WEAPON_GRAPPLINGHOOK)
		F::AimbotProjectile.RunGrapplingHook(pLocal, pWeapon, pCmd);
}

void CAimbot::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	Store(false);

	G::AimbotSteering = false;

	RunMain(pLocal, pWeapon, pCmd);

	if ((G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true)) == 1 
		&& m_eRanType == EWeaponType::UNKNOWN 
		&& !F::AntiCheatCompatibility.Active() 
		&& pLocal->IsAlive() && !pLocal->IsAGhost() && !pLocal->InCond(TF_COND_HALLOWEEN_KART))
	{
		if (G::PrimaryWeaponType == EWeaponType::HITSCAN)
			F::AimbotHitscan.BacktrackToCrosshair(pCmd);
		else if (G::PrimaryWeaponType == EWeaponType::MELEE)
			F::AimbotMelee.BacktrackToCrosshair(pLocal, pCmd);
	}
}

void CAimbot::Draw(CTFPlayer* pLocal)
{
	if (!Vars::Aimbot::General::FOVCircle.Value || !Vars::Colors::FOVCircle.Value.a || !pLocal->CanAttack(false))
		return;

	auto pWeapon = H::Entities.GetWeapon();
	if (pWeapon && !SDK::AttribHookValue(1, "mult_dmg", pWeapon))
		return;

	if (Vars::Aimbot::General::AimFOV.Value >= 90.f)
		return;

	float flRadius = tanf(Math::Deg2Rad(Vars::Aimbot::General::AimFOV.Value)) / tanf(Math::Deg2Rad(G::FOV) / 2) * float(H::Draw.m_nScreenW) * (4.f / 6.f) / (16.f / 9.f);
	H::Draw.LineCircle(H::Draw.m_nScreenW / 2, H::Draw.m_nScreenH / 2, flRadius, 68, Vars::Colors::FOVCircle.Value);
}

void CAimbot::Store(CBaseEntity* pEntity, size_t iSize)
{
	if (!Vars::Visuals::Prediction::RealPath.Value)
		return;

	if (!pEntity->IsPlayer())
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	int iUserID = pResource->m_iUserID(pEntity->entindex());
	float flDuration = Vars::Visuals::Prediction::PlayerDrawDuration.Value ? Vars::Visuals::Prediction::PlayerDrawDuration.Value : 5.f;
	m_mRealPaths[iUserID] = {
		{ { pEntity->m_vecOrigin() }, I::GlobalVars->curtime + flDuration, Color_t(), Vars::Visuals::Prediction::RealPath.Value },
		iSize
	};
}

void CAimbot::Store(bool bFrameStageNotify)
{
	if (!Vars::Visuals::Prediction::RealPath.Value)
		return;

	int iLag = 1;
	if (bFrameStageNotify)
	{
		static int iStaticTickcout = I::GlobalVars->tickcount;
		iLag = I::GlobalVars->tickcount - iStaticTickcout;
		iStaticTickcout = I::GlobalVars->tickcount;
	}
	
	int iLocalIndex = I::EngineClient->GetLocalPlayer();
	for (auto& [iUserID, tPath] : m_mRealPaths)
	{
		if (tPath.m_tPath.m_vPath.size() >= tPath.m_iSize || tPath.m_tPath.m_flTime < I::GlobalVars->curtime)
		{
			if (tPath.m_tPath.m_tColor = Vars::Colors::RealPath.Value, tPath.m_tPath.m_bZBuffer = true; tPath.m_tPath.m_tColor.a)
				G::PathStorage.push_back(tPath.m_tPath);
			if (tPath.m_tPath.m_tColor = Vars::Colors::RealPathIgnoreZ.Value, tPath.m_tPath.m_bZBuffer = false; tPath.m_tPath.m_tColor.a)
				G::PathStorage.push_back(tPath.m_tPath);
			m_mRealPaths.erase(iUserID);
			continue;
		}

		int iIndex = I::EngineClient->GetPlayerForUserID(iUserID);
		if (bFrameStageNotify ? iIndex == iLocalIndex : iIndex != iLocalIndex)
			continue;

		auto pPlayer = I::ClientEntityList->GetClientEntity(iIndex)->As<CTFPlayer>();
		if (!pPlayer)
			continue;

		for (int i = 0; i < iLag; i++)
			tPath.m_tPath.m_vPath.push_back(pPlayer->m_vecOrigin());
	}
}