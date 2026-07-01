#include "NavBotJobs.h"
#include "../NavAreaUtils.h"

inline bool HasReloadTask(int iReloadSlot)
{
	return G::Reloading || (iReloadSlot >= SLOT_PRIMARY && iReloadSlot <= SLOT_SECONDARY);
}

static bool TryNavToHiddenSpot(CNavArea* pLocalArea, const Vector& vVischeckPoint, PriorityListEnum::PriorityListEnum ePriority)
{
	if (!pLocalArea)
		return false;

	std::pair<CNavArea*, int> tBestSpot;
	if (!NavAreaUtils::FindClosestHidingSpot(pLocalArea, vVischeckPoint, 5, tBestSpot))
		return false;

	return F::NavEngine.NavTo(tBestSpot.first->m_vCenter, ePriority, true, !F::NavEngine.IsPathing());
}

bool CNavBotReload::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tReloadrunCooldown{};

	// Don't run unless we are actively reloading, or we have a valid weapon slot that should be reloaded.
	if (!HasReloadTask(m_iLastReloadSlot))
		return false;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
		return false;

	// Too high priority, so don't try
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::RunReload)
		return false;

	// Re-calc only every once in a while
	if (!tReloadrunCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunReload;

	// Get closest enemy to vicheck
	const auto tClosestEnemy = NavJobUtils::FindClosestTargetEnemy(pLocal, pWeapon);
	if (!tClosestEnemy.m_pPlayer)
		return false;

	Vector vVischeckPoint = tClosestEnemy.m_vOrigin;
	vVischeckPoint.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	return TryNavToHiddenSpot(F::NavEngine.GetLocalNavArea(), vVischeckPoint, PriorityListEnum::RunReload);
}

bool CNavBotReload::RunSafe(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tReloadrunCooldown{};
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || !HasReloadTask(m_iLastReloadSlot))
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload)
			F::NavEngine.CancelPath();
		return false;
	}

	// Re-calc only every once in a while
	if (!tReloadrunCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload;

	// If pathing try to avoid going to our current destination until we fully reload
	Vector vCurrentDestination;
	auto pCrumbs = F::NavEngine.GetCrumbs();
	bool bHasDestination = F::NavEngine.m_eCurrentPriority != PriorityListEnum::RunSafeReload && pCrumbs->size() > 4;
	if (bHasDestination)
		vCurrentDestination = pCrumbs->at(4).m_vPos;

	if (bHasDestination)
		vCurrentDestination.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	else
	{
		// Get closest enemy to vicheck
		const auto tClosestEnemy = NavJobUtils::FindClosestTargetEnemy(pLocal, pWeapon);
		if (tClosestEnemy.m_pPlayer)
		{
			bHasDestination = true;
			vCurrentDestination = tClosestEnemy.m_vOrigin;
			vCurrentDestination.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		}
	}

	if (bHasDestination)
		return TryNavToHiddenSpot(F::NavEngine.GetLocalNavArea(), vCurrentDestination, PriorityListEnum::RunSafeReload);

	return false;
}

int CNavBotReload::GetReloadWeaponSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons))
		return -1;

	if (G::Reloading && F::BotUtils.m_iCurrentSlot >= SLOT_PRIMARY && F::BotUtils.m_iCurrentSlot <= SLOT_SECONDARY)
		return F::BotUtils.m_iCurrentSlot;

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Capture)
		return -1;

	// Dont try to reload in combat
	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear && tClosestEnemy.m_flDist <= 500.f
		|| tClosestEnemy.m_flDist <= 250.f)
		return -1;

	auto pPrimaryWeapon = pLocal->GetWeaponFromSlot(SLOT_PRIMARY);
	auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
	bool bCheckPrimary = !SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[SLOT_PRIMARY], G::SavedDefIndexes[SLOT_PRIMARY], false);
	bool bCheckSecondary = !SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY], false);

	float flDivider = F::NavEngine.m_eCurrentPriority < PriorityListEnum::StayNear && tClosestEnemy.m_flDist > 500.f ? 1.f : 3.f;

	CTFWeaponInfo* pWeaponInfo = nullptr;
	bool bWeaponCantReload = false;
	if (bCheckPrimary && pPrimaryWeapon)
	{
		pWeaponInfo = pPrimaryWeapon->m_pWeaponInfo();
		bWeaponCantReload = (!pWeaponInfo || pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pPrimaryWeapon->m_iPrimaryAmmoType())) && G::SavedWepIds[SLOT_PRIMARY] != TF_WEAPON_PARTICLE_CANNON && G::SavedWepIds[SLOT_PRIMARY] != TF_WEAPON_DRG_POMSON;
		if (pWeaponInfo && !bWeaponCantReload && G::AmmoInSlot[SLOT_PRIMARY].m_iClip < (pWeaponInfo->iMaxClip1 / flDivider))
			return SLOT_PRIMARY;
	}

	bool bFoundPrimaryWepInfo = pWeaponInfo;
	if (bCheckSecondary && pSecondaryWeapon && (bFoundPrimaryWepInfo || !bCheckPrimary))
	{
		pWeaponInfo = pSecondaryWeapon->m_pWeaponInfo();
		bWeaponCantReload = (!pWeaponInfo || pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pSecondaryWeapon->m_iPrimaryAmmoType())) && G::SavedWepIds[SLOT_SECONDARY] != TF_WEAPON_RAYGUN;
		if (pWeaponInfo && !bWeaponCantReload && G::AmmoInSlot[SLOT_SECONDARY].m_iClip < (pWeaponInfo->iMaxClip1 / flDivider))
			return SLOT_SECONDARY;
	}

	return -1;
}
