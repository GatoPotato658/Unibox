#include "BotUtils.h"
#include "NavEngine.h"
#include "Objectives.h"
#include "../Players/PlayerUtils.h"
#include "../Misc/Misc.h"
#include "../Aimbot/AutoHeal/AutoHeal.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../Aimbot/AimbotGlobal/AimbotGlobal.h"
#include "../Misc/NamedPipe/NamedPipe.h"
#include "../Ticks/Ticks.h"
#include "../Simulation/MovementSimulation/MovementSimulation.h"

static int GetRangedFallbackSlot(CTFPlayer* pLocal)
{
	auto HasUsableWeapon = [&](int iSlot)
		{
			if (!G::HasWeaponForSlot[iSlot]) return false;
			if (!G::AmmoInSlot[iSlot].m_bUsesAmmo) return true;
			return G::AmmoInSlot[iSlot].m_iClip > 0 || G::AmmoInSlot[iSlot].m_iReserve > 0;
		};

	if (HasUsableWeapon(SLOT_PRIMARY))			return SLOT_PRIMARY;
	if (HasUsableWeapon(SLOT_SECONDARY))		return SLOT_SECONDARY;
	if (G::HasWeaponForSlot[SLOT_PRIMARY])		return SLOT_PRIMARY;
	if (G::HasWeaponForSlot[SLOT_SECONDARY])	return SLOT_SECONDARY;
	return SLOT_MELEE;
}

static bool SlotUsesAmmo(int iSlot) { return G::AmmoInSlot[iSlot].m_bUsesAmmo; }
static bool SlotHasClip(int iSlot) { return G::AmmoInSlot[iSlot].m_iClip > 0; }
static bool SlotHasReserve(int iSlot) { return G::AmmoInSlot[iSlot].m_iReserve > 0; }
static bool SlotHasAnyAmmo(int iSlot) { return !SlotUsesAmmo(iSlot) || SlotHasClip(iSlot) || SlotHasReserve(iSlot); }
static bool SlotCanUseNow(int iSlot) { return !SlotUsesAmmo(iSlot) || SlotHasClip(iSlot); }

static bool IsTargetBehindLocal(CTFPlayer* pLocal, CTFPlayer* pTarget)
{
	if (!pLocal || !pTarget) return false;
	Vec3 vForward{};
	Math::AngleVectors(pTarget->GetEyeAngles(), &vForward);
	Vec3 vToLocal = pLocal->GetAbsOrigin() - pTarget->GetAbsOrigin();
	vToLocal.z = 0.f;
	vForward.z = 0.f;
	if (vToLocal.Normalize() <= 0.01f || vForward.Normalize() <= 0.01f) return false;
	return vForward.Dot(vToLocal) < -0.5f;
}

static int GetSniperTargetHealthTier(const ClosestEnemy_t& tClosestEnemy)
{
	if (!tClosestEnemy.m_pPlayer) return -1;
	const int iHealth = tClosestEnemy.m_pPlayer->m_iHealth();
	const int iMaxHealth = tClosestEnemy.m_pPlayer->GetMaxHealth();
	if (iHealth < iMaxHealth * 0.35f) return 2;
	return iHealth < iMaxHealth * 0.75f;
}

constexpr float kMeleeZLimit = 80.f;

static int SelectBestSlot(CTFPlayer* pLocal, const ClosestEnemy_t& tClosestEnemy, int iCurrentSlot, bool bHasMedigunTargets)
{
	if (!pLocal) return -1;

	const bool bHasEnemy = tClosestEnemy.m_pPlayer != nullptr;
	const float flEnemyDist = tClosestEnemy.m_flDist;
	const bool bMeleeReachable = bHasEnemy && std::abs(tClosestEnemy.m_flDistZ) <= kMeleeZLimit;

	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
	{
		const bool bPrimaryEmpty = !SlotHasClip(SLOT_PRIMARY);
		const bool bSecondaryLow = !SlotUsesAmmo(SLOT_SECONDARY) || !SlotHasClip(SLOT_SECONDARY) || G::AmmoInSlot[SLOT_SECONDARY].m_iReserve <= G::AmmoInSlot[SLOT_SECONDARY].m_iMaxReserve / 4;
		if (bMeleeReachable && bPrimaryEmpty && bSecondaryLow && flEnemyDist <= 200.f)
			return SLOT_MELEE;
		if (SlotUsesAmmo(SLOT_SECONDARY) && SlotHasClip(SLOT_SECONDARY) && (bPrimaryEmpty || flEnemyDist > 900.f))
			return SLOT_SECONDARY;
		if (SlotHasClip(SLOT_PRIMARY))
			return SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_HEAVY:
	{
		const bool bHolidayPunch = G::SavedDefIndexes[SLOT_MELEE] == Heavy_t_TheHolidayPunch;
		const bool bPunchTarget = bHolidayPunch && bHasEnemy && !tClosestEnemy.m_pPlayer->IsTaunting() && tClosestEnemy.m_pPlayer->IsInvulnerable() && flEnemyDist < 400.f;
		const bool bOutOfAmmo = !SlotHasClip(SLOT_PRIMARY) && !SlotHasClip(SLOT_SECONDARY) && !SlotHasReserve(SLOT_SECONDARY);
		if (bMeleeReachable && (bOutOfAmmo || bPunchTarget))
			return SLOT_MELEE;
		if (SlotHasClip(SLOT_PRIMARY))
			return SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_MEDIC:
	{
		auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
		if (!pSecondaryWeapon)
			return -1;

		if (pSecondaryWeapon->As<CWeaponMedigun>()->m_hHealingTarget() || bHasMedigunTargets)
			return SLOT_SECONDARY;
		if (bMeleeReachable && (!SlotHasClip(SLOT_PRIMARY) || flEnemyDist <= 400.f))
			return SLOT_MELEE;
		return SLOT_PRIMARY;
	}
	case TF_CLASS_SPY:
	{
		const bool bIsBehind = IsTargetBehindLocal(pLocal, tClosestEnemy.m_pPlayer);
		auto pKnife = pLocal->GetWeaponFromSlot(SLOT_MELEE);
		const bool bReadyToBackstab = pKnife && pKnife->GetWeaponID() == TF_WEAPON_KNIFE && pKnife->As<CTFKnife>()->m_bReadyToBackstab();
		const bool bKnifeLethal = pKnife && bHasEnemy && tClosestEnemy.m_pPlayer->m_iHealth() <= pKnife->GetDamage(false);
		if (bMeleeReachable && flEnemyDist <= 250.f && (bReadyToBackstab || bKnifeLethal))
			return SLOT_MELEE;
		if (bMeleeReachable && (pLocal->InCond(TF_COND_STEALTHED) || bIsBehind) && flEnemyDist <= 1000.f)
			return SLOT_MELEE;
		if (SlotHasClip(SLOT_PRIMARY) || SlotHasReserve(SLOT_PRIMARY))
			return SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SNIPER:
	{
		const int iTargetLowHp = GetSniperTargetHealthTier(tClosestEnemy);
		const bool bOutOfAmmo = !SlotHasReserve(SLOT_PRIMARY) && !SlotHasClip(SLOT_SECONDARY);
		if (bMeleeReachable && (bOutOfAmmo || flEnemyDist <= 200.f))
			return SLOT_MELEE;
		if (SlotUsesAmmo(SLOT_SECONDARY) && SlotCanUseNow(SLOT_SECONDARY) && bHasEnemy && flEnemyDist <= 240.f && (!SlotCanUseNow(SLOT_PRIMARY) || iTargetLowHp > 1))
			return SLOT_SECONDARY;
		if (iCurrentSlot >= SLOT_PRIMARY && iCurrentSlot < SLOT_MELEE && SlotUsesAmmo(iCurrentSlot) && SlotHasClip(iCurrentSlot) && bHasEnemy && flEnemyDist <= 800.f && iTargetLowHp > 1)
			return iCurrentSlot;
		if (SlotHasClip(SLOT_PRIMARY))
			return SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_PYRO:
	{
		const bool bSecondaryLow = !SlotHasClip(SLOT_SECONDARY) && SlotUsesAmmo(SLOT_SECONDARY) && G::AmmoInSlot[SLOT_SECONDARY].m_iReserve <= G::AmmoInSlot[SLOT_SECONDARY].m_iMaxReserve / 4;
		if (bMeleeReachable && !SlotHasClip(SLOT_PRIMARY) && bSecondaryLow && flEnemyDist <= 300.f)
			return SLOT_MELEE;
		if (SlotCanUseNow(SLOT_PRIMARY) && (!bHasEnemy || flEnemyDist <= 520.f))
			return SLOT_PRIMARY;
		if (SlotHasClip(SLOT_SECONDARY) && (!SlotCanUseNow(SLOT_PRIMARY) || (bHasEnemy && flEnemyDist > 650.f)))
			return SLOT_SECONDARY;
		if (SlotCanUseNow(SLOT_PRIMARY))
			return SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SOLDIER:
	{
		auto pEnemyWeaponEntity = bHasEnemy ? tClosestEnemy.m_pPlayer->m_hActiveWeapon().Get() : nullptr;
		auto pEnemyWeapon = pEnemyWeaponEntity ? pEnemyWeaponEntity->As<CTFWeaponBase>() : nullptr;
		const bool bEnemyCanAirblast = pEnemyWeapon && pEnemyWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER && pEnemyWeapon->m_iItemDefinitionIndex() != Pyro_m_ThePhlogistinator;
		const bool bEnemyClose = bHasEnemy && flEnemyDist <= 250.f;
		const bool bPrimaryEmpty = SlotUsesAmmo(SLOT_PRIMARY) && !SlotHasClip(SLOT_PRIMARY) && !SlotHasReserve(SLOT_PRIMARY);
		if (bMeleeReachable && (iCurrentSlot != SLOT_PRIMARY || bPrimaryEmpty) && bEnemyClose &&
			(tClosestEnemy.m_pPlayer->m_iHealth() < 80 ? !SlotHasClip(SLOT_SECONDARY) : tClosestEnemy.m_pPlayer->m_iHealth() >= 150 || G::AmmoInSlot[SLOT_SECONDARY].m_iClip < 2))
			return SLOT_MELEE;
		if (SlotHasClip(SLOT_SECONDARY) && (bPrimaryEmpty || bEnemyCanAirblast || (bHasEnemy && flEnemyDist <= 250.f && tClosestEnemy.m_pPlayer->m_iHealth() <= 90)))
			return SLOT_SECONDARY;
		if (SlotCanUseNow(SLOT_PRIMARY))
			return SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_DEMOMAN:
	{
		if (SlotCanUseNow(SLOT_PRIMARY))
			return SLOT_PRIMARY;
		if (bMeleeReachable && flEnemyDist <= 260.f)
			return SLOT_MELEE;
		if (G::HasWeaponForSlot[SLOT_PRIMARY])
			return SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_ENGINEER:
	{
		if (bMeleeReachable && SlotUsesAmmo(SLOT_PRIMARY) && !SlotHasClip(SLOT_PRIMARY) && !SlotHasClip(SLOT_SECONDARY) && flEnemyDist <= 200.f)
			return SLOT_MELEE;
		if (SlotCanUseNow(SLOT_PRIMARY) && (!bHasEnemy || flEnemyDist <= 1000.f))
			return SLOT_PRIMARY;
		if ((!SlotHasAnyAmmo(SLOT_PRIMARY) || (bHasEnemy && flEnemyDist > 1100.f)) && (SlotHasClip(SLOT_SECONDARY) || SlotHasReserve(SLOT_SECONDARY)))
			return SLOT_SECONDARY;
		if (SlotHasAnyAmmo(SLOT_PRIMARY))
			return SLOT_PRIMARY;
		break;
	}
	default:
		break;
	}

	return -1;
}

bool CBotUtils::HasMedigunTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!pLocal || !pWeapon || !Vars::Aimbot::Healing::AutoHeal.Value)
		return false;

	Vec3 vShootPos = F::Ticks.GetShootPos();
	float flRange = pWeapon->GetRange();
	int iLocalIdx = pLocal->entindex();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		if (pEntity->entindex() == iLocalIdx || pEntity->IsDormant() || vShootPos.DistTo(pEntity->GetCenter()) > flRange)
			continue;

		if (pEntity->As<CTFPlayer>()->InCond(TF_COND_STEALTHED) ||
			(Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly &&
			!H::Entities.IsFriend(pEntity->entindex()) && !H::Entities.InParty(pEntity->entindex())))
			continue;

		return true;
	}
	return false;
}

bool CBotUtils::ShouldAssist(CTFPlayer* pLocal, int iEntIdx)
{
	if (!pLocal)
		return false;

	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIdx);
	if (!pEntity || pEntity->As<CBaseEntity>()->m_iTeamNum() != pLocal->m_iTeamNum())
		return false;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpFriendlyCaptureObjectives))
		return true;

	if (F::PlayerUtils.IsIgnored(iEntIdx)
		|| H::Entities.InParty(iEntIdx)
		|| H::Entities.IsFriend(iEntIdx))
		return true;

	return false;
}

ShouldTargetEnum::ShouldTargetEnum CBotUtils::ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIdx)
{
	if (!pLocal)
		return ShouldTargetEnum::Invalid;

	auto pClientEntity = I::ClientEntityList->GetClientEntity(iEntIdx);
	auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
	if (!pEntity || !pEntity->IsPlayer())
		return ShouldTargetEnum::Invalid;

	if (!GetDormantOrigin(iEntIdx))
		return ShouldTargetEnum::DontTarget;

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (!pPlayer->IsAlive() || pPlayer == pLocal)
		return ShouldTargetEnum::Invalid;

#ifdef TEXTMODE
	if (auto pResource = H::Entities.GetResource(); pResource && F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iEntIdx)))
		return ShouldTargetEnum::DontTarget;
#endif

	if (F::PlayerUtils.IsIgnored(iEntIdx))
		return ShouldTargetEnum::DontTarget;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(iEntIdx)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(iEntIdx)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invulnerable && pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invisible
			&& !pPlayer->InCond(TF_COND_BURNING) && !pPlayer->InCond(TF_COND_BURNING_PYRO)
			&& !pPlayer->InCond(TF_COND_MAD_MILK) && !pPlayer->InCond(TF_COND_URINE)
			&& Vars::Aimbot::General::IgnoreInvisible.Value > 0.f
			&& pPlayer->GetEffectiveInvisibilityLevel() >= Vars::Aimbot::General::IgnoreInvisible.Value / 100.f
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::DeadRinger && pPlayer->m_bFeignDeathReady()
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Taunting && pPlayer->IsTaunting()
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Disguised && pPlayer->InCond(TF_COND_DISGUISED))
		return ShouldTargetEnum::DontTarget;

	if (pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
		return ShouldTargetEnum::DontTarget;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Vaccinator)
	{
		switch (SDK::GetWeaponType(pWeapon))
		{
		case EWeaponType::HITSCAN:
			if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && SDK::AttribHookValue(0, "mod_pierce_resists_absorbs", pWeapon) == 0)
				return ShouldTargetEnum::DontTarget;
			break;
		case EWeaponType::PROJECTILE:
			if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) && (G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_FLAMETHROWER && G::SavedWepIds[SLOT_SECONDARY] == TF_WEAPON_FLAREGUN))
				return ShouldTargetEnum::DontTarget;
			else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_COMPOUND_BOW)
				return ShouldTargetEnum::DontTarget;
			else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
				return ShouldTargetEnum::DontTarget;
		}
	}

	return ShouldTargetEnum::Target;
}

ShouldTargetEnum::ShouldTargetEnum CBotUtils::ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx)
{
	if (!pLocal || iEntIdx <= 0)
		return ShouldTargetEnum::DontTarget;

	auto pClientEntity = I::ClientEntityList->GetClientEntity(iEntIdx);
	auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
	if (!pEntity)
		return ShouldTargetEnum::Invalid;

	if (!pEntity->IsBuilding() || !GetDormantOrigin(iEntIdx))
		return ShouldTargetEnum::DontTarget;

	auto pBuilding = pEntity->As<CBaseObject>();
	if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Sentry) && pBuilding->IsSentrygun()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Dispenser) && pBuilding->IsDispenser()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Teleporter) && pBuilding->IsTeleporter())
		return ShouldTargetEnum::DontTarget;

	if (pLocal->m_iTeamNum() == pBuilding->m_iTeamNum())
		return ShouldTargetEnum::DontTarget;

	auto pOwner = pBuilding->m_hBuilder().Get();
	if (pOwner)
	{
		if (F::PlayerUtils.IsIgnored(pOwner->entindex()))
			return ShouldTargetEnum::DontTarget;

		if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(pOwner->entindex())
			|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(pOwner->entindex()))
			return ShouldTargetEnum::DontTarget;
	}

	return ShouldTargetEnum::Target;
}

bool CBotUtils::GetDormantOrigin(int iIndex, Vector* pOut)
{
	if (iIndex <= 0)
		return false;

	auto pClientEntity = I::ClientEntityList->GetClientEntity(iIndex);
	auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
	if (!pEntity ||
		(pEntity->IsPlayer() ? !pEntity->As<CBasePlayer>()->IsAlive() :
		(!pEntity->IsBuilding() || !pEntity->As<CBaseObject>()->m_iHealth())))
		return false;

	if (!pEntity->IsDormant() || H::Entities.GetDormancy(iIndex))
	{
		if (pOut)
			*pOut = pEntity->GetAbsOrigin();
		return true;
	}

	return false;
}

ClosestEnemy_t CBotUtils::UpdateCloseEnemies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	ClosestEnemy_t tClosestEnemy{};

	Vector vLocalOrigin = pLocal->GetAbsOrigin();
	const bool bVSHBoss = F::VSHController.IsBossLocal();
	float flBestScore = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iEntIndex = pPlayer->entindex();
		if (ShouldTarget(pLocal, pWeapon, iEntIndex) != ShouldTargetEnum::Target)
			continue;

		Vector vOrigin = pPlayer->GetAbsOrigin();
		const float flDistance = vLocalOrigin.DistTo(vOrigin);
		const float flScore = bVSHBoss ? F::VSHController.GetHuntScore(pPlayer, flDistance) : flDistance;
		if (flScore >= flBestScore)
			continue;

		flBestScore = flScore;

		tClosestEnemy.m_iEntIdx = iEntIndex;
		tClosestEnemy.m_pPlayer = pPlayer;
		tClosestEnemy.m_vOrigin = vOrigin;
		tClosestEnemy.m_flDist = flDistance;
		tClosestEnemy.m_flDistZ = vOrigin.z - vLocalOrigin.z;
	}

	if (bVSHBoss)
		F::VSHController.SetHuntTarget(tClosestEnemy.m_iEntIdx);

	return tClosestEnemy;
}

void CBotUtils::UpdateBestSlot(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Movement::BotUtils::WeaponSlot.Value || F::AutoHeal.m_iAutoSwitch != 0)
	{
		m_iBestSlot = -1;
		return;
	}

	if (Vars::Misc::Movement::BotUtils::WeaponSlot.Value != Vars::Misc::Movement::BotUtils::WeaponSlotEnum::Best)
	{
		m_iBestSlot = Vars::Misc::Movement::BotUtils::WeaponSlot.Value - 2;
		return;
	}

	if (F::ZIController.IsZombie())
	{
		m_iBestSlot = G::HasWeaponForSlot[SLOT_MELEE] ? SLOT_MELEE : -1;
		return;
	}

	if (F::VSHController.IsBossLocal())
	{
		m_iBestSlot = SLOT_MELEE;
		return;
	}

	if (F::MVMController.WantsPrimary(pLocal))
	{
		m_iBestSlot = SLOT_PRIMARY;
		return;
	}

	if (F::MVMController.WantsScoutSecondary(pLocal))
	{
		m_iBestSlot = SLOT_SECONDARY;
		return;
	}

	const bool bHasMedigunTargets = pLocal->m_iClass() == TF_CLASS_MEDIC &&
		HasMedigunTargets(pLocal, pLocal->GetWeaponFromSlot(SLOT_SECONDARY));
	m_iBestSlot = SelectBestSlot(pLocal, m_tClosestEnemy, m_iCurrentSlot, bHasMedigunTargets);

	if (m_iBestSlot == SLOT_MELEE && !G::HasWeaponForSlot[SLOT_MELEE])
		m_iBestSlot = GetRangedFallbackSlot(pLocal);

	if (m_iBestSlot == -1 && m_iCurrentSlot == SLOT_MELEE)
		m_iBestSlot = GetRangedFallbackSlot(pLocal);
}

void CBotUtils::SetSlot(CTFPlayer* pLocal, int iSlot)
{
	auto pWeaponEntity = pLocal ? pLocal->m_hActiveWeapon().Get() : nullptr;
	auto pWeapon = pWeaponEntity ? pWeaponEntity->As<CTFWeaponBase>() : nullptr;
	if (pWeapon && pWeapon->m_flSmackTime() > 0.f || I::GlobalVars->curtime <= m_tLegitAim.m_flCommitUntil)
		return;

	if (iSlot > -1 && F::AutoHeal.m_iAutoSwitch == 0)
	{
		auto sCommand = "slot" + std::to_string(iSlot + 1);
		if (m_iCurrentSlot != iSlot)
			I::EngineClient->ClientCmd_Unrestricted(sCommand.c_str());
	}
}

void CBotUtils::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || !pWeapon)
	{
		Reset();
		return;
	}

	if (!Vars::Misc::Movement::NavBot::Enabled.Value && !(Vars::Misc::Movement::FollowBot::Enabled.Value && Vars::Misc::Movement::FollowBot::Targets.Value))
	{
		m_mAutoScopeCache.clear();
		m_mAutoRevCache.clear();
		m_bScopeKeep = m_bScopeClearCache = m_bRevKeep = m_bRevClearCache = false;
		m_tClosestEnemy = {};
		m_iBestSlot = -1;
		m_iCurrentSlot = -1;
		m_eJumpState = STATE_AWAITING_JUMP;
		m_vPredictedJumpPos = {};
		m_vJumpPeakPos = {};
		return;
	}

	m_tClosestEnemy = UpdateCloseEnemies(pLocal, pWeapon);
	m_iCurrentSlot = pWeapon->GetSlot();
	UpdateBestSlot(pLocal);

	if (!F::NavEngine.IsNavMeshLoaded() || (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK))
	{
		m_mAutoScopeCache.clear();
		m_mAutoRevCache.clear();
		return;
	}

	AutoScope(pLocal, pWeapon, pCmd);
	AutoRev(pLocal, pWeapon, pCmd);
}

void CBotUtils::Reset()
{
	m_mAutoScopeCache.clear();
	m_mAutoRevCache.clear();
	m_bScopeKeep = m_bScopeClearCache = m_bRevKeep = m_bRevClearCache = false;
	m_tClosestEnemy = {};
	m_iBestSlot = -1;
	m_iCurrentSlot = -1;
	m_eJumpState = STATE_AWAITING_JUMP;
	m_vPredictedJumpPos = {};
	m_vJumpPeakPos = {};
	m_vDebugLook = {};
	m_flDebugLookExpire = 0.f;
	m_tLegitAim = {};
	m_tLLAP = {};
	m_vLastAngles = I::EngineClient ? I::EngineClient->GetViewAngles() : Vec3{};
}

bool CBotUtils::IsSurfaceWalkable(const Vector& vNormal)
{
	static const Vector vUp = { 0.f, 0.f, 1.f };
	float flAngle = Math::Rad2Deg(std::acos(vNormal.Dot(vUp)));
	return flAngle < 50.f;
}

bool CBotUtils::SmartJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || !Vars::Misc::Movement::NavEngine::Enabled.Value || !Vars::Misc::Movement::NavBot::SmartJump.Value) return false;
	if (NavRuntime::IsMinigunJumpLocked(H::Entities.GetWeapon(), pCmd))
		return false;

	if (pLocal->OnSolid())
	{
		Vector vVelocity = pLocal->m_vecVelocity();
		Vector vMoveInput = { pCmd->forwardmove, pCmd->sidemove, 0.f };
		if (vMoveInput.Length() > 0.f)
		{
			Vector vViewAngles = pCmd->viewangles;
			Vector vForward, vRight;
			Math::AngleVectors(vViewAngles, &vForward, &vRight, nullptr);
			vForward.z = vRight.z = 0.f;
			vForward.Normalize();
			vRight.Normalize();

			Vector vRotatedMoveDir = vForward * vMoveInput.x + vRight * vMoveInput.y;
			vVelocity = vRotatedMoveDir.Normalized() * std::max(10.f, vVelocity.Length());
		}

		const float flJumpForce = TFGame::JUMP_SPEED;
		const float flGravity = TFGame::GRAVITY;
		float flTimeToPeak = flJumpForce / flGravity;
		float flDistTravelled = vVelocity.Length2D() * flTimeToPeak;
		Vector vJumpDirection = vVelocity.Normalized();
		if (F::NavEngine.IsPathing())
		{
			Vector vPathDir = F::NavEngine.GetCurrentPathDir();
			if (!vPathDir.IsZero())
			{
				if (vJumpDirection.Dot(vPathDir) < 0.5f)
					return false;

				auto pCrumbs = F::NavEngine.GetCrumbs();
				if (pCrumbs->size() > 1)
				{
					Vector vNextDir = ((*pCrumbs)[1].m_vPos - (*pCrumbs)[0].m_vPos);
					vNextDir.z = 0.f;
					if (vNextDir.Normalize() > 0.1f && vPathDir.Dot(vNextDir) < 0.707f)
					{
						if (pLocal->GetAbsOrigin().DistTo((*pCrumbs)[0].m_vPos) < 100.f)
							return false;
					}
				}

				vJumpDirection = vPathDir;
			}
		}
		Vector vJumpPeakPos = pLocal->GetAbsOrigin() + vJumpDirection * flDistTravelled;
		m_vJumpPeakPos = vJumpPeakPos;

		const Vector vHullMin = { -23.99f, -23.99f, 0.f };
		const Vector vHullMax = { 23.99f, 23.99f, 62.f };
		const Vector vHullMinSjump = { -16.f, -16.f, 0.f };
		const Vector vHullMaxSjump = { 16.f, 16.f, 62.f };
		const Vector vStepHeight = { 0.f, 0.f, 18.f };
		const Vector vMaxJumpHeight = { 0.f, 0.f, 72.f };

		Vector vTraceStart = pLocal->GetAbsOrigin() + vStepHeight;
		Vector vTraceEnd = vTraceStart + vJumpDirection * flDistTravelled;

		CGameTrace forwardTrace = {};
		CTraceFilterNavigation filter(pLocal);
		filter.m_iPlayer = PLAYER_DEFAULT;
		SDK::TraceHull(vTraceStart, vTraceEnd, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID, &filter, &forwardTrace);

		m_vPredictedJumpPos = forwardTrace.endpos;

		if (forwardTrace.fraction < 1.0f && !IsSurfaceWalkable(forwardTrace.plane.normal))
		{
			CGameTrace downwardTrace = {};
			SDK::TraceHull(forwardTrace.endpos, forwardTrace.endpos - vMaxJumpHeight, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID_BRUSHONLY, &filter, &downwardTrace);

			Vector vLandingPos = downwardTrace.endpos + vJumpDirection * 10.f;
			CGameTrace landingTrace = {};
			SDK::TraceHull(vLandingPos + vMaxJumpHeight, vLandingPos, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &landingTrace);

			m_vPredictedJumpPos = landingTrace.endpos;

			if (landingTrace.fraction > 0.f && landingTrace.fraction < 0.75f)
			{
				if (IsSurfaceWalkable(landingTrace.plane.normal))
					return true;
			}
		}
	}
	else if (pCmd->buttons & IN_JUMP)
		return true;

	return false;
}

void CBotUtils::HandleSmartJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (F::NavEngine.IsUnstucking() || F::VSHController.IsMovementHeld())
		return;

	if (!pLocal || !pLocal->IsAlive() || !Vars::Misc::Movement::NavEngine::Enabled.Value
		|| !Vars::Misc::Movement::NavBot::SmartJump.Value || F::AutoRocketJump.IsRunning())
	{
		m_eJumpState = STATE_AWAITING_JUMP;
		return;
	}

	if (NavRuntime::IsMinigunJumpLocked(H::Entities.GetWeapon(), pCmd))
	{
		pCmd->buttons &= ~IN_JUMP;
		m_eJumpState = STATE_AWAITING_JUMP;
		return;
	}

	bool bOnGround = pLocal->OnSolid();
	bool bDucking = pLocal->IsDucking();

	if (bOnGround && bDucking)
		m_eJumpState = STATE_AWAITING_JUMP;

	switch (m_eJumpState)
	{
	case STATE_AWAITING_JUMP:
		if (SmartJump(pLocal, pCmd))
			m_eJumpState = (Vars::Misc::Movement::AutoCTap.Value && bOnGround) ? STATE_CTAP : STATE_JUMP;
		break;
	case STATE_CTAP:
		pCmd->buttons |= IN_DUCK;
		pCmd->buttons &= ~IN_JUMP;
		m_eJumpState = STATE_JUMP;
		break;
	case STATE_JUMP:
		pCmd->buttons &= ~IN_DUCK;
		pCmd->buttons |= IN_JUMP;
		m_eJumpState = STATE_ASCENDING;
		break;
	case STATE_ASCENDING:
		pCmd->buttons |= IN_DUCK;
		if (pLocal->m_vecVelocity().z <= 0.f)
			m_eJumpState = STATE_DESCENDING;
		else if (bOnGround)
			m_eJumpState = STATE_AWAITING_JUMP;
		break;
	case STATE_DESCENDING:
		pCmd->buttons &= ~IN_DUCK;
		if (!bOnGround)
		{
			if (SmartJump(pLocal, pCmd))
			{
				pCmd->buttons &= ~IN_DUCK;
				pCmd->buttons |= IN_JUMP;
				m_eJumpState = STATE_JUMP;
			}
		}
		else
		{
			pCmd->buttons |= IN_DUCK;
			m_eJumpState = STATE_AWAITING_JUMP;
		}
		break;
	}
}

static bool SmoothAimHasPriority()
{
	const auto iAimType = Vars::Aimbot::General::AimType.Value;
	if (iAimType == Vars::Aimbot::General::AimTypeEnum::LegitForBots)
		return F::BotUtils.IsLegitAimActive();
	if (iAimType != Vars::Aimbot::General::AimTypeEnum::Smooth
		&& iAimType != Vars::Aimbot::General::AimTypeEnum::SmoothVelocity
		&& iAimType != Vars::Aimbot::General::AimTypeEnum::Assistive)
		return false;
	return G::AimbotSteering;
}

static Vec3 GetFlatDirection(Vec3 vDirection, const Vec3& vFallback)
{
	vDirection.z = 0.f;
	if (vDirection.Length() > 24.f)
	{
		vDirection.Normalize();
		return vDirection;
	}

	Vec3 vFlatFallback = vFallback;
	vFlatFallback.z = 0.f;
	if (vFlatFallback.Length() > 0.01f)
	{
		vFlatFallback.Normalize();
		return vFlatFallback;
	}

	return { 1.f, 0.f, 0.f };
}

static bool IsPathTargetForward(const Vec3& vOrigin, const Vec3& vTarget, const Vec3& vPathDirection, float flMinDot)
{
	Vec3 vToTarget = vTarget - vOrigin;
	vToTarget.z = 0.f;
	if (vToTarget.Length() < 24.f)
		return true;
	vToTarget.Normalize();
	return vPathDirection.Dot(vToTarget) >= flMinDot;
}

static Vec3 MakePositionFromYaw(const Vec3& vOrigin, const Vec3& vDirection, float flYawOffset, float flDistance, float flHeight)
{
	Vec3 vAngles = Math::CalcAngle(Vec3(), vDirection);
	vAngles.y += flYawOffset;

	Vec3 vForward = {};
	Math::AngleVectors(vAngles, &vForward);
	vForward.z = 0.f;
	if (vForward.Normalize() <= 0.01f)
		vForward = vDirection;

	return vOrigin + vForward * flDistance + Vec3(0.f, 0.f, flHeight);
}

static bool TraceLegitLookPoint(CTFPlayer* pLocal, const Vec3& vEye, const Vec3& vTarget, float* pDistance = nullptr)
{
	CGameTrace tTrace = {};
	CTraceFilterWorldAndPropsOnly tFilter(pLocal);
	SDK::Trace(vEye, vTarget, MASK_SHOT | CONTENTS_GRATE, &tFilter, &tTrace);
	const bool bClear = !tTrace.DidHit();
	if (pDistance)
		*pDistance = bClear ? vEye.DistTo(vTarget) : vEye.DistTo(tTrace.endpos);
	return bClear;
}

static float LegitApproachAngle(float flGoal, float flCurrent, float flStep)
{
	float flDelta = flGoal - flCurrent;
	while (flDelta > 180.f)
		flDelta -= 360.f;
	while (flDelta < -180.f)
		flDelta += 360.f;
	if (std::fabs(flDelta) <= flStep)
		return flGoal;
	return flCurrent + (flDelta > 0.f ? flStep : -flStep);
}

float CBotUtils::GetLegitSkill()
{
	return std::clamp(Vars::Aimbot::General::LegitForBots::SkillLevel.Value / 100.f, 0.f, 1.f);
}

float CBotUtils::GetLegitTrackInterval()
{
	const float flSkill = GetLegitSkill();
	const float flRate = std::clamp(Vars::Aimbot::General::LegitForBots::CorrectionRate.Value, 0.2f, 1.f);
	return Math::Lerp(0.22f, 0.06f, flSkill) / flRate;
}

void CBotUtils::AdvanceLegitView(LegitMotion_t& tMotion, const Vec3& vGoal, float flDt, float flMaxRate, float flMinRate, float flPitchScale, float* pAnchorSince, float* pResettleUntil, float* pMousepad, bool bAllowFlick)
{
	if (!tMotion.m_bInitialized)
	{
		tMotion.m_vAngles = I::EngineClient->GetViewAngles();
		tMotion.m_vVelocity = {};
		tMotion.m_vGoalPrev = vGoal;
		tMotion.m_bInitialized = true;
	}
	if (flDt <= 0.f)
		return;

	const float flNow = I::GlobalVars->curtime;

	const float flSpeed = tMotion.m_vVelocity.Length();

	if (pAnchorSince)
	{
		if (flSpeed < 25.f)
		{
			if (*pAnchorSince <= 0.f)
				*pAnchorSince = flNow;
		}
		else
			*pAnchorSince = 0.f;
	}
	const bool bCalm = !pAnchorSince || (*pAnchorSince > 0.f && flNow - *pAnchorSince > 0.4f);

	if (pResettleUntil && pMousepad)
	{
		*pMousepad *= expf(-flDt * 0.3f);
		if (!bCalm && *pMousepad > 90.f && *pResettleUntil <= flNow)
		{
			*pResettleUntil = flNow + SDK::RandomFloat(0.22f, 0.38f);
			*pMousepad = 0.f;
			tMotion.m_flFlickStart += *pResettleUntil - flNow;
		}
		if (*pResettleUntil > flNow)
		{
			tMotion.m_vVelocity = tMotion.m_vVelocity.Lerp(Vec3{}, std::clamp(flDt * 14.f, 0.f, 1.f));
			Math::ClampAngles(tMotion.m_vAngles);
			return;
		}
	}

	const Vec3 vDelta = vGoal.DeltaAngle(tMotion.m_vAngles);
	const float flDist = vDelta.Length();

	if (bAllowFlick)
	{
		if (tMotion.m_flFlickDur <= 0.f && flDist > 18.f)
		{
			const float flFitts = std::clamp(0.14f + 0.13f * std::log2(flDist / 30.f + 1.f), 0.13f, 0.46f);
			const float flSpeedScale = 700.f / std::max(flMaxRate, 200.f);
			tMotion.m_flFlickDur = flFitts * flSpeedScale * SDK::RandomFloat(0.85f, 1.25f);
			tMotion.m_flFlickStart = flNow;
			tMotion.m_vFlickFrom = tMotion.m_vAngles;
			tMotion.m_vFlickTotal = vDelta;
		}
	}
	if (tMotion.m_flFlickDur > 0.f && flDist < 2.5f)
		tMotion.m_flFlickDur = 0.f;

	if (tMotion.m_flFlickDur > 0.f)
	{
		const float flT = std::clamp((flNow - tMotion.m_flFlickStart) / tMotion.m_flFlickDur, 0.f, 1.f);
		const Vec3 vDrift = vGoal.DeltaAngle(tMotion.m_vFlickFrom) - tMotion.m_vFlickTotal;
		tMotion.m_vFlickTotal += vDrift * std::clamp(flDt * 6.f, 0.f, 1.f);
		const Vec3 vTotal = tMotion.m_vFlickTotal;
		const float flU = flT * flT * flT * (flT * (flT * 6.f - 15.f) + 10.f);
		const float flUP = std::clamp(flU * 1.25f - 0.08f, 0.f, 1.f);

		Vec3 vNew;
		vNew.y = tMotion.m_vFlickFrom.y + vTotal.y * flU;
		vNew.x = tMotion.m_vFlickFrom.x + vTotal.x * flUP;
		vNew.z = 0.f;
		Math::ClampAngles(vNew);

		const Vec3 vMoved = vNew.DeltaAngle(tMotion.m_vAngles);
		tMotion.m_vVelocity = vMoved / flDt;
		tMotion.m_vAngles = vNew;
		tMotion.m_vGoalPrev = vGoal;
		if (pMousepad)
			*pMousepad += std::fabs(vMoved.y);
		if (flT >= 1.f)
			tMotion.m_flFlickDur = 0.f;
		return;
	}

	Vec3 vForward;
	Math::AngleVectors(tMotion.m_vAngles, &vForward);
	Vec3 vDesired;
	Math::AngleVectors(vGoal, &vDesired);
	const float flDot = std::clamp(vForward.Dot(vDesired), -1.f, 1.f);

	float flRate = std::min(flMaxRate * 0.35f, 240.f);
	if (flDot > 0.7f)
	{
		const float flT = std::clamp((flDot - 0.7f) / 0.3f, 0.f, 1.f);
		flRate *= std::max(sinf(1.5707963f * (1.f - flT)), 0.02f);
	}
	float flGoalRate = 0.f;
	if (!tMotion.m_vGoalPrev.IsZero())
		flGoalRate = tMotion.m_vGoalPrev.DeltaAngle(vGoal).Length() / flDt;
	tMotion.m_vGoalPrev = vGoal;
	flRate = std::max(flRate, std::max(flMinRate, std::min(flGoalRate * 1.05f, 170.f)));

	const Vec3 vPrev = tMotion.m_vAngles;
	tMotion.m_vAngles.x = LegitApproachAngle(vGoal.x, tMotion.m_vAngles.x, flRate * flPitchScale * flDt);
	tMotion.m_vAngles.y = LegitApproachAngle(vGoal.y, tMotion.m_vAngles.y, flRate * flDt);
	tMotion.m_vAngles.z = 0.f;
	Math::ClampAngles(tMotion.m_vAngles);

	const Vec3 vMoved = tMotion.m_vAngles.DeltaAngle(vPrev);
	tMotion.m_vVelocity = vMoved / flDt;
	if (pMousepad)
		*pMousepad += std::fabs(vMoved.y);
}

float CBotUtils::GetLegitReactionTime()
{
	const float flSkill = GetLegitSkill();
	const float flCvar = std::max(Vars::Aimbot::General::LegitForBots::ReactionTime.Value, 0.02f);
	const float flBase = Math::Lerp(0.30f, 0.13f, flSkill) * (flCvar / 0.16f);
	const float flVariation = std::clamp(Vars::Aimbot::General::LegitForBots::ReactionVariation.Value / 100.f, 0.f, 1.f);
	return flBase * SDK::RandomFloat(1.f - flVariation, 1.f + flVariation);
}

bool CBotUtils::IsLegitShotClear(CTFPlayer* pLocal, CBaseEntity* pTarget, const Vec3& vAngles)
{
	if (!pLocal || !Vars::Aimbot::General::LegitForBots::AvoidFriendlyFire.Value || !SDK::FriendlyFire())
		return true;

	auto pWeaponEntity = pLocal->m_hActiveWeapon().Get();
	auto pWeapon = pWeaponEntity ? pWeaponEntity->As<CTFWeaponBase>() : nullptr;
	Vec3 vShotAngles = vAngles;
	if (pWeapon && SDK::GetWeaponType(pWeapon) != EWeaponType::MELEE)
		vShotAngles += pLocal->m_vecPunchAngle();

	Vec3 vForward;
	Math::AngleVectors(vShotAngles, &vForward);
	const float flRange = pWeapon ? std::max(pWeapon->GetRange(), 256.f) : 8192.f;
	const Vec3 vStart = pLocal->GetShootPos();

	CGameTrace tTrace = {};
	CTraceFilterCollideable tFilter(pLocal);
	tFilter.m_iPlayer = PLAYER_ALL;
	tFilter.m_iObject = OBJECT_ALL;
	SDK::Trace(vStart, vStart + vForward * flRange, MASK_SHOT, &tFilter, &tTrace);
	if (!tTrace.DidHit() || !tTrace.m_pEnt || tTrace.m_pEnt == pTarget)
		return true;
	return tTrace.m_pEnt->m_iTeamNum() != pLocal->m_iTeamNum();
}

void CBotUtils::DoSlowAim(Vec3& vWishAngles, float flSpeed, Vec3 vPreviousAngles)
{
	const float flAimSpeed = std::clamp(flSpeed, 1.f, 120.f);
	const float flDeltaTime = std::max(I::GlobalVars->interval_per_tick, 0.001f);
	const float flBlend = std::clamp(1.f - expf(-flAimSpeed * flDeltaTime * 0.15f), 0.005f, 0.8f);
	const Vec3 vSlowDelta = vWishAngles.DeltaAngle(vPreviousAngles);
	vPreviousAngles += vSlowDelta * flBlend;
	vWishAngles = vPreviousAngles;
	Math::ClampAngles(vWishAngles);
}

void CBotUtils::LookAtPath(CUserCmd* pCmd, Vec2 vDest, Vec3 vLocalEyePos, bool bSilent)
{
	if (SmoothAimHasPriority())
	{
		m_vLastAngles = I::EngineClient->GetViewAngles();
		return;
	}

	Vec3 vWishAng{ vDest.x, vDest.y, vLocalEyePos.z };
	vWishAng = Math::CalcAngle(vLocalEyePos, vWishAng);

	DoSlowAim(vWishAng, static_cast<float>(Vars::Misc::Movement::BotUtils::LookAtPathSpeed.Value), m_vLastAngles);
	if (bSilent)
		pCmd->viewangles = vWishAng;
	else
		I::EngineClient->SetViewAngles(vWishAng);
	m_vLastAngles = vWishAng;
}

void CBotUtils::LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth)
{
	if (SmoothAimHasPriority())
	{
		m_vLastAngles = I::EngineClient->GetViewAngles();
		return;
	}

	if (bSmooth)
		DoSlowAim(vWishAngles, static_cast<float>(Vars::Misc::Movement::BotUtils::LookAtPathSpeed.Value), m_vLastAngles);

	if (bSilent)
		pCmd->viewangles = vWishAngles;
	else
		I::EngineClient->SetViewAngles(vWishAngles);
	m_vLastAngles = vWishAngles;
}

Vec3 CBotUtils::GetLegitActorPoint(CBaseEntity* pActor)
{
	if (!pActor)
		return {};
	if (!pActor->IsPlayer())
		return pActor->GetCenter();

	Vec3 vPoint = pActor->As<CTFPlayer>()->GetEyePosition();
	vPoint.z -= 8.f;
	const Vec3 vVelocity = pActor->GetAbsVelocity();
	const float flLead = std::clamp(vPoint.DistTo(H::Entities.GetLocal() ? H::Entities.GetLocal()->GetEyePosition() : vPoint) / 3200.f, 0.01f, 0.07f);
	return vPoint + vVelocity * flLead;
}

CBaseEntity* CBotUtils::FindLegitThreat(CTFPlayer* pLocal, const Vec3& vEye)
{
	if (!pLocal || !Vars::Aimbot::General::LegitForBots::ReactToThreats.Value)
		return nullptr;

	CBaseEntity* pBest = nullptr;
	float flBestTime = 0.85f;
	const Vec3 vVelocity = pLocal->m_vecVelocity();
	for (auto pProjectile : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (!pProjectile || pProjectile->IsDormant() || pProjectile->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;

		const Vec3 vOrigin = pProjectile->GetAbsOrigin();
		const Vec3 vRelativeOrigin = vOrigin - vEye;
		const Vec3 vRelativeVelocity = pProjectile->GetAbsVelocity() - vVelocity;
		const float flRelativeSpeed = vRelativeVelocity.LengthSqr();
		if (flRelativeSpeed < 100.f * 100.f)
			continue;

		const float flTime = std::clamp(-vRelativeOrigin.Dot(vRelativeVelocity) / flRelativeSpeed, 0.f, 0.85f);
		if (flTime > flBestTime)
			continue;

		const Vec3 vClosest = vRelativeOrigin + vRelativeVelocity * flTime;
		if (vClosest.LengthSqr() > 220.f * 220.f || !SDK::VisPosWorld(pLocal, pProjectile, vEye, vOrigin))
			continue;

		flBestTime = flTime;
		pBest = pProjectile;
	}
	return pBest;
}

CBaseEntity* CBotUtils::FindLegitAttention(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3& vEye, const Vec3& vOrigin, const Vec3& vPathDirection, bool bHasPathTarget, bool bAllowSteep, float* pScore)
{
	if (!pLocal || !Vars::Aimbot::General::LegitForBots::ReactToEnemies.Value)
		return nullptr;

	struct Candidate_t
	{
		CBaseEntity* m_pEntity = nullptr;
		float m_flScore = -FLT_MAX;
	};

	std::vector<Candidate_t> vCandidates;
	const Vec3 vViewAngles = I::EngineClient->GetViewAngles();
	const float flSinceDamage = I::GlobalVars->curtime - pLocal->m_flLastDamageTime();
	auto AddCandidate = [&](CBaseEntity* pEntity, const Vec3& vPoint, float flBase)
		{
			if (!pEntity || pEntity->IsDormant())
				return;
			const Vec3 vToTarget = vPoint - vEye;
			const float flDistance = vToTarget.Length();
			if (flDistance < 1.f)
				return;
			const float flFov = Math::CalcFov(vViewAngles, Math::CalcAngle(vEye, vPoint));
			if (flFov > 165.f)
				return;

			float flScore = flBase + std::clamp(1.f - flDistance / 2600.f, 0.f, 1.f) * 2.2f
				+ std::clamp(1.f - flFov / 130.f, 0.f, 1.f) * 2.4f;
			if (pEntity->entindex() == G::AimTarget.m_iEntIndex)
				flScore += 3.5f;
			if (pEntity->entindex() == m_tLLAP.m_iTarget)
				flScore += 3.f;
			if (pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.35f)
				flScore += 0.8f;
			if (bHasPathTarget && !IsPathTargetForward(vOrigin, vPoint, vPathDirection, -0.25f))
				flScore -= 1.5f;
			vCandidates.push_back({ pEntity, flScore });
		};

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pEnemy = pEntity->As<CTFPlayer>();
		if (!pEnemy || !pEnemy->IsAlive() || ShouldTarget(pLocal, pWeapon, pEnemy->entindex()) != ShouldTargetEnum::Target)
			continue;

		const Vec3 vPoint = GetLegitActorPoint(pEnemy);
		float flThreat = 0.f;
		Vec3 vForward;
		Math::AngleVectors(pEnemy->GetEyeAngles(), &vForward);
		Vec3 vToLocal = pLocal->GetEyePosition() - pEnemy->GetEyePosition();
		if (vToLocal.Normalize() > 0.01f && vForward.Dot(vToLocal) > 0.94f)
			flThreat += 3.5f;
		if (flSinceDamage < 0.75f)
			flThreat += 1.4f;
		if (pEnemy->m_iHealth() < pEnemy->GetMaxHealth() * 0.35f)
			flThreat += 0.5f;
		AddCandidate(pEnemy, vPoint, flThreat);
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (!pBuilding || pBuilding->m_iHealth() <= 0 || ShouldTargetBuilding(pLocal, pBuilding->entindex()) != ShouldTargetEnum::Target)
			continue;
		float flThreat = 0.f;
		if (pBuilding->IsSentrygun())
		{
			auto pSentry = pBuilding->As<CObjectSentrygun>();
			if (pSentry && pSentry->m_hEnemy().Get() == pLocal)
				flThreat += 4.5f;
		}
		AddCandidate(pBuilding, GetLegitActorPoint(pBuilding), flThreat);
	}

	if (pWeapon)
	{
		if (auto pMedigun = pWeapon->As<CWeaponMedigun>())
		{
			if (auto pHealing = pMedigun->m_hHealingTarget().Get())
				AddCandidate(pHealing, GetLegitActorPoint(pHealing), 4.f);
		}
	}

	std::sort(vCandidates.begin(), vCandidates.end(), [](const Candidate_t& a, const Candidate_t& b)
		{
			return a.m_flScore > b.m_flScore;
		});

	const size_t nTraceCount = std::min<size_t>(vCandidates.size(), 5);
	for (size_t i = 0; i < nTraceCount; i++)
	{
		CBaseEntity* pEntity = vCandidates[i].m_pEntity;
		const Vec3 vPoint = GetLegitActorPoint(pEntity);
		if (!bAllowSteep && std::fabs(vPoint.z - vEye.z) > 180.f)
			continue;
		if (SDK::VisPos(pLocal, pEntity, vEye, vPoint))
		{
			if (pScore)
				*pScore = vCandidates[i].m_flScore;
			return pEntity;
		}
	}
	return nullptr;
}

Vec3 CBotUtils::GetLegitLookPoint(CTFPlayer* pLocal, const Vec3& vEye, const Vec3& vOrigin, const Vec3& vDest, const Vec3& vPathDirection, bool bAllowSteep)
{
	auto& tState = m_tLLAP;
	const int iTick = I::GlobalVars->tickcount;
	const float flNow = I::GlobalVars->curtime;

	const bool bSameDest = (vDest.IsZero() && tState.m_vPathDest.IsZero())
		|| (!vDest.IsZero() && !tState.m_vPathDest.IsZero() && vDest.DistToSqr(tState.m_vPathDest) < 128.f * 128.f);
	const bool bSameDirection = tState.m_vPathDirection.IsZero() || tState.m_vPathDirection.Dot(vPathDirection) >= 0.94f;
	if (!bSameDest || !bSameDirection)
	{
		tState.m_vPathGoal = {};
		tState.m_vOpening = {};
		tState.m_flOpeningUntil = 0.f;
	}
	tState.m_vPathDest = vDest;
	tState.m_vPathDirection = vPathDirection;

	if (iTick - tState.m_iPathScanTick < 6 && !tState.m_vPathGoal.IsZero())
	{
		float flGoalDist = 0.f;
		if (TraceLegitLookPoint(pLocal, vEye, tState.m_vPathGoal, &flGoalDist) && flGoalDist > 60.f
			&& IsPathTargetForward(vOrigin, tState.m_vPathGoal, vPathDirection, -0.25f))
			return tState.m_vPathGoal;
		tState.m_vPathGoal = {};
	}
	tState.m_iPathScanTick = iTick;

	const float flEyeLift = pLocal->m_iClass() == TF_CLASS_SNIPER
		? std::clamp(vEye.z - vOrigin.z - 6.f, 40.f, 62.f)
		: std::clamp(vEye.z - vOrigin.z - 14.f, 34.f, 58.f);

	Vec3 vBest = {};
	float flBestDist = 0.f;

	if (auto pCrumbs = F::NavEngine.GetCrumbs())
	{
		for (const Crumb_t& tCrumb : *pCrumbs)
		{
			Vec3 vPoint = tCrumb.m_vPos;
			vPoint.z += flEyeLift;
			if (vPoint.DistToSqr(vEye) < 130.f * 130.f)
				continue;
			if (!bAllowSteep && std::fabs(vPoint.z - vEye.z) > 140.f)
				continue;
			float flDist = 0.f;
			if (!TraceLegitLookPoint(pLocal, vEye, vPoint, &flDist))
				break;
			if (flDist > flBestDist)
			{
				vBest = vPoint;
				flBestDist = flDist;
			}
			if (flBestDist > 500.f)
				break;
		}
	}

	if (vBest.IsZero() && !vDest.IsZero())
	{
		Vec3 vPoint = vDest;
		vPoint.z += flEyeLift;
		if (vPoint.DistToSqr(vEye) >= 110.f * 110.f && TraceLegitLookPoint(pLocal, vEye, vPoint))
			vBest = vPoint;
	}

	if (vBest.IsZero())
	{
		const float flSlope = std::clamp(vDest.IsZero() ? 0.f : vDest.z - vOrigin.z, -40.f, 40.f);
		for (float flDist : { 300.f, 220.f, 160.f, 110.f, 80.f })
		{
			Vec3 vPoint = vEye + vPathDirection * flDist;
			vPoint.z = vEye.z + flSlope * std::min(flDist / 200.f, 1.f);
			if (TraceLegitLookPoint(pLocal, vEye, vPoint))
			{
				vBest = vPoint;
				break;
			}
		}
	}

	tState.m_vOpening = {};
	tState.m_flOpeningUntil = 0.f;
	for (float flYaw : { 42.f, -42.f, 70.f, -70.f })
	{
		const Vec3 vProbe = MakePositionFromYaw(vEye, vPathDirection, flYaw, 180.f, 0.f);
		if (TraceLegitLookPoint(pLocal, vEye, vProbe))
		{
			tState.m_vOpening = vProbe;
			tState.m_flOpeningUntil = flNow + 1.2f;
			break;
		}
	}

	if (vBest.IsZero())
	{
		Vec3 vFall = vPathDirection;
		if (vFall.LengthSqr() < 0.01f)
		{
			Math::AngleVectors(tState.m_tMotion.m_vAngles, &vFall);
			vFall.z = 0.f;
			if (vFall.Normalize() < 0.01f)
				vFall = { 1.f, 0.f, 0.f };
		}
		vBest = vEye + vFall * 140.f;
	}

	tState.m_vPathGoal = vBest;
	return vBest;
}

void CBotUtils::AimLegit(CTFPlayer* pLocal, CUserCmd* pCmd, int iTarget, const Vec3& vToAngle)
{
	if (!pLocal || !pCmd)
		return;

	auto& tState = m_tLegitAim;
	const float flNow = I::GlobalVars->curtime;
	const float flDt = std::clamp(flNow - tState.m_tMotion.m_flLastTime, 0.f, std::max(I::GlobalVars->interval_per_tick * 4.f, 0.05f));
	tState.m_tMotion.m_flLastTime = flNow;
	tState.m_iLastCommand = pCmd->command_number;

	if (!tState.m_tMotion.m_bInitialized)
	{
		tState.m_tMotion.m_vAngles = I::EngineClient->GetViewAngles();
		tState.m_tMotion.m_bInitialized = true;
	}
	else if (flNow - tState.m_flLastSeen > 0.25f)
	{
		tState.m_tMotion.m_vAngles = I::EngineClient->GetViewAngles();
		tState.m_tMotion.m_vVelocity = {};
		tState.m_tMotion.m_vGoalPrev = {};
		tState.m_tMotion.m_flFlickDur = 0.f;
		tState.m_vAimPoint = {};
	}

	auto pClientTarget = iTarget > 0 ? I::ClientEntityList->GetClientEntity(iTarget) : nullptr;
	CBaseEntity* pTarget = pClientTarget ? pClientTarget->As<CBaseEntity>() : nullptr;

	auto pWeaponEntity = pLocal->m_hActiveWeapon().Get();
	auto pWeapon = pWeaponEntity ? pWeaponEntity->As<CTFWeaponBase>() : nullptr;
	const bool bZoomed = pLocal->InCond(TF_COND_ZOOMED);
	const bool bProjectile = pWeapon && SDK::GetWeaponType(pWeapon) == EWeaponType::PROJECTILE;

	const Vec3 vEye = pLocal->GetEyePosition();
	const Vec3 vShootPos = pLocal->GetShootPos();
	float flDistance = 400.f;
	if (pTarget)
	{
		const Vec3 vApprox = pTarget->IsPlayer() ? pTarget->As<CTFPlayer>()->GetEyePosition() : pTarget->GetCenter();
		flDistance = std::max((vApprox - vShootPos).Length(), 40.f);
	}

	const Vec3 vTruePoint = [&]
		{
			Vec3 vDir;
			Math::AngleVectors(vToAngle, &vDir);
			return vShootPos + vDir * flDistance;
		}();

	const float flSeen = m_mSeenTimes[iTarget];
	m_mSeenTimes[iTarget] = flNow;
	if (m_mSeenTimes.size() > 96)
		m_mSeenTimes.clear();

	const bool bCommitted = flNow < tState.m_flCommitUntil;
	const bool bNewEngagement = tState.m_iTarget != iTarget;

	if (bNewEngagement && !bCommitted)
	{
		const bool bReacquire = flSeen > 0.f && flNow - flSeen < 3.5f;
		tState.m_flReactUntil = flNow + GetLegitReactionTime() * (bReacquire ? 0.55f : 1.f);
		tState.m_vAimPoint = {};
		tState.m_vAimVel = {};
		tState.m_flPrevYaw = 0.f;
		tState.m_flSteadySince = 0.f;
		tState.m_flFireAt = 0.f;
		tState.m_flPrevErr = FLT_MAX;
		tState.m_flMousepad = 0.f;
		tState.m_flResettleUntil = 0.f;
	}
	tState.m_iTarget = iTarget;
	if (pTarget && !pTarget->IsDormant())
		tState.m_flLastSeen = flNow;

	if (tState.m_vAimPoint.IsZero())
	{
		Vec3 vForward;
		Math::AngleVectors(tState.m_tMotion.m_vAngles, &vForward);
		tState.m_vAimPoint = vEye + vForward * flDistance;
		tState.m_vAimVel = pTarget ? pTarget->GetAbsVelocity() * 0.3f : Vec3{};
	}

	const Vec3 vSubjVel = pTarget ? pTarget->GetAbsVelocity() : Vec3{};
	const Vec3 vSubjAccel = flDt > 0.f ? (vSubjVel - tState.m_vPrevSubjVel) / flDt : Vec3{};
	tState.m_vPrevSubjVel = vSubjVel;
	const float flInterval = GetLegitTrackInterval() * SDK::RandomFloat(0.85f, 1.15f);
	if (flNow >= tState.m_flNextSample)
	{
		Vec3 vErr = vTruePoint + tState.m_vBias - tState.m_vAimPoint;
		const float flErrLen = vErr.Normalize();

		if (bNewEngagement && !bCommitted && flErrLen > 0.f)
		{
			const float flOvershoot = std::clamp(Vars::Aimbot::General::LegitForBots::Overshoot.Value / 100.f, 0.f, 1.f)
				* Math::Lerp(1.35f, 0.55f, GetLegitSkill());
			tState.m_vAimPoint += vErr * std::clamp(flErrLen * flOvershoot, 0.f, flDistance * 0.06f);
			vErr = vTruePoint + tState.m_vBias - tState.m_vAimPoint;
		}

		const float flLeadTime = Math::Lerp(0.008f, 0.03f, GetLegitSkill());
		Vec3 vLead = vSubjVel * flLeadTime + vSubjAccel * (0.35f * flLeadTime * flLeadTime);
		const float flLeadLen = std::min(vLead.Length(), flDistance * 0.02f);
		if (vLead.Normalize() > 0.f)
			vLead *= flLeadLen;
		const Vec3 vCatch = (vTruePoint + tState.m_vBias + vLead) - tState.m_vAimPoint;
		Vec3 vNewVel = vCatch / std::max(flInterval, flDt) + vSubjVel;
		const float flCatchCap = flDistance * Math::Lerp(2.0f, 4.5f, GetLegitSkill());
		const float flNewLen = vNewVel.Normalize();
		if (flNewLen > 0.f)
			tState.m_vAimVel = vNewVel * std::min(flNewLen, flCatchCap);
		else
			tState.m_vAimVel = {};
		tState.m_flNextSample = flNow + flInterval;
	}

	{
		const float flSign = (vSubjVel.x * (vTruePoint - vEye).y - vSubjVel.y * (vTruePoint - vEye).x);
		if (std::fabs(flSign) > 1.f && tState.m_flPrevYaw != 0.f && std::signbit(flSign) != std::signbit(tState.m_flPrevYaw)
			&& vSubjVel.Length2D() > 60.f)
			tState.m_flNextSample = std::max(tState.m_flNextSample, flNow + Math::Lerp(0.15f, 0.05f, GetLegitSkill()));
		if (flSign != 0.f)
			tState.m_flPrevYaw = flSign;
	}

	if (flNow >= tState.m_flNextBias)
	{
		const float flMovePenalty = 1.f + std::min(pLocal->m_vecVelocity().Length2D() / 400.f, 1.f) * 0.35f;
		const float flAmp = std::max(Vars::Aimbot::General::LegitForBots::AimError.Value, 0.f) * flDistance * 0.0175f * flMovePenalty;
		tState.m_vBias = {
			SDK::RandomFloat(-flAmp, flAmp),
			SDK::RandomFloat(-flAmp, flAmp),
			SDK::RandomFloat(-flAmp * 0.5f, flAmp * 0.35f)
		};
		tState.m_flNextBias = flNow + SDK::RandomFloat(0.9f, 1.8f);
	}
	tState.m_vBias *= expf(-flDt * Math::Lerp(0.35f, 0.9f, GetLegitSkill()));

	tState.m_vAimPoint += tState.m_vAimVel * flDt;
	if (pTarget && !pTarget->IsDormant())
	{
		const Vec3 vPull = vTruePoint + tState.m_vBias - tState.m_vAimPoint;
		tState.m_vAimPoint += vPull * std::clamp(flDt * Math::Lerp(2.f, 6.f, GetLegitSkill()), 0.f, 0.5f);
	}
	else
		tState.m_vAimVel *= expf(-flDt * 2.5f);

	const float flFlick = std::max(Vars::Aimbot::General::LegitForBots::FlickSpeed.Value, 200.f);
	const float flTrack = std::max(Vars::Aimbot::General::LegitForBots::TrackingSpeed.Value, 10.f);
	float flMaxRate = flFlick;
	float flMinRate = flTrack * Math::Lerp(0.5f, 0.9f, GetLegitSkill());
	float flPitchScale = 0.62f;
	if (bZoomed)
	{
		flMaxRate *= 0.55f;
		flMinRate *= 0.65f;
		flPitchScale *= 0.7f;
	}
	if (bCommitted)
	{
		flMaxRate *= 1.4f;
		flMinRate *= 1.5f;
	}

	const bool bReacting = flNow < tState.m_flReactUntil;
	if (bReacting)
	{
		flMaxRate *= 0.16f;
		flMinRate *= 0.2f;
	}
	else if (tState.m_flReactUntil > 0.f)
	{
		const float flEaseIn = std::clamp((flNow - tState.m_flReactUntil) / 0.12f, 0.55f, 1.f);
		flMaxRate *= flEaseIn;
	}

	const float flTremorAmp = flDistance * 0.0038f * Math::Lerp(1.4f, 0.45f, GetLegitSkill());
	tState.m_flPhase = std::fmod(tState.m_flPhase + flDt, 6.2831853f);
	const float flPhase = tState.m_flPhase * 8.f;
	const Vec3 vTremor = {
		sinf(flPhase * 2.24f + 1.7f) * 0.6f + sinf(flPhase * 3.93f + 0.9f) * 0.4f,
		sinf(flPhase * 1.91f + 3.9f) * 0.6f + sinf(flPhase * 3.46f + 2.1f) * 0.4f,
		sinf(flPhase * 2.65f + 5.3f) * 0.5f
	};
	const Vec3 vDesired = Math::CalcAngle(vEye, tState.m_vAimPoint + vTremor * flTremorAmp);
	AdvanceLegitView(tState.m_tMotion, vDesired, flDt, flMaxRate, flMinRate, flPitchScale,
		&tState.m_flAnchorSince, &tState.m_flResettleUntil, &tState.m_flMousepad, !bReacting);

	Vec3 vWish = tState.m_tMotion.m_vAngles;
	{
		const Vec3 vWrote = pCmd->viewangles;
		const float flMaxStep = flMaxRate * flDt * 1.35f + 0.5f;
		const Vec3 vStep = vWish.DeltaAngle(vWrote);
		const float flStepLen = vStep.Length();
		if (flStepLen > flMaxStep && flStepLen > 0.f)
			vWish = vWrote + vStep * (flMaxStep / flStepLen);
		tState.m_tMotion.m_vAngles = vWish;
	}
	if (bProjectile && !bReacting)
	{
		const float flReadyIn = std::max(pWeapon->m_flNextPrimaryAttack() - flNow, 0.f);
		const auto pWeaponInfo = pWeapon->m_pWeaponInfo();
		const float flFireDelay = pWeaponInfo ? pWeaponInfo->GetWeaponData(pWeapon->m_iWeaponMode()).m_flTimeFireDelay : 0.2f;
		const float flSnapWindow = std::clamp(flFireDelay * 0.4f, 0.05f, 0.12f);
		const bool bAngleValid = vToAngle.Length2D() > 0.01f;
		if (flReadyIn > 0.f && flReadyIn <= flSnapWindow && bAngleValid)
		{
			if (tState.m_flSnapStart <= 0.f)
			{
				tState.m_flSnapStart = flNow;
				tState.m_vSnapFrom = vWish;
			}
		}

		if (tState.m_flSnapStart > 0.f && bAngleValid)
		{
			const float flT = std::clamp(1.f - flReadyIn / flSnapWindow, 0.f, 1.f);
			const Vec3 vSnapDelta = vToAngle.DeltaAngle(tState.m_vSnapFrom);
			if (vSnapDelta.Length() < 75.f)
			{
				vWish = tState.m_vSnapFrom + vSnapDelta * std::clamp(flDt / std::max(flReadyIn, 0.001f) * (0.55f + flT * 0.45f), 0.f, 1.f);
				tState.m_tMotion.m_vAngles = vWish;
				tState.m_tMotion.m_vVelocity = vWish.DeltaAngle(tState.m_vSnapFrom) / std::max(flDt, 0.001f);
			}
			if (flReadyIn > flSnapWindow || flNow - tState.m_flSnapStart > 0.15f)
				tState.m_flSnapStart = 0.f;
		}
		else if (tState.m_flSnapStart > 0.f)
			tState.m_flSnapStart = 0.f;
	}
	vWish.x = std::clamp(vWish.x, -89.f, 89.f);
	Math::ClampAngles(vWish);

	const float flGoalErrDeg = Math::CalcFov(tState.m_tMotion.m_vAngles, Math::CalcAngle(vEye, tState.m_vAimPoint));
	const float flErrDeg = Math::CalcFov(vWish, vToAngle);
	const float flAngularSize = std::atan2(24.f, std::max(flDistance, 40.f)) * 57.2958f;
	const float flTolerance = std::max(
		Vars::Aimbot::General::LegitForBots::FireTolerance.Value * Math::Lerp(1.5f, 0.7f, GetLegitSkill()),
		flAngularSize * 0.45f);

	bool bAligned = flErrDeg <= flTolerance && (bProjectile || flGoalErrDeg <= flTolerance * 1.6f);
	const bool bCrossing = !bAligned && flErrDeg <= flTolerance * 2.2f && (bProjectile || flGoalErrDeg <= flTolerance * 1.6f)
		&& tState.m_flPrevErr != FLT_MAX && tState.m_flPrevErr - flErrDeg > 0.03f;
	tState.m_flPrevErr = flErrDeg;

	const float flViewSpeed = tState.m_tMotion.m_vVelocity.Length();
	if (flViewSpeed < 14.f)
	{
		if (tState.m_flSteadySince <= 0.f)
			tState.m_flSteadySince = flNow;
	}
	else
		tState.m_flSteadySince = 0.f;

	bool bClear = IsLegitShotClear(pLocal, pTarget, vWish);
	bool bRecentTarget = flNow - tState.m_flLastSeen < 0.25f;
	bool bGate = (bAligned || bCrossing) && bClear && bRecentTarget && !bReacting;

	if (bGate && bZoomed && pWeapon)
	{
		bool bSteadyGate = flViewSpeed < 18.f && tState.m_flSteadySince > 0.f && flNow - tState.m_flSteadySince > 0.08f;
		bGate = flErrDeg <= std::max(flTolerance * 0.5f, flAngularSize * 0.4f) && bSteadyGate;
	}

	if (bGate)
	{
		if (tState.m_flFireAt <= 0.f)
		{
			const float flDelay = std::max(Vars::Aimbot::General::LegitForBots::TriggerDelay.Value, 0.f);
			tState.m_flFireAt = flNow + flDelay * SDK::RandomFloat(0.7f, 1.4f);
		}
	}
	else
		tState.m_flFireAt = 0.f;

	tState.m_bReady = bGate && flNow >= tState.m_flFireAt;

	pCmd->viewangles = vWish;
	I::EngineClient->SetViewAngles(vWish);
}

bool CBotUtils::IsLegitAimReady(int iTarget) const
{
	if (m_tLegitAim.m_iTarget != iTarget || !m_tLegitAim.m_bReady)
		return false;
	const int iCommand = G::CurrentUserCmd ? G::CurrentUserCmd->command_number : -1;
	return m_tLegitAim.m_iLastCommand == iCommand;
}

bool CBotUtils::IsLegitAimActive() const
{
	return m_tLegitAim.m_iTarget > 0
		&& I::GlobalVars->curtime - m_tLegitAim.m_flLastSeen < 0.35f
		&& m_tLegitAim.m_tMotion.m_bInitialized;
}

void CBotUtils::LockLegitAim(int iTarget, int iTicks)
{
	m_tLegitAim.m_iTarget = iTarget;
	m_tLegitAim.m_flCommitUntil = I::GlobalVars->curtime + std::max(iTicks, 1) * I::GlobalVars->interval_per_tick;
	m_tLegitAim.m_flReactUntil = 0.f;
	m_tLegitAim.m_tMotion.m_flLastTime = I::GlobalVars->curtime;
	m_tLegitAim.m_bReady = true;
}

void CBotUtils::LookLegit(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vDest, bool bSilent, bool bFocus)
{
	if (!pLocal || !pCmd)
		return;
	auto& tState = m_tLLAP;
	const float flNow = I::GlobalVars->curtime;
	if (SmoothAimHasPriority())
	{
		const Vec3 vCurrent = I::EngineClient->GetViewAngles();
		tState.m_iLastCommand = pCmd->command_number;
		tState.m_tMotion.m_vAngles = vCurrent;
		tState.m_tMotion.m_vVelocity = {};
		tState.m_tMotion.m_vGoalPrev = {};
		tState.m_tMotion.m_flLastTime = flNow;
		tState.m_tMotion.m_bInitialized = true;
		m_vLastAngles = vCurrent;
		return;
	}

	if (tState.m_iLastCommand == pCmd->command_number)
		return;
	const float flDt = std::clamp(flNow - tState.m_tMotion.m_flLastTime, 0.f, std::max(I::GlobalVars->interval_per_tick * 4.f, 0.05f));
	if (flNow - tState.m_tMotion.m_flLastTime > 0.25f && tState.m_tMotion.m_bInitialized)
	{
		tState.m_tMotion.m_vAngles = I::EngineClient->GetViewAngles();
		tState.m_tMotion.m_vVelocity = {};
		tState.m_tMotion.m_vGoalPrev = {};
	}
	tState.m_tMotion.m_flLastTime = flNow;
	tState.m_iLastCommand = pCmd->command_number;

	const Vec3 vVelocity = pLocal->m_vecVelocity();
	const Vec3 vOrigin = pLocal->GetAbsOrigin();
	const Vec3 vEye = pLocal->GetEyePosition();
	const Vec3 vPathDelta = vDest.IsZero() ? Vec3{} : vDest - vOrigin;
	const bool bHasPathTarget = !vDest.IsZero() && vPathDelta.Length2D() > 24.f;
	Vec3 vViewForward;
	Math::AngleVectors(I::EngineClient->GetViewAngles(), &vViewForward);
	const Vec3 vPathDirection = GetFlatDirection(vPathDelta, bHasPathTarget ? Vec3{} : vVelocity.Length2D() > 24.f ? vVelocity : vViewForward);
	const bool bAllowSteep = !pLocal->OnSolid() || (!vDest.IsZero() && std::fabs(vDest.z - vOrigin.z) > 48.f) || std::fabs(vVelocity.z) > 120.f;
	auto pWeaponEntity = pLocal->m_hActiveWeapon().Get();
	auto pWeapon = pWeaponEntity ? pWeaponEntity->As<CTFWeaponBase>() : nullptr;
	auto pMedigun = pWeapon ? pWeapon->As<CWeaponMedigun>() : nullptr;
	CBaseEntity* pHealingTarget = pMedigun ? pMedigun->m_hHealingTarget().Get() : nullptr;
	if (tState.m_flNextGlance <= 0.f)
		tState.m_flNextGlance = flNow + SDK::RandomFloat(2.8f, 5.6f);

	auto pClientTarget = tState.m_iTarget > 0 ? I::ClientEntityList->GetClientEntity(tState.m_iTarget) : nullptr;
	CBaseEntity* pTarget = pClientTarget ? pClientTarget->As<CBaseEntity>() : nullptr;
	bool bTargetAlive = pTarget && (pTarget->IsPlayer()
		? pTarget->As<CTFPlayer>()->IsAlive()
		: pTarget->IsBuilding() && pTarget->As<CBaseObject>()->m_iHealth() > 0);
	if (!bTargetAlive)
	{
		pTarget = nullptr;
		tState.m_bTargetValid = false;
		tState.m_iTarget = -1;
	}
	if (!Vars::Aimbot::General::LegitForBots::ReactToEnemies.Value)
	{
		pTarget = nullptr;
		tState.m_bTargetValid = false;
		tState.m_iTarget = -1;
		tState.m_flTargetSeen = 0.f;
		tState.m_vLastSeenPoint = {};
		tState.m_flTargetScore = 0.f;
	}
	else if (pTarget && (pTarget->IsDormant()
		|| pTarget->IsPlayer() && pTarget != pHealingTarget && ShouldTarget(pLocal, pWeapon, pTarget->entindex()) != ShouldTargetEnum::Target
		|| pTarget->IsBuilding() && ShouldTargetBuilding(pLocal, pTarget->entindex()) != ShouldTargetEnum::Target))
	{
		pTarget = nullptr;
		tState.m_bTargetValid = false;
	}

	if (pTarget)
	{
		tState.m_vTargetPoint = GetLegitActorPoint(pTarget);
		tState.m_bTargetValid = SDK::VisPos(pLocal, pTarget, vEye, tState.m_vTargetPoint);
		if (tState.m_bTargetValid)
		{
			tState.m_flTargetSeen = flNow;
			tState.m_vLastSeenPoint = tState.m_vTargetPoint;
		}
	}
	else
		tState.m_bTargetValid = false;

	if (flNow >= tState.m_flNextTargetScan)
	{
		float flCandidateScore = 0.f;
		CBaseEntity* pCandidate = FindLegitAttention(pLocal, pWeapon, vEye, vOrigin, vPathDirection, bHasPathTarget, bAllowSteep, &flCandidateScore);
		const bool bImportant = flCandidateScore > tState.m_flTargetScore + 1.5f;
		if (pCandidate && pCandidate->entindex() == tState.m_iTarget)
			tState.m_flTargetScore = flCandidateScore;
		if (pCandidate && (pCandidate->entindex() != tState.m_iTarget || !tState.m_bTargetValid)
			&& (!tState.m_bTargetValid || flNow >= tState.m_flTargetDwell || bImportant))
		{
			const int iPrevious = tState.m_iTarget;
			tState.m_iTarget = pCandidate->entindex();
			tState.m_vTargetPoint = GetLegitActorPoint(pCandidate);
			tState.m_flTargetSeen = flNow;
			tState.m_flTargetScore = flCandidateScore;
			tState.m_flTargetDwell = flNow + SDK::RandomFloat(0.55f, 1.2f);
			const bool bImmediate = flNow - pLocal->m_flLastDamageTime() < 0.5f;
			tState.m_flReactionEnd = flNow + GetLegitReactionTime() * (bImmediate ? 0.55f : 1.f);
			tState.m_vLastSeenPoint = tState.m_vTargetPoint;
			tState.m_bTargetValid = true;
			if (iPrevious != tState.m_iTarget)
				tState.m_vGlanceGoal = {};
		}
		tState.m_flNextTargetScan = flNow + SDK::RandomFloat(0.1f, 0.18f);
	}

	if (!Vars::Aimbot::General::LegitForBots::ReactToThreats.Value)
	{
		tState.m_iThreat = -1;
		tState.m_vThreatPoint = {};
		tState.m_flThreatUntil = 0.f;
	}
	else
	{
		if (tState.m_iThreat > 0)
		{
			auto pClientThreat = I::ClientEntityList->GetClientEntity(tState.m_iThreat);
			CBaseEntity* pThreat = pClientThreat ? pClientThreat->As<CBaseEntity>() : nullptr;
			if (!pThreat || pThreat->IsDormant() || flNow >= tState.m_flThreatUntil)
			{
				tState.m_iThreat = -1;
				tState.m_vThreatPoint = {};
				tState.m_flThreatUntil = 0.f;
			}
			else
				tState.m_vThreatPoint = pThreat->GetAbsOrigin();
		}
		if (flNow >= tState.m_flNextThreatScan)
		{
			if (auto pThreat = FindLegitThreat(pLocal, vEye))
			{
				tState.m_iThreat = pThreat->entindex();
				tState.m_vThreatPoint = pThreat->GetAbsOrigin();
				tState.m_flThreatUntil = flNow + 0.32f;
			}
			tState.m_flNextThreatScan = flNow + SDK::RandomFloat(0.11f, 0.18f);
		}
	}

	const bool bTargetMemory = !tState.m_bTargetValid && !tState.m_vLastSeenPoint.IsZero()
		&& flNow - tState.m_flTargetSeen <= Vars::Aimbot::General::LegitForBots::TargetMemory.Value;
	if (!tState.m_bTargetValid && !bTargetMemory)
	{
		tState.m_iTarget = -1;
		tState.m_vLastSeenPoint = {};
		tState.m_flTargetScore = 0.f;
	}

	bool bActorFocus = pTarget && tState.m_bTargetValid && flNow >= tState.m_flReactionEnd;
	bool bMemoryFocus = !bActorFocus && bTargetMemory && flNow >= tState.m_flReactionEnd;
	const bool bThreatFocus = Vars::Aimbot::General::LegitForBots::ReactToThreats.Value && flNow < tState.m_flThreatUntil;

	Vec3 vLook = GetLegitLookPoint(pLocal, vEye, vOrigin, vDest, vPathDirection, bAllowSteep);

	if (bThreatFocus)
	{
		vLook = tState.m_vThreatPoint;
		tState.m_bGlancing = true;
	}
	else if (bFocus && !vDest.IsZero())
		vLook = vDest;
	else if (bActorFocus)
		vLook = tState.m_vTargetPoint;
	else if (bMemoryFocus)
		vLook = tState.m_vLastSeenPoint;

	if (!bThreatFocus && !bActorFocus && !bMemoryFocus && !bFocus)
	{
		if (tState.m_bGlancing)
		{
			if (flNow >= tState.m_flGlanceUntil)
			{
				tState.m_bGlancing = false;
				tState.m_vGlanceGoal = {};
				tState.m_flNextGlance = flNow + SDK::RandomFloat(2.8f, 5.6f);
			}
		}
		else if (Vars::Aimbot::General::LegitForBots::IdleScanning.Value && flNow >= tState.m_flNextGlance)
		{
			tState.m_bGlancing = true;
			tState.m_flGlanceUntil = flNow + SDK::RandomFloat(0.32f, 0.68f);
			if (!tState.m_vOpening.IsZero() && flNow < tState.m_flOpeningUntil && SDK::RandomInt(0, 9) < 6)
			{
				const Vec3 vOpenDelta = Math::CalcAngle(vEye, tState.m_vOpening).DeltaAngle(Math::CalcAngle(vEye, vLook));
				tState.m_vGlanceGoal = {
					std::clamp(vOpenDelta.x * 0.7f, -6.f, 6.f),
					std::clamp(vOpenDelta.y * 0.8f, -26.f, 26.f),
					0.f
				};
			}
			else
			{
				tState.m_vGlanceGoal = {
					SDK::RandomFloat(-2.5f, 3.5f),
					SDK::RandomFloat(7.f, 19.f) * (SDK::RandomInt(0, 1) ? 1.f : -1.f),
					0.f
				};
			}
		}
	}
	else
	{
		tState.m_bGlancing = false;
		tState.m_vGlanceGoal = {};
	}
	tState.m_vGlanceCurrent = tState.m_vGlanceCurrent.LerpAngle(tState.m_vGlanceGoal, std::clamp(I::GlobalVars->interval_per_tick * (tState.m_bGlancing ? 12.f : 7.f), 0.f, 0.45f));

	tState.m_flPhase += I::GlobalVars->interval_per_tick * std::clamp(0.7f + vVelocity.Length2D() / 360.f, 0.7f, 1.7f);
	if (tState.m_flPhase > 8192.f)
		tState.m_flPhase = std::fmod(tState.m_flPhase, 8192.f);
	const float flMicroScale = bActorFocus ? 0.12f : 0.35f;
	const Vec3 vMicro = {
		std::sin(tState.m_flPhase * 0.43f) * 0.45f * flMicroScale,
		std::sin(tState.m_flPhase * 0.29f + 1.8f) * 0.65f * flMicroScale,
		0.f
	};

	Vec3 vDesired = Math::CalcAngle(vEye, vLook);
	vDesired += tState.m_vGlanceCurrent + vMicro;
	Math::ClampAngles(vDesired);
	vDesired.x = std::clamp(vDesired.x, (bThreatFocus || bActorFocus) ? -70.f : -35.f, (bThreatFocus || bActorFocus) ? 70.f : 30.f);

	const float flBaseSpeed = static_cast<float>(Vars::Misc::Movement::BotUtils::LookAtPathSpeed.Value);
	const float flSpeedScale = bThreatFocus ? 1.7f : bActorFocus ? 1.3f : bFocus ? 1.45f : 1.f;
	const float flMaxRate = std::max(flBaseSpeed * flSpeedScale * 14.f, 140.f);
	const float flMinRate = std::max(flBaseSpeed * flSpeedScale * 1.5f, 30.f);
	AdvanceLegitView(tState.m_tMotion, vDesired, flDt, flMaxRate, flMinRate, 0.7f,
		&tState.m_flAnchorSince, &tState.m_flResettleUntil, &tState.m_flMousepad, true);
	Vec3 vWish = tState.m_tMotion.m_vAngles;
	Math::ClampAngles(vWish);

	m_vDebugLook = vLook;
	m_flDebugLookExpire = flNow + 0.1f;
	pCmd->viewangles = vWish;
	if (!bSilent)
		I::EngineClient->SetViewAngles(vWish);
	m_vLastAngles = vWish;
}

void CBotUtils::InvalidateLLAP()
{
	const Vec3 vAngles = I::EngineClient ? I::EngineClient->GetViewAngles() : Vec3{};
	m_tLLAP = {};
	m_vLastAngles = vAngles;
}

static void RefreshAutoCache(std::unordered_map<int, bool>& mCache, bool& bClearNext)
{
	if (!Vars::Misc::Movement::BotUtils::AutoScopeUseCachedResults.Value)
		bClearNext = true;

	if (bClearNext)
	{
		mCache.clear();
		bClearNext = false;
	}
	else if (!mCache.empty())
		bClearNext = true;
}

static bool GetAutoViewOrigin(CTFPlayer* pLocal, Vector& vOut)
{
	CNavArea* pArea = nullptr;
	auto pCrumbs = F::NavEngine.GetCrumbs();
	if (pCrumbs->size() > 4)
		pArea = (*pCrumbs)[4].m_pNavArea;
	if (!pArea)
		pArea = F::NavEngine.FindClosestNavArea(pLocal->GetAbsOrigin());
	if (!pArea)
		return false;

	vOut = pArea->m_vCenter;
	vOut.z += PLAYER_JUMP_HEIGHT;
	return true;
}

static bool IsAutoTargetVisible(const Vector& vLocalOrigin, const Vector& vFrom, const Vec3& vTo)
{
	CTraceFilterWorldAndPropsOnly filter = {};
	CGameTrace trace = {};
	SDK::Trace(Vector(vLocalOrigin.x, vLocalOrigin.y, vLocalOrigin.z + PLAYER_JUMP_HEIGHT), vTo, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	if (trace.fraction == 1.0f)
		return true;

	SDK::Trace(vFrom, vTo, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	return trace.fraction == 1.0f;
}

static std::vector<std::pair<CBaseEntity*, float>> GatherAutoTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bSkipInvulnerable)
{
	std::vector<std::pair<CBaseEntity*, float>> vTargets;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	for (auto pEnemy : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (pEnemy->IsDormant())
			continue;
		if (bSkipInvulnerable && pEnemy->As<CTFPlayer>()->IsInvulnerable())
			continue;
		if (F::BotUtils.ShouldTarget(pLocal, pWeapon, pEnemy->entindex()) != ShouldTargetEnum::Target)
			continue;
		vTargets.emplace_back(pEnemy, pEnemy->GetAbsOrigin().DistToSqr(vLocalOrigin));
	}

	for (auto pBuilding : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (pBuilding->IsDormant())
			continue;
		if (F::BotUtils.ShouldTargetBuilding(pLocal, pBuilding->entindex()) != ShouldTargetEnum::Target)
			continue;
		vTargets.emplace_back(pBuilding, pBuilding->GetAbsOrigin().DistToSqr(vLocalOrigin));
	}

	std::sort(vTargets.begin(), vTargets.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
	return vTargets;
}

void CBotUtils::AutoScope(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const int iWeaponID = pWeapon->GetWeaponID();
	const bool bIsClassic = iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC;
	if (!Vars::Misc::Movement::BotUtils::AutoScope.Value
		|| iWeaponID != TF_WEAPON_SNIPERRIFLE && !bIsClassic && iWeaponID != TF_WEAPON_SNIPERRIFLE_DECAP)
	{
		m_bScopeKeep = false;
		m_mAutoScopeCache.clear();
		return;
	}

	RefreshAutoCache(m_mAutoScopeCache, m_bScopeClearCache);

	if (bIsClassic)
	{
		if (m_bScopeKeep)
		{
			pCmd->buttons |= IN_ATTACK;
			if (m_tScopeTimer.Check(Vars::Misc::Movement::BotUtils::AutoScopeCancelTime.Value))
				pCmd->buttons |= IN_JUMP;
		}
		if (!pLocal->OnSolid() && !(pCmd->buttons & IN_ATTACK))
			m_bScopeKeep = false;
	}
	else if (m_bScopeKeep && pLocal->InCond(TF_COND_ZOOMED) && m_tScopeTimer.Check(Vars::Misc::Movement::BotUtils::AutoScopeCancelTime.Value))
	{
		m_bScopeKeep = false;
		pCmd->buttons |= IN_ATTACK2;
		return;
	}

	Vector vFrom;
	if (!GetAutoViewOrigin(pLocal, vFrom))
		return;

	const auto vEnemiesSorted = GatherAutoTargets(pLocal, pWeapon, false);
	if (vEnemiesSorted.empty())
		return;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	auto Engage = [&]
		{
			if (bIsClassic)
				pCmd->buttons |= IN_ATTACK;
			else if (!pLocal->InCond(TF_COND_ZOOMED) && !(pCmd->buttons & IN_ATTACK2))
				pCmd->buttons |= IN_ATTACK2;

			m_tScopeTimer.Update();
			m_bScopeKeep = true;
		};
	auto CheckVisibility = [&](const Vec3& vTo, int iEntIndex) -> bool
		{
			const bool bHit = IsAutoTargetVisible(vLocalOrigin, vFrom, vTo);
			if (iEntIndex != -1)
				m_mAutoScopeCache[iEntIndex] = bHit;
			if (bHit)
				Engage();
			return bHit;
		};

	const bool bSimple = Vars::Misc::Movement::BotUtils::AutoScope.Value == Vars::Misc::Movement::BotUtils::AutoScopeEnum::Simple;
	const int iMaxTicks = TIME_TO_TICKS(0.5f);
	MoveStorage tStorage{};
	for (auto [pEnemy, _] : vEnemiesSorted)
	{
		const int iEntIndex = Vars::Misc::Movement::BotUtils::AutoScopeUseCachedResults.Value ? pEnemy->entindex() : -1;
		if (const auto it = m_mAutoScopeCache.find(iEntIndex); it != m_mAutoScopeCache.end())
		{
			if (!it->second)
				continue;
			Engage();
			break;
		}

		Vector vNonPredictedPos = pEnemy->GetAbsOrigin();
		vNonPredictedPos.z += PLAYER_JUMP_HEIGHT;
		if (CheckVisibility(vNonPredictedPos, iEntIndex))
			return;

		if (!bSimple)
		{
			F::MoveSim.Initialize(pEnemy, tStorage, false);
			if (tStorage.m_bFailed)
				continue;

			for (int i = 0; i < iMaxTicks; i++)
				F::MoveSim.RunTick(tStorage);
		}

		bool bResult = false;
		Vector vPredictedPos = bSimple ? pEnemy->GetAbsOrigin() + pEnemy->GetAbsVelocity() * TICKS_TO_TIME(iMaxTicks) : tStorage.m_vPredictedOrigin;

		auto pTargetNav = F::NavEngine.FindClosestNavArea(vPredictedPos, false);
		if (pTargetNav)
		{
			Vector vTo = pTargetNav->m_vCenter;

			if (!pEnemy->As<CBasePlayer>()->OnSolid() && vTo.DistToSqr(vPredictedPos) >= pow(400.f, 2))
				vTo = vPredictedPos;

			vTo.z += PLAYER_JUMP_HEIGHT;
			bResult = CheckVisibility(vTo, iEntIndex);
		}

		if (bResult)
			break;
	}
}

void CBotUtils::AutoRev(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MINIGUN || pLocal->m_iClass() != TF_CLASS_HEAVY || !pWeapon->HasAmmo())
	{
		m_bRevKeep = false;
		m_mAutoRevCache.clear();
		return;
	}

	RefreshAutoCache(m_mAutoRevCache, m_bRevClearCache);

	if (m_bRevKeep)
	{
		if (m_tRevTimer.Check(Vars::Misc::Movement::BotUtils::AutoScopeCancelTime.Value))
			m_bRevKeep = false;
		else
		{
			pCmd->buttons |= IN_ATTACK2;
			return;
		}
	}

	Vector vFrom;
	if (!GetAutoViewOrigin(pLocal, vFrom))
		return;

	const auto vTargetsSorted = GatherAutoTargets(pLocal, pWeapon, true);
	if (vTargetsSorted.empty())
		return;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	auto Engage = [&]
		{
			pCmd->buttons |= IN_ATTACK2;
			m_tRevTimer.Update();
			m_bRevKeep = true;
		};
	auto CheckVisibility = [&](const Vec3& vTo, int iEntIndex) -> bool
		{
			const bool bHit = IsAutoTargetVisible(vLocalOrigin, vFrom, vTo);
			if (iEntIndex != -1)
				m_mAutoRevCache[iEntIndex] = bHit;
			if (bHit)
				Engage();
			return bHit;
		};

	const bool bSimple = Vars::Misc::Movement::BotUtils::AutoScope.Value != Vars::Misc::Movement::BotUtils::AutoScopeEnum::MoveSim;
	const int iMaxTicks = TIME_TO_TICKS(0.4f);
	MoveStorage tStorage{};
	for (auto [pEntity, _] : vTargetsSorted)
	{
		const int iEntIndex = Vars::Misc::Movement::BotUtils::AutoScopeUseCachedResults.Value ? pEntity->entindex() : -1;
		if (const auto it = m_mAutoRevCache.find(iEntIndex); it != m_mAutoRevCache.end())
		{
			if (!it->second)
				continue;
			Engage();
			break;
		}

		Vector vNonPredictedPos = pEntity->GetAbsOrigin();
		vNonPredictedPos.z += PLAYER_JUMP_HEIGHT;
		if (CheckVisibility(vNonPredictedPos, iEntIndex))
			return;

		if (bSimple || !pEntity->IsPlayer())
			continue;

		auto pEnemyPlayer = pEntity->As<CTFPlayer>();
		F::MoveSim.Initialize(pEnemyPlayer, tStorage, false);
		if (tStorage.m_bFailed)
			continue;

		for (int i = 0; i < iMaxTicks; i++)
			F::MoveSim.RunTick(tStorage);

		bool bResult = false;
		Vector vPredictedPos = tStorage.m_vPredictedOrigin;
		auto pTargetNav = F::NavEngine.FindClosestNavArea(vPredictedPos, false);
		if (pTargetNav)
		{
			Vector vTo = pTargetNav->m_vCenter;
			if (!pEnemyPlayer->OnSolid() && vTo.DistToSqr(vPredictedPos) >= pow(400.f, 2))
				vTo = vPredictedPos;

			vTo.z += PLAYER_JUMP_HEIGHT;
			bResult = CheckVisibility(vTo, iEntIndex);
		}

		if (bResult)
			break;
	}
}

namespace NavRuntime
{
	bool IsMovementLocked(CTFPlayer* pLocal)
	{
		if (!pLocal || !pLocal->IsAlive())
			return true;

		if (pLocal->m_fFlags() & FL_FROZEN)
			return true;

		if (pLocal->InCond(TF_COND_FREEZE_INPUT))
			return true;

		if (F::ZIController.IsSpawning(pLocal))
			return true;

		if (pLocal->InCond(TF_COND_STUNNED) && (pLocal->m_iStunFlags() & TF_STUN_CONTROLS))
			return true;

		if (pLocal->IsTaunting() && !pLocal->m_bAllowMoveDuringTaunt())
			return true;

		if (F::VSHController.IsMovementHeld())
			return true;

		const auto pGameRules = I::TFGameRules();
		if (!pGameRules)
			return false;

		if (pGameRules->m_bPlayingMannVsMachine())
			return false;

		const int iRoundState = pGameRules->m_iRoundState();
		if (iRoundState == GR_STATE_PREROUND && !Vars::Misc::Movement::NavEngine::PathInSetup.Value)
			return true;

		return false;
	}

	bool IsMinigunJumpLocked(CTFWeaponBase* pWeapon, CUserCmd* pCmd)
	{
		if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MINIGUN)
			return false;

		const int iState = pWeapon->As<CTFMinigun>()->m_iWeaponState();
		return iState == AC_STATE_STARTFIRING || iState == AC_STATE_FIRING || iState == AC_STATE_SPINNING || (pCmd && (pCmd->buttons & IN_ATTACK2));
	}

	bool CanUseNavJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
	{
		if (!pLocal)
			return false;
		if (!pWeapon || pLocal->m_fFlags() & FL_INWATER)
			return true;

		if (IsMinigunJumpLocked(pWeapon, G::LastUserCmd))
			return false;

		return pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC
			? !pWeapon->As<CTFSniperRifleClassic>()->m_bCharging()
			: !pLocal->InCond(TF_COND_ZOOMED);
	}
}
