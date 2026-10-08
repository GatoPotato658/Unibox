#include "AutoDetonate.h"

enum
{
	SHIELD_NONE = 0,
	SHIELD_NORMAL,	// 33% damage taken
	SHIELD_MAX,		// 10% damage taken, no inactive period
};

#define SHIELD_NORMAL_VALUE		0.33f
#define SHIELD_MAX_VALUE		0.10f

void CAutoDetonate::PredictPlayer(CBaseEntity* pLocal, CBaseEntity* pTarget, float flLatency)
{
	m_vRestore = std::nullopt;
	if (flLatency <= 0.f)
		return;

	m_vRestore = pTarget->GetAbsOrigin();

	pTarget->SetAbsOrigin(SDK::PredictOrigin(pTarget->m_vecOrigin(), pTarget->GetAbsVelocity(), flLatency, true, pTarget->m_vecMins() + 0.125f, pTarget->m_vecMaxs() - 0.125f, pTarget->SolidMask()));
}

void CAutoDetonate::RestorePlayer(CBaseEntity* pTarget)
{
	if (m_vRestore)
	{
		pTarget->SetAbsOrigin(m_vRestore.value());
		m_vRestore = std::nullopt;
	}
}

void CAutoDetonate::ApplyDamageDebuffs(CBaseEntity* pTarget, float& flDamage, float flDamageNoBuffs)
{
	float damageDebuff{ 0.f };
	float extraDamage{ 0.f };
	const auto iClassId{ pTarget->GetClassID() };
	if (iClassId == ETFClassID::CTFPlayer)
	{
		bool useCritBuffs{ true };
		CTFPlayer* pPlayer = pTarget->As<CTFPlayer>();
		if (pPlayer->InCond(TF_COND_DEFENSEBUFF))
		{
			useCritBuffs = false;
			flDamage = flDamageNoBuffs;
		}
		if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST))
		{
			useCritBuffs = false;
			flDamage = flDamageNoBuffs;
			damageDebuff += flDamage * 0.10f;
		}
		else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
		{
			useCritBuffs = false;
			flDamage = flDamageNoBuffs;
			damageDebuff += flDamage * 0.75f;
		}

		if (pPlayer->IsMarked() && useCritBuffs)
			extraDamage += (TF_DAMAGE_MINICRIT_MULTIPLIER - 1.f) * flDamage;

		auto pActiveWeapon{ pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>() };
		if (auto pMelee = pPlayer->GetWeaponFromSlot(SLOT_MELEE))
		{
			if (pMelee->m_iItemDefinitionIndex() == Scout_t_TheCandyCane) // No way someone is using this trash
				extraDamage += flDamage * 0.25f;
			else if (pActiveWeapon && pActiveWeapon == pMelee)
			{
				if (pMelee->m_iItemDefinitionIndex() == Heavy_t_WarriorsSpirit)
					extraDamage += flDamage * 0.3f;
				else if (pMelee->m_iItemDefinitionIndex() == Heavy_t_FistsofSteel)
					damageDebuff += flDamage * 0.4f;
			}
		}

		if (pActiveWeapon && (pActiveWeapon->m_iItemDefinitionIndex() == Heavy_m_TheBrassBeast || pActiveWeapon->m_iItemDefinitionIndex() == Heavy_m_Natascha) &&
			pActiveWeapon->As<CTFMinigun>()->m_iWeaponState() != AC_STATE_IDLE && (pTarget->As<CTFPlayer>()->m_iHealth() < (pTarget->As<CTFPlayer>()->GetMaxHealth() / 2)))
			damageDebuff += flDamage * 0.2f;

		if (pPlayer->InCond(TF_COND_DEFENSEBUFF_HIGH))
			damageDebuff += flDamage * 0.75f;
		else if (pPlayer->InCond(TF_COND_DEFENSEBUFF) || pPlayer->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK))
			damageDebuff += flDamage * 0.35f;
	}
	else
	{
		flDamage = flDamageNoBuffs;
		if (iClassId == ETFClassID::CObjectSentrygun)
		{
			auto pSentry = pTarget->As<CObjectSentrygun>();
			if (pSentry->m_nShieldLevel() && !pSentry->m_bHasSapper() && !pSentry->m_bPlasmaDisable())
				damageDebuff += flDamage * (pSentry->m_nShieldLevel() == SHIELD_NORMAL) ? 1.f - SHIELD_NORMAL_VALUE : 1.f - SHIELD_MAX_VALUE;
		}
	}
	flDamage += extraDamage - damageDebuff;
}

float CAutoDetonate::GetTotalDamageForTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget, std::unordered_set<CTFGrenadePipebombProjectile*> vStickies, std::unordered_map<int, Vec3> vPredictedStickyOrigins, std::unordered_map<int, float> vRadiuses, float& flDamageNoBuffs, bool bUseDist)
{
	float flTotalDamage{ 0.f };
	Vec3 vPredictedTargetOrigin;

	if (pTarget)
		vPredictedTargetOrigin = pTarget->GetAbsOrigin();

	for (auto pSticky : vStickies)
	{
		float flHalfDist{ (pLocal->m_vecMins().z + pLocal->m_vecMaxs().z) * 0.5f };
		float flDist{ flHalfDist };
		Vec3 hitPos;

		if (bUseDist && pTarget)
		{
			const bool bCheckPredicted{ Vars::Aimbot::Projectile::AutodetAccountPing.Value ? CanSee(pTarget, pSticky, vPredictedStickyOrigins[pSticky->entindex()], vRadiuses[pSticky->entindex()], &hitPos) : true };
			RestorePlayer(pTarget);
			if (!bCheckPredicted || !CanSee(pTarget, pSticky, pSticky->m_vecOrigin(), vRadiuses[pSticky->entindex()], &hitPos))
				continue;

			pTarget->SetAbsOrigin(vPredictedTargetOrigin);

			flDist = pTarget->GetClassID() == ETFClassID::CTFPlayer ? (vPredictedStickyOrigins[pSticky->entindex()] - pTarget->GetAbsOrigin()).Length() : (vPredictedStickyOrigins[pSticky->entindex()] - hitPos).Length();
			flHalfDist = (pTarget->m_vecMins().z + pTarget->m_vecMaxs().z) * 0.5f;
		}

		float flDamageNoBuffs{ 0.f };
		const float flDamage = SDK::CalculateSplashRadiusDamage(pWeapon, pLocal, pSticky, vRadiuses[pSticky->entindex()], flDist, flDamageNoBuffs);
		flTotalDamage += flDamage;
		flDamageNoBuffs += flDamageNoBuffs;
	}
	return flTotalDamage;
}

float CAutoDetonate::GetTotalDamageOfStickies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, std::vector<CTFGrenadePipebombProjectile*> vStickies, std::unordered_map<int, float> vRadiuses, float& flDamageNoBuffs)
{
	float flTotalDamage{ 0.f };
	if (!vStickies.empty())
	{
		bool bInsecureTrap{ vStickies.size() < 2 };
		if (!bInsecureTrap)
		{
			Vector centerPoint{};
			for (auto pSticky : vStickies)
			{
				const Vector origin{ pSticky->m_vecOrigin() };
				centerPoint += origin;

			}
			centerPoint /= vStickies.size();

			for (auto pSticky : vStickies)
			{
				const float flDist{ pSticky->m_vecOrigin().DistTo(centerPoint) };
				if (flDist < vRadiuses[pSticky->entindex()])
				{
					float flDamageNoBuffs{ 0.f };
					const float flDamage = SDK::CalculateSplashRadiusDamage(pWeapon, pLocal, pSticky, vRadiuses[pSticky->entindex()], flDist, flDamageNoBuffs);
					flTotalDamage += flDamage;
					flDamageNoBuffs += flDamageNoBuffs;
				}
			}
			bInsecureTrap = flTotalDamage < 100.f;

		}
		if (bInsecureTrap)
		{
			const auto pSticky = vStickies.front();
			const float flDist{ vRadiuses[pSticky->entindex()] / 4 };
			float flDamageNoBuffs{ 0.f };
			const float flDamage = SDK::CalculateSplashRadiusDamage(pWeapon, pLocal, pSticky, vRadiuses[pSticky->entindex()], flDist, flDamageNoBuffs);
			flTotalDamage = flDamage;
			flDamageNoBuffs = flDamageNoBuffs;
		}
	}
	return flTotalDamage;
}

bool CAutoDetonate::CanKill(CBaseEntity* pTarget, float& flDamage, float flDamageNoBuffs, float& flMaxDamage, float flMaxDamageNoBuffs, int iMaxHealth)
{
	const auto iClassId{ pTarget->GetClassID() };
	const float flCurrHealth{ static_cast<float>(iClassId == ETFClassID::CTFPlayer ? pTarget->As<CTFPlayer>()->m_iHealth() : pTarget->As<CBaseObject>()->m_iHealth()) };

	ApplyDamageDebuffs(pTarget, flDamage, flDamageNoBuffs);
	ApplyDamageDebuffs(pTarget, flMaxDamage, flMaxDamageNoBuffs);

	// There is no way we are killing the target, still detonate
	if (flCurrHealth == iMaxHealth && iMaxHealth > flMaxDamage)
		return true;

	if (flCurrHealth <= flDamage)
		return true;

	return false;
}

bool CAutoDetonate::CanSee(CBaseEntity* pTarget, CBaseEntity* pProjectile, const Vec3 vProjectileOrigin, const float flRadius, Vec3* vOut, Vec3* vCustomTargetPos) const
{
	Vec3 vTargetCenter{ vCustomTargetPos ? *vCustomTargetPos : pTarget->GetClassID() == ETFClassID::CTFPlayer ? pTarget->GetAbsOrigin() + pTarget->As<CTFPlayer>()->m_vecViewOffset() : pTarget->GetCenter() };

	Vec3 vNearestPos{};
	if (!vCustomTargetPos)
		reinterpret_cast<CCollisionProperty*>(pTarget->GetCollideable())->CalcNearestPoint(vProjectileOrigin, &vNearestPos);

	if (vOut)
		*vOut = vNearestPos;

	if (vProjectileOrigin.DistTo(vTargetCenter) > flRadius)
	{
		if (vCustomTargetPos)
			return false;

		vTargetCenter = vNearestPos;
		if (vProjectileOrigin.DistTo(vNearestPos) > flRadius)
			return false;
	}

	return SDK::VisPosCollideable(pProjectile, pTarget, vProjectileOrigin, vTargetCenter, MASK_SOLID & ~CONTENTS_GRATE);
}

bool CAutoDetonate::SkipTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget)
{
	if (pTarget->m_iTeamNum() != pLocal->m_iTeamNum())
	{
		switch (pTarget->GetClassID())
		{
		case ETFClassID::CTFPlayer:
			return !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players) || F::AimbotGlobal.ShouldIgnore(pTarget->As<CTFPlayer>(), pLocal, pWeapon);
		case ETFClassID::CObjectSentrygun:
			return !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Sentry);
		case ETFClassID::CObjectDispenser:
			return !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Dispenser);
		case ETFClassID::CObjectTeleporter:
			return !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Teleporter);
		case ETFClassID::CTFGrenadePipebombProjectile:
			return !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies) ||
				pWeapon->m_iItemDefinitionIndex() != Pyro_s_TheDetonator &&
				(!(pWeapon->m_iItemDefinitionIndex() == Demoman_s_TheQuickiebombLauncher || pWeapon->m_iItemDefinitionIndex() == Demoman_s_TheScottishResistance) ||
					!pTarget->As<CTFGrenadePipebombProjectile>()->HasStickyEffects());
		case ETFClassID::CEyeballBoss:
		case ETFClassID::CHeadlessHatman:
		case ETFClassID::CMerasmus:
		case ETFClassID::CTFBaseBoss:
		case ETFClassID::CTFTankBoss:
		case ETFClassID::CZombie:
			return !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs);
		case ETFClassID::CTFPumpkinBomb:
		case ETFClassID::CTFGenericBomb:
			return !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Bombs);
		default:
			break;
		}
	}

	return true;
}

bool CAutoDetonate::AimAtSticky(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pSticky)
{
	if (!pCmd || !pWeapon || !(Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::AimAtStickies))
		return true;

	const bool bLookDetonate = pWeapon->m_iItemDefinitionIndex() == Demoman_s_TheScottishResistance
		|| pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER && pWeapon->As<CTFPipebombLauncher>()->GetDetonateType() == TF_DETONATE_MODE_DOT;
	if (!bLookDetonate)
		return true;

	if (G::Attacking == 1 || I::ClientState->chokedcommands)
		return false;

	Vec3 vAngleTo = Math::CalcAngle(pLocal->GetShootPos(), pSticky->m_vecOrigin());
	SDK::FixMovement(pCmd, vAngleTo);
	pCmd->viewangles = vAngleTo;
	G::PSilentAngles = true;
	return true;
}

bool CAutoDetonate::FlareCheck(CTFPlayer* pLocal)
{
	auto& vProjectiles = H::Entities.GetGroup(EntityEnum::LocalFlares);
	if (vProjectiles.empty())
		return false;

	float flLatency = Vars::Aimbot::Projectile::AutodetAccountPing.Value ? std::max(F::Backtrack.GetReal() - 0.05f, 0.f) : -1.f;
	for (auto pProjectile : vProjectiles)
	{
		auto pWeapon = pProjectile->As<CTFProjectile_Flare>()->m_hLauncher()->As<CTFWeaponBase>();
		if (!pWeapon)
			continue;

		float flRadius = Vars::Aimbot::Projectile::AutodetRadius.Value * 110.f / 100;
		flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon) - 1;

		Vec3 vOrigin = SDK::PredictOrigin(pProjectile->m_vecOrigin(), pProjectile->GetAbsVelocity(), flLatency);

		CBaseEntity* pEntity;
		for (CEntitySphereQuery sphere(vOrigin, flRadius);
			(pEntity = sphere.GetCurrentEntity()) != nullptr;
			sphere.NextEntity())
		{
			if (pEntity == pLocal || pEntity->IsPlayer() && (!pEntity->As<CTFPlayer>()->IsAlive() || pEntity->As<CTFPlayer>()->IsAGhost()) || pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
				continue;

			if (SkipTarget(pLocal, pWeapon, pEntity))
				continue;

			PredictPlayer(pLocal, pEntity, flLatency);
			bool bCheckPredicted = !flLatency || CanSee(pEntity, pProjectile, vOrigin, flRadius);
			RestorePlayer(pEntity);
			if (bCheckPredicted && CanSee(pEntity, pProjectile, vOrigin, flRadius))
				return true;
		}
	}
	return false;
}

bool CAutoDetonate::StickyCheck(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	auto& vProjectiles = H::Entities.GetGroup(EntityEnum::LocalStickies);
	if (vProjectiles.empty())
		return false;

	float flLatency = Vars::Aimbot::Projectile::AutodetAccountPing.Value ? F::Backtrack.GetReal()/*std::max(F::Backtrack.GetReal() - 0.05f, 0.f)*/ : -1.f;

	static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
	float flLiveTimeMod = 1.f;
	if (pLocal->InCond(TF_COND_RUNE_HASTE))
		flLiveTimeMod *= 0.5f;
	else if (pLocal->InCond(TF_COND_RUNE_KING) || pLocal->InCond(TF_COND_KING_BUFFED))
		flLiveTimeMod *= 0.75f;

	std::vector<CTFGrenadePipebombProjectile*> vStickies{};
	std::unordered_map<int, float> vRadiuses;
	std::unordered_map<int, Vector> vPredictedStickyOrigins;

	CTFWeaponBase* pWeapon = nullptr;
	for (auto pProjectile : vProjectiles)
	{
		auto pPipebomb = pProjectile->As<CTFGrenadePipebombProjectile>();
		pWeapon = pWeapon = pProjectile->As<CTFGrenadePipebombProjectile>()->m_hOriginalLauncher()->As<CTFWeaponBase>();
		if (!pWeapon)
			continue;

		float flLiveTime{ SDK::AttribHookValue(tf_grenadelauncher_livetime->GetFloat(), "sticky_arm_time", pWeapon) * flLiveTimeMod };
		if (!pPipebomb->m_flCreationTime() || I::GlobalVars->curtime < pPipebomb->m_flCreationTime() + flLiveTime)
			continue;

		if (pPipebomb->m_iType() != TF_GL_MODE_REMOTE_DETONATE)
			continue;

		vStickies.push_back(pPipebomb);
		vRadiuses[pPipebomb->entindex()] = (pPipebomb->GetDamageRadius() * Vars::Aimbot::Projectile::AutodetRadius.Value / 100) - 1;
		vPredictedStickyOrigins[pPipebomb->entindex()] = SDK::PredictOrigin(pPipebomb->m_vecOrigin(), pPipebomb->GetAbsVelocity(), flLatency);
	}

	if (!pWeapon)
		return false;

	if (!(Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::MaxDamage))
	{
		for (auto pSticky : vStickies)
		{
			if (Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::PreventSelfDamage && CanSee(pLocal, pSticky, vPredictedStickyOrigins[pSticky->entindex()], vRadiuses[pSticky->entindex()]))
				return false;

			CBaseEntity* pEntity;
			for (CEntitySphereQuery sphere(vPredictedStickyOrigins[pSticky->entindex()], vRadiuses[pSticky->entindex()]);
				(pEntity = sphere.GetCurrentEntity()) != nullptr;
				sphere.NextEntity())
			{

				if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum() || pEntity->IsPlayer() && (!pEntity->As<CTFPlayer>()->IsAlive() || pEntity->As<CTFPlayer>()->IsAGhost()))
					continue;

				if (SkipTarget(pLocal, pWeapon, pEntity))
					continue;

				PredictPlayer(pLocal, pEntity, flLatency);
				bool bCheckPred = !flLatency || CanSee(pEntity, pSticky, vPredictedStickyOrigins[pSticky->entindex()], vRadiuses[pSticky->entindex()]);
				RestorePlayer(pEntity);
				if (bCheckPred && CanSee(pEntity, pSticky, vPredictedStickyOrigins[pSticky->entindex()], vRadiuses[pSticky->entindex()]))
					return AimAtSticky(pLocal, pWeapon, pCmd, pSticky);
			}
		}
	}
	else // I hate it
	{
		int iStickyTraps = 0;
		std::unordered_map<int, std::unordered_set<CTFGrenadePipebombProjectile*>> mStickyTraps;
		std::unordered_map<int, std::tuple<float, float, int>> mMaxDamageInfo;
		std::unordered_map<int, std::unordered_set<int>> mHitTargetIndexes;
		std::unordered_map<int, std::vector<CBaseEntity*>> mVictims;

		float flSafeLocalPlayerDamage = static_cast<float>(std::max(pLocal->GetMaxHealth(), pLocal->m_iHealth()) * 0.75f);
		//ApplyDamageDebuffs( pLocal, flSafeLocalPlayerDamage, flSafeLocalPlayerDamage );

		for (auto pSticky : vStickies)
		{
			if (!mStickyTraps.contains(iStickyTraps))
				mStickyTraps[iStickyTraps].insert(pSticky);

			int iMaxTargetHealth = -1;
			bool bIsPartOfATrap = false;
			std::vector<CBaseEntity*> vVictimsTemp;
			std::vector<CTFGrenadePipebombProjectile*> vHitStickiesTemp{ pSticky };
			bool bHitLocal = false;
			CBaseEntity* pEntity;
			for (CEntitySphereQuery sphere(vPredictedStickyOrigins[pSticky->entindex()], (vRadiuses[pSticky->entindex()] * 2.f) - 45.f);
				(pEntity = sphere.GetCurrentEntity()) != nullptr;
				sphere.NextEntity())
			{
				const auto iClassId = pEntity->GetClassID();
				if (iClassId != ETFClassID::CTFGrenadePipebombProjectile)
				{
					if (pEntity == pLocal && (Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::PreventSelfDamage))
						bHitLocal = true;
					else if (pEntity->m_iTeamNum() != pLocal->m_iTeamNum() && !SkipTarget(pLocal, pWeapon, pEntity))
					{
						PredictPlayer(pLocal, pEntity, flLatency);
						Vector vPredOrigin = pEntity->GetAbsOrigin();
						RestorePlayer(pEntity);
						if (vPredOrigin.DistTo(vPredictedStickyOrigins[pSticky->entindex()]) <= vRadiuses[pSticky->entindex()] &&
							pEntity->GetAbsOrigin().DistTo(pSticky->m_vecOrigin()) <= vRadiuses[pSticky->entindex()])
						{
							vVictimsTemp.push_back(pEntity);
							const int iCurrHealth{ iClassId == ETFClassID::CTFPlayer ? pEntity->As<CTFPlayer>()->m_iHealth() : (iClassId == ETFClassID::CObjectSentrygun || iClassId == ETFClassID::CObjectDispenser || iClassId == ETFClassID::CObjectTeleporter) ? pEntity->As<CBaseObject>()->m_iHealth() : -1 };
							iMaxTargetHealth = std::max(iCurrHealth, iMaxTargetHealth);
						}
					}
					continue;
				}

				auto pHitSticky = pEntity->As<CTFGrenadePipebombProjectile>();
				auto pOwner = pHitSticky->m_hThrower()->As<CTFPlayer>();
				if (pHitSticky != pSticky && pOwner == pLocal && vRadiuses.contains(pHitSticky->entindex()))
				{
					const float flDistToHitSticky{ pSticky->m_vecOrigin().DistTo(pHitSticky->m_vecOrigin()) };
					const float flDistToHitStickyPredicted{ vPredictedStickyOrigins[pSticky->entindex()].DistTo(vPredictedStickyOrigins[pHitSticky->entindex()]) };
					const float flMaxDistanceBetween{ vRadiuses[pSticky->entindex()] + vRadiuses[pHitSticky->entindex()] - 45.f };
					if (flDistToHitSticky > flMaxDistanceBetween || flDistToHitStickyPredicted > flMaxDistanceBetween)
						continue;

					vHitStickiesTemp.push_back(pHitSticky);

					if (!mStickyTraps[iStickyTraps].contains(pSticky) && !mStickyTraps[iStickyTraps].contains(pHitSticky))
					{
						iStickyTraps++;
						mStickyTraps[iStickyTraps].insert(pSticky);
					}

					for (auto hit_target : vVictimsTemp)
					{
						const bool bSetup{ mHitTargetIndexes.contains(iStickyTraps) };
						if (!bSetup || bSetup && !mHitTargetIndexes[iStickyTraps].contains(hit_target->entindex()))
						{
							mVictims[iStickyTraps].push_back(hit_target);
							mHitTargetIndexes[iStickyTraps].insert(hit_target->entindex());
						}
					}
					mStickyTraps[iStickyTraps].insert(pHitSticky);
					bIsPartOfATrap = true;
				}
			}

			if (!bIsPartOfATrap)
			{
				if (iStickyTraps)
					iStickyTraps++;

				for (auto pVictim : vVictimsTemp)
				{
					const bool bSetup{ mHitTargetIndexes.contains(iStickyTraps) };
					if (!bSetup || bSetup && !mHitTargetIndexes[iStickyTraps].contains(pVictim->entindex()))
					{
						mVictims[iStickyTraps].push_back(pVictim);
						mHitTargetIndexes[iStickyTraps].insert(pVictim->entindex());
					}
				}
				mStickyTraps[iStickyTraps].insert(pSticky);
			}

			float flCurrentMaxDamageNoBuff{ -1.f };
			const float flCurrMaxDamage{ GetTotalDamageOfStickies(pLocal, pWeapon, vHitStickiesTemp, vRadiuses, flCurrentMaxDamageNoBuff) };
			if (bHitLocal && flCurrMaxDamage >= flSafeLocalPlayerDamage) // this is stupid and mostly doesnt work
				return false;

			mMaxDamageInfo[iStickyTraps] = { flCurrMaxDamage, flCurrentMaxDamageNoBuff, iMaxTargetHealth };
		}

		for (int n = 0; n <= iStickyTraps; n++)
		{
			if (!mStickyTraps.contains(n) || !mVictims.contains(n))
				continue;

			const auto vTrapVictims = mVictims[n];
			const auto vTrapStickies = mStickyTraps[n];

			if (vTrapVictims.empty())
				continue;

			for (auto pVictim : vTrapVictims)
			{
				const auto iClassId = pVictim->GetClassID();
				if (iClassId == ETFClassID::CEyeballBoss ||
					iClassId == ETFClassID::CHeadlessHatman ||
					iClassId == ETFClassID::CMerasmus ||
					iClassId == ETFClassID::CTFBaseBoss ||
					iClassId == ETFClassID::CTFTankBoss ||
					iClassId == ETFClassID::CZombie) // Fuck these npcs
				{
					for (auto pSticky : vTrapStickies)
					{
						const bool bCheckPredicted{ flLatency ? CanSee(pVictim, pSticky, vPredictedStickyOrigins[pSticky->entindex()], vRadiuses[pSticky->entindex()]) : true };
						if (bCheckPredicted && CanSee(pVictim, pSticky, pSticky->m_vecOrigin(), vRadiuses[pSticky->entindex()]))
						{
							return AimAtSticky(pLocal, pWeapon, pCmd, pSticky);
						}
					}
					continue;
				}

				for (auto pSticky : vTrapStickies)
				{
					float flDamageNoBuff{ 0.f };
					PredictPlayer(pLocal, pVictim, flLatency);
					float flTotalDamage{ GetTotalDamageForTarget(pLocal, pWeapon, pVictim, vTrapStickies, vPredictedStickyOrigins, vRadiuses, flDamageNoBuff) };
					RestorePlayer(pVictim);
					if (flTotalDamage)
					{
						auto [flMaxDamage, flMaxDamageNoBuff, iMaxTargetHealth] = mMaxDamageInfo[n];
						if (CanKill(pVictim, flTotalDamage, flDamageNoBuff, flMaxDamage, flMaxDamageNoBuff, iMaxTargetHealth))
						{
							return AimAtSticky(pLocal, pWeapon, pCmd, pSticky);
						}
					}
				}
			}
		}
	}
	return false;
}

void CAutoDetonate::Run(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Aimbot::Projectile::AutoDetonate.Value)
		return;

	if ((Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::Stickies && StickyCheck(pLocal, pCmd))
		|| (Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::Flares && FlareCheck(pLocal)))
		pCmd->buttons |= IN_ATTACK2;
}