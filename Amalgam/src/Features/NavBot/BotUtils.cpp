#include "BotUtils.h"
#include "NavEngine/NavEngine.h"
#include "NavEngine/Controllers/MVMController/MVMController.h"
#include "../Players/PlayerUtils.h"
#include "../Misc/Misc.h"
#include "../Aimbot/AutoHeal/AutoHeal.h"
#include "../Misc/NamedPipe/NamedPipe.h"
#include "../Ticks/Ticks.h"

static int GetRangedFallbackSlot(CTFPlayer* pLocal)
{
	auto HasWeapon = [&](int iSlot) { return pLocal->GetWeaponFromSlot(iSlot) != nullptr; };
	auto HasUsableWeapon = [&](int iSlot)
		{
			if (!HasWeapon(iSlot)) return false;
			if (!G::AmmoInSlot[iSlot].m_bUsesAmmo) return true;
			return G::AmmoInSlot[iSlot].m_iClip > 0 || G::AmmoInSlot[iSlot].m_iReserve > 0;
		};

	if (HasUsableWeapon(SLOT_PRIMARY))   return SLOT_PRIMARY;
	if (HasUsableWeapon(SLOT_SECONDARY)) return SLOT_SECONDARY;
	if (HasWeapon(SLOT_PRIMARY))         return SLOT_PRIMARY;
	if (HasWeapon(SLOT_SECONDARY))       return SLOT_SECONDARY;
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
		const bool bOutOfAmmo = !SlotHasClip(SLOT_PRIMARY) && !SlotHasClip(SLOT_SECONDARY);
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
		auto pEnemyWeapon = bHasEnemy ? tClosestEnemy.m_pPlayer->m_hActiveWeapon().Get()->As<CTFWeaponBase>() : nullptr;
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
		if (pLocal->GetWeaponFromSlot(SLOT_PRIMARY))
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
	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIdx)->As<CBaseEntity>();
	if (!pEntity || !pEntity->IsPlayer())
		return ShouldTargetEnum::Invalid;

	if (!GetDormantOrigin(iEntIdx))
		return ShouldTargetEnum::DontTarget;

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (!pPlayer->IsAlive() || pPlayer == pLocal)
		return ShouldTargetEnum::Invalid;

#ifdef TEXTMODE
	if (auto pResource = H::Entities.GetResource(); pResource && F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iEntIdx)) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::LocalBots))
		return ShouldTargetEnum::DontTarget;
#endif

	if (F::PlayerUtils.IsIgnored(iEntIdx) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Ignored))
		return ShouldTargetEnum::DontTarget;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(iEntIdx) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Friends)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(iEntIdx) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Friends)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invulnerable && pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invisible && pPlayer->m_flInvisibility() && pPlayer->m_flInvisibility() >= Vars::Aimbot::General::IgnoreInvisible.Value / 100.f
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
	if (iEntIdx <= 0)
		return ShouldTargetEnum::DontTarget;

	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIdx)->As<CBaseEntity>();
	if (!pEntity)
		return ShouldTargetEnum::Invalid;

	if (!pEntity->IsBuilding() || !GetDormantOrigin(iEntIdx))
		return ShouldTargetEnum::DontTarget;

	auto pBuilding = pEntity->As<CBaseObject>();
	if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Sentry) && pBuilding->IsSentrygun()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Dispenser) && pBuilding->IsDispenser()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Teleporter) && pBuilding->IsTeleporter())
		return ShouldTargetEnum::Target;

	if (pLocal->m_iTeamNum() == pBuilding->m_iTeamNum())
		return ShouldTargetEnum::Target;

	auto pOwner = pBuilding->m_hBuilder().Get();
	if (pOwner)
	{
		if (F::PlayerUtils.IsIgnored(pOwner->entindex()) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Ignored))
			return ShouldTargetEnum::DontTarget;

		if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(pOwner->entindex()) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Friends)
			|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(pOwner->entindex()) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Friends))
			return ShouldTargetEnum::DontTarget;
	}

	return ShouldTargetEnum::Target;
}

bool CBotUtils::GetDormantOrigin(int iIndex, Vector* pOut)
{
	if (iIndex <= 0)
		return false;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex)->As<CBaseEntity>();
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
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iEntIndex = pPlayer->entindex();
		if (ShouldTarget(pLocal, pWeapon, iEntIndex) == ShouldTargetEnum::DontTarget)
			continue;

		Vector vOrigin = pPlayer->GetAbsOrigin();
		const float flDistance = vLocalOrigin.DistTo(vOrigin);
		if (flDistance >= tClosestEnemy.m_flDist)
			continue;

		tClosestEnemy.m_iEntIdx = iEntIndex;
		tClosestEnemy.m_pPlayer = pPlayer;
		tClosestEnemy.m_vOrigin = vOrigin;
		tClosestEnemy.m_flDist = flDistance;
		tClosestEnemy.m_flDistZ = vOrigin.z - vLocalOrigin.z;
	}

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

	if (m_iBestSlot == SLOT_MELEE && !pLocal->GetWeaponFromSlot(SLOT_MELEE))
		m_iBestSlot = GetRangedFallbackSlot(pLocal);

	if (m_iBestSlot == -1 && m_iCurrentSlot == SLOT_MELEE)
		m_iBestSlot = GetRangedFallbackSlot(pLocal);
}

void CBotUtils::SetSlot(CTFPlayer* pLocal, int iSlot)
{
	if (iSlot > -1 && F::AutoHeal.m_iAutoSwitch == 0)
	{
		auto sCommand = "slot" + std::to_string(iSlot + 1);
		if (m_iCurrentSlot != iSlot)
			I::EngineClient->ClientCmd_Unrestricted(sCommand.c_str());
	}
}

void CBotUtils::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if ((!Vars::Misc::Movement::NavBot::Enabled.Value && !(Vars::Misc::Movement::FollowBot::Enabled.Value && Vars::Misc::Movement::FollowBot::Targets.Value)) ||
		!pLocal->IsAlive() || !pWeapon)
	{
		Reset();
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
	m_tClosestEnemy = {};
	m_iBestSlot = -1;
	m_iCurrentSlot = -1;
	m_eJumpState = STATE_AWAITING_JUMP;
	m_vPredictedJumpPos = {};
	m_vJumpPeakPos = {};
	InvalidateLLAP();
}
