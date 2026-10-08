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
	static Timer tReloadRunCooldown{};

	if (!HasTask() || !(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunReload)
			F::NavEngine.CancelPath();
		return false;
	}

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::RunReload)
		return false;

	if (!tReloadRunCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunReload;

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunReload && F::NavEngine.IsPathing())
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
	static Timer tReloadRunCooldown{};

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || !HasTask())
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload)
			F::NavEngine.CancelPath();
		return false;
	}

	if (!tReloadRunCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload;

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload && F::NavEngine.IsPathing())
		return true;

	Vector vHideFrom;
	auto pCrumbs = F::NavEngine.GetCrumbs();
	if (pCrumbs->size() > 4)
		vHideFrom = pCrumbs->at(4).m_vPos;
	else if (F::BotUtils.m_tClosestEnemy.m_pPlayer)
		vHideFrom = F::BotUtils.m_tClosestEnemy.m_vOrigin;
	else
		return false;

	vHideFrom.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	return TryNavToHiddenSpot(vHideFrom, PriorityListEnum::RunSafeReload);
}

int CNavBotReload::GetReloadWeaponSlot(CTFPlayer* pLocal, const ClosestEnemy_t& tClosestEnemy)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || F::VSHController.IsBossLocal())
		return -1;

	if (G::Reloading && F::BotUtils.m_iCurrentSlot >= SLOT_PRIMARY && F::BotUtils.m_iCurrentSlot <= SLOT_SECONDARY)
		return F::BotUtils.m_iCurrentSlot;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Capture)
		return -1;

	if ((F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear && tClosestEnemy.m_flDist <= 500.f) || tClosestEnemy.m_flDist <= 250.f)
		return -1;

	const float flDivider = F::NavEngine.m_eCurrentPriority < PriorityListEnum::StayNear && tClosestEnemy.m_flDist > 500.f ? 1.f : 3.f;

	const auto SlotNeedsReload = [&](int iSlot, bool bIgnoreReserve) -> bool
		{
			if (SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[iSlot], G::SavedDefIndexes[iSlot], false))
				return false;

			auto pSlotWeapon = pLocal->GetWeaponFromSlot(iSlot);
			auto pWeaponInfo = pSlotWeapon ? pSlotWeapon->m_pWeaponInfo() : nullptr;
			if (!pWeaponInfo)
				return false;

			if (!bIgnoreReserve && (pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pSlotWeapon->m_iPrimaryAmmoType())))
				return false;

			return G::AmmoInSlot[iSlot].m_iClip < pWeaponInfo->iMaxClip1 / flDivider;
		};

	if (SlotNeedsReload(SLOT_PRIMARY, G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_PARTICLE_CANNON || G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_DRG_POMSON))
		return SLOT_PRIMARY;
	if (SlotNeedsReload(SLOT_SECONDARY, G::SavedWepIds[SLOT_SECONDARY] == TF_WEAPON_RAYGUN))
		return SLOT_SECONDARY;

	return -1;
}
