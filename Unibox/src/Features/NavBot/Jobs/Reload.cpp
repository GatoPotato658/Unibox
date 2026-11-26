#include "NavBotJobs.h"
#include "../Objectives.h"

static bool TryNavToHiddenSpot(const Vector& vVischeckPoint, PriorityListEnum::PriorityListEnum ePriority)
{
	auto pLocalArea = F::NavEngine.GetLocalNavArea();
	if (!pLocalArea)
		return false;

	std::pair<CNavArea*, int> tBestSpot;
	if (!NavAreaUtils::FindClosestHidingSpot(pLocalArea, vVischeckPoint, 5, tBestSpot))
		return false;

	return F::NavEngine.NavTo(tBestSpot.first->m_vCenter, ePriority);
}

bool CNavBotReload::Run()
{
	if (!HasTask() || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
	{
		F::NavEngine.CancelPathIf(PriorityListEnum::RunReload);
		return false;
	}

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::RunReload)
		return false;

	if (!m_tRunReloadCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunReload;

	if (F::NavEngine.IsPathingFor(PriorityListEnum::RunReload))
		return true;

	const auto& tClosestEnemy = F::BotUtils.m_tClosestEnemy;
	if (!tClosestEnemy.m_pPlayer)
		return false;

	Vector vVischeckPoint = tClosestEnemy.m_vOrigin;
	vVischeckPoint.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	return TryNavToHiddenSpot(vVischeckPoint, PriorityListEnum::RunReload);
}

bool CNavBotReload::RunSafe()
{
	if (!NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || !HasTask())
	{
		F::NavEngine.CancelPathIf(PriorityListEnum::RunSafeReload);
		return false;
	}

	if (!m_tSafeReloadCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload;

	if (F::NavEngine.IsPathingFor(PriorityListEnum::RunSafeReload))
		return true;

	Vector vHideFrom;
	const auto& vCrumbs = F::NavEngine.GetCrumbs();
	if (vCrumbs.size() > 4)
		vHideFrom = vCrumbs.at(4).m_vPos;
	else if (F::BotUtils.m_tClosestEnemy.m_pPlayer)
		vHideFrom = F::BotUtils.m_tClosestEnemy.m_vOrigin;
	else
		return false;

	vHideFrom.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	return TryNavToHiddenSpot(vHideFrom, PriorityListEnum::RunSafeReload);
}

void CNavBotReload::Reset()
{
	m_tRunReloadCooldown = Timer();
	m_tSafeReloadCooldown = Timer();
	m_iLastReloadSlot = -1;
}

int CNavBotReload::GetReloadWeaponSlot(CTFPlayer* pLocal, const ClosestEnemy_t& tClosestEnemy)
{
	if (!NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || F::VSHController.IsBossLocal())
		return -1;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Capture)
		return -1;

	if ((F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear && tClosestEnemy.m_flDist <= 500.f) || (tClosestEnemy.m_pPlayer && tClosestEnemy.m_flDist <= 250.f))
		return -1;

	const bool bBestCombatSwitch = Vars::Misc::Movement::BotUtils::WeaponSlot.Value && tClosestEnemy.m_pPlayer
		&& F::BotUtils.m_iBestSlot != -1 && F::BotUtils.m_iBestSlot != F::BotUtils.m_iCurrentSlot && tClosestEnemy.m_flDist <= 900.f;
	if (G::Reloading && !bBestCombatSwitch && F::BotUtils.m_iCurrentSlot >= SLOT_PRIMARY && F::BotUtils.m_iCurrentSlot <= SLOT_SECONDARY)
		return F::BotUtils.m_iCurrentSlot;

	if (bBestCombatSwitch)
		return -1;

	const float flDivider = F::NavEngine.m_eCurrentPriority < PriorityListEnum::StayNear && tClosestEnemy.m_flDist > 500.f ? 1.f : 3.f;

	const auto SlotNeedsReload = [&](int iSlot, bool bIgnoreReserve) -> bool
		{
			if (!G::HasWeaponForSlot[iSlot] || G::SavedDefIndexes[iSlot] == Soldier_m_TheBeggarsBazooka)
				return false;

			if (SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[iSlot], G::SavedDefIndexes[iSlot], false))
				return false;

			const auto& tAmmo = G::AmmoInSlot[iSlot];
			if (!bIgnoreReserve && (tAmmo.m_iMaxClip < 0 || !tAmmo.m_iReserve))
				return false;

			return tAmmo.m_iClip < tAmmo.m_iMaxClip / flDivider;
		};

	if (SlotNeedsReload(SLOT_PRIMARY, G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_PARTICLE_CANNON || G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_DRG_POMSON))
		return SLOT_PRIMARY;
	if (SlotNeedsReload(SLOT_SECONDARY, G::SavedWepIds[SLOT_SECONDARY] == TF_WEAPON_RAYGUN))
		return SLOT_SECONDARY;

	return -1;
}
