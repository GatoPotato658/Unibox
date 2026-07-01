#include "NavBotJobs.h"

inline void SortSuppliesByDistance(std::vector<SupplyData_t>& vSupplies, const Vector& vLocalOrigin)
{
	std::sort(vSupplies.begin(), vSupplies.end(), [&](const SupplyData_t& a, const SupplyData_t& b) -> bool
		{
			return a.m_vOrigin.DistTo(vLocalOrigin) < b.m_vOrigin.DistTo(vLocalOrigin);
		});
}

inline PriorityListEnum::PriorityListEnum GetSupplyPriority(int iFlags)
{
	if (iFlags & GetSupplyEnum::Health)
		return iFlags & GetSupplyEnum::LowPrio ? PriorityListEnum::LowPrioGetHealth : PriorityListEnum::GetHealth;

	return PriorityListEnum::GetAmmo;
}

inline SupplyData_t BuildRememberedDispenser(const Vector& vOrigin)
{
	SupplyData_t tRemembered{};
	tRemembered.m_bDispenser = true;
	tRemembered.m_vOrigin = vOrigin;
	return tRemembered;
}

bool CNavBotSupplies::GetSuppliesData(CTFPlayer* pLocal, bool& bClosestTaken, bool bIsAmmo)
{
	if (bIsAmmo)
	{
		// We dont cache ammopacks with physics
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PickupAmmo))
		{
			if (pEntity->IsDormant())
				continue;

			SupplyData_t tData;
			tData.m_vOrigin = pEntity->GetAbsOrigin();
			m_vTempMain.emplace_back(tData);
		}
		m_vTempMain.reserve(m_vTempMain.size() + m_vCachedAmmoOrigins.size());
		m_vTempMain.insert(m_vTempMain.end(), m_vCachedAmmoOrigins.begin(), m_vCachedAmmoOrigins.end());
	}
	else
		m_vTempMain = m_vCachedHealthOrigins;

	if (m_vTempMain.size())
	{
		// Sort by distance, closer is better
		SortSuppliesByDistance(m_vTempMain, pLocal->GetAbsOrigin());

		bClosestTaken = m_vTempMain.front().m_flRespawnTime;
		return true;
	}
	return false;
}

bool CNavBotSupplies::GetDispensersData(CTFPlayer* pLocal)
{
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
	{
		if (pEntity->GetClassID() != ETFClassID::CObjectDispenser)
			continue;

		auto pDispenser = pEntity->As<CObjectDispenser>();
		if (pDispenser->m_bCarried() || pDispenser->m_bHasSapper() || pDispenser->m_bBuilding())
			continue;

		Vector vOrigin;
		if (!F::BotUtils.GetDormantOrigin(pDispenser->entindex(), &vOrigin))
			continue;

		Vec2 vOrigin2D = Vec2(vOrigin.x, vOrigin.y);
		auto pClosestArea = F::NavEngine.FindClosestNavArea(vOrigin);
		if (!pClosestArea)
			continue;

		// This fixes the fact that players can just place dispensers in unreachable locations
		Vector vNearestPoint = pClosestArea->GetNearestPoint(vOrigin2D);
		if (vNearestPoint.DistTo(vOrigin) > 300.f ||
			vOrigin.z - vNearestPoint.z > PLAYER_CROUCHED_JUMP_HEIGHT)
			continue;

		SupplyData_t tData;
		tData.m_bDispenser = true;
		tData.m_vOrigin = vOrigin;
		m_vTempDispensers.emplace_back(tData);
	}
	if (m_vTempDispensers.size())
	{
		// Sort by distance, closer is better
		SortSuppliesByDistance(m_vTempDispensers, pLocal->GetAbsOrigin());
		return true;
	}
	return false;
}

bool CNavBotSupplies::ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchHealth))
		return false;

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::GetHealth)
		return false;

	float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / pLocal->GetMaxHealth();
	bool bAlreadyGettingHealth = F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetHealth || F::NavEngine.m_eCurrentPriority == PriorityListEnum::LowPrioGetHealth;

	if (bAlreadyGettingHealth)
		return flHealthPercent < (bLowPrio ? 0.92f : 0.9f);

	// Check if being gradually healed in any way
	if (pLocal->m_nPlayerCond() & (1 << 21)/*TFCond_Healing*/)
		return false;

	// Get health when below 65%, or below 80% and just patrolling
	return flHealthPercent < 0.64f || bLowPrio && (F::NavEngine.m_eCurrentPriority <= PriorityListEnum::Patrol || F::NavEngine.m_eCurrentPriority == PriorityListEnum::LowPrioGetHealth) && flHealthPercent <= 0.80f;
}

bool CNavBotSupplies::ShouldSearchAmmo(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchAmmo))
		return false;

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::GetAmmo)
		return false;

	bool bAlreadyGettingAmmo = F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetAmmo;

	for (int i = 0; i <= SLOT_MELEE; i++)
	{
		int iActualSlot = G::SavedWepSlots[i];
		if (iActualSlot == SLOT_MELEE || !G::AmmoInSlot[iActualSlot].m_bUsesAmmo)
			continue;

		int iWeaponID = G::SavedWepIds[iActualSlot];
		int iReserveAmmo = G::AmmoInSlot[iActualSlot].m_iReserve;
		if (iReserveAmmo <= (bAlreadyGettingAmmo ? 10 : 5) &&
			(iWeaponID == TF_WEAPON_SNIPERRIFLE ||
			iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC ||
			iWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP))
			return true;

		int iClip = G::AmmoInSlot[iActualSlot].m_iClip;
		int iMaxClip = G::AmmoInSlot[iActualSlot].m_iMaxClip;
		int iMaxReserveAmmo = G::AmmoInSlot[iActualSlot].m_iMaxReserve;
		if (!iMaxReserveAmmo)
			continue;

		const float flClipThreshold = bAlreadyGettingAmmo ? 0.35f : 0.25f;
		const float flReserveCriticalThreshold = bAlreadyGettingAmmo ? 0.35f : 0.25f;
		const float flReserveSkipThreshold = bAlreadyGettingAmmo ? 0.75f : 0.6f;
		const float flReserveSearchThreshold = bAlreadyGettingAmmo ? 0.45f : (1.f / 3.f);

		// If clip and reserve are both very low, definitely get ammo
		if (iMaxClip > 0 && iClip <= iMaxClip * flClipThreshold && iReserveAmmo <= iMaxReserveAmmo * flReserveCriticalThreshold)
			return true;

		// Don't search for ammo if we have enough reserve
		if (iReserveAmmo >= iMaxReserveAmmo * flReserveSkipThreshold)
			continue;

		// Search for ammo if we're low on reserve
		if (iReserveAmmo <= iMaxReserveAmmo * flReserveSearchThreshold)
			return true;
	}

	return false;
}

bool CNavBotSupplies::GetSupply(CUserCmd* pCmd, CTFPlayer* pLocal, Vector vLocalOrigin, SupplyData_t* pSupplyData, const int iPriority)
{
	float flDist = pSupplyData->m_vOrigin.DistTo(vLocalOrigin);
	const auto ePriority = PriorityListEnum::PriorityListEnum(iPriority);
	if (!pSupplyData->m_bDispenser)
	{
		// Check if we are close enough to the pack to pick it up
		if (flDist < 75.0f)
		{
			Vector2D vTo = { pSupplyData->m_vOrigin.x, pSupplyData->m_vOrigin.y };
			Vector vPathPoint = F::NavEngine.GetLocalNavArea()->GetNearestPoint(vTo);
			vPathPoint.z = pSupplyData->m_vOrigin.z;

			// We are close enough to take the pack. Mark as taken
			if (pSupplyData->m_pOriginalSelfPtr && !pSupplyData->m_flRespawnTime && flDist <= 20.f)
				pSupplyData->m_pOriginalSelfPtr->m_flRespawnTime = I::GlobalVars->curtime + 10.f;

			// Try to touch the pack
			SDK::WalkTo(pCmd, pLocal, vPathPoint);
			return true;
		}
	}
	// Stand still if we are close to a dispenser 
	else if (flDist <= 150.f)
	{
		// Keep job priority alive while waiting for dispenser ticks.
		if (F::NavEngine.m_eCurrentPriority != ePriority)
		{
			if (!F::NavEngine.NavTo(pSupplyData->m_vOrigin, ePriority, true, false))
				F::NavEngine.m_eCurrentPriority = ePriority;
		}
		return true;
	}

	return F::NavEngine.NavTo(pSupplyData->m_vOrigin, ePriority, true, flDist > 200.f);
}

void CNavBotSupplies::UpdateTakenState()
{
	float flCurTime = I::GlobalVars->curtime;
	for (auto& pHealthData : m_vCachedHealthOrigins)
	{
		if (pHealthData.m_flRespawnTime < flCurTime)
			pHealthData.m_flRespawnTime = 0.f;
	}
	for (auto& pAmmoData : m_vCachedAmmoOrigins)
	{
		if (pAmmoData.m_flRespawnTime < flCurTime)
			pAmmoData.m_flRespawnTime = 0.f;
	}
}

bool CNavBotSupplies::Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iFlags)
{
	m_vTempMain.clear();
	m_vTempDispensers.clear();
	bool bLowPrio = iFlags & GetSupplyEnum::LowPrio;
	const auto ePriority = GetSupplyPriority(iFlags);

	static bool bWasForce = false;
	bool bShouldForce = iFlags & GetSupplyEnum::Forced;
	bool bIsAmmo = ePriority == PriorityListEnum::GetAmmo;
	const auto eCurrentPriority = F::NavEngine.m_eCurrentPriority;
	const bool bActiveHealthJob = eCurrentPriority == PriorityListEnum::GetHealth || eCurrentPriority == PriorityListEnum::LowPrioGetHealth;
	const bool bActiveSupplyJob = bIsAmmo ? eCurrentPriority == PriorityListEnum::GetAmmo : bActiveHealthJob;

	static Timer tStickySupplyLockTimer{};
	const float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / pLocal->GetMaxHealth();
	const bool bNeedsHealthStill = flHealthPercent < (bLowPrio ? 0.92f : 0.9f);
	const bool bCanKeepStickyLock = bIsAmmo || bNeedsHealthStill;

	// Keep trying the last known dispenser for a short while if dispenser scanning flickers.
	static bool bHasRememberedDispenser = false;
	static Vector vRememberedDispenser = {};
	static Timer tRememberedDispenserTimer{};
	if (!bShouldForce && !(bIsAmmo ? ShouldSearchAmmo(pLocal) : ShouldSearchHealth(pLocal, bLowPrio)))
	{
		if (!bIsAmmo && bHasRememberedDispenser && bNeedsHealthStill && !tRememberedDispenserTimer.Check(2.f))
		{
			auto tRemembered = BuildRememberedDispenser(vRememberedDispenser);
			if (GetSupply(pCmd, pLocal, pLocal->GetAbsOrigin(), &tRemembered, ePriority))
				return true;
		}

		// Cancel pathing if we no longer need to get anything
		if (bActiveSupplyJob && bCanKeepStickyLock && !tStickySupplyLockTimer.Check(1.25f))
			return true;

		if (bActiveSupplyJob && (!bIsAmmo || !bWasForce))
			F::NavEngine.CancelPath();
		return false;
	}
	tStickySupplyLockTimer.Update();

	static Timer tCooldownTimer{};
	if (!bShouldForce && !tCooldownTimer.Check(1.f))
		return bActiveSupplyJob;

	// Already pathing, only try to repath every 2s
	static Timer tRepathCooldownTimer{};
	if (bActiveSupplyJob && !tRepathCooldownTimer.Run(2.f))
		return true;

	UpdateTakenState();
	bWasForce = false;
	bool bClosestSupplyWasTaken = false;
	bool bGotSupplies = GetSuppliesData(pLocal, bClosestSupplyWasTaken, bIsAmmo);
	bool bGotDispensers = GetDispensersData(pLocal);
	if (!bIsAmmo)
	{
		if (bGotDispensers && !m_vTempDispensers.empty())
		{
			bHasRememberedDispenser = true;
			vRememberedDispenser = m_vTempDispensers.front().m_vOrigin;
			tRememberedDispenserTimer.Update();
		}
		else if (bHasRememberedDispenser && bNeedsHealthStill && !tRememberedDispenserTimer.Check(2.f))
		{
			auto tRemembered = BuildRememberedDispenser(vRememberedDispenser);
			if (GetSupply(pCmd, pLocal, pLocal->GetAbsOrigin(), &tRemembered, ePriority))
				return true;
		}
		else if (bHasRememberedDispenser && (tRememberedDispenserTimer.Check(2.f) || !bNeedsHealthStill))
			bHasRememberedDispenser = false;
	}
	if (!bGotSupplies && !bGotDispensers)
	{
		if (bActiveSupplyJob && bCanKeepStickyLock && !tStickySupplyLockTimer.Check(1.25f))
			return true;

		tCooldownTimer.Update();
		return false;
	}

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	bool bHasCloseDispenser = false;
	if (bGotDispensers)
	{
		bHasCloseDispenser = true;
		m_vTempMain.reserve(m_vTempMain.size() + m_vTempDispensers.size());
		m_vTempMain.insert(m_vTempMain.end(), m_vTempDispensers.begin(), m_vTempDispensers.end());
		SortSuppliesByDistance(m_vTempMain, vLocalOrigin);
	}

	SupplyData_t* pBest = nullptr, * pSecondBest = nullptr;
	if (bClosestSupplyWasTaken)
	{
		for (auto& pSupplyData : m_vTempMain)
		{
			if (pSupplyData.m_flRespawnTime)
				continue;

			if (pBest)
			{
				pSecondBest = &pSupplyData;
				break;
			}
			pBest = &pSupplyData;
		}
	}

	if (!pBest)
	{
		pBest = &m_vTempMain.front();
		if (bHasCloseDispenser)
		{
			if (bClosestSupplyWasTaken)
				pBest = &m_vTempDispensers.front();
		}
		else if (m_vTempMain.size() > 1)
			pSecondBest = &m_vTempMain.at(1);
	}

	// Check if going for the other one will take less time
	if (pSecondBest)
	{
		float flFirstTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, pBest->m_vOrigin);
		float flSecondTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, pSecondBest->m_vOrigin);
		if (flSecondTargetCost < flFirstTargetCost)
			pBest = pSecondBest;
	}

	if (pBest && GetSupply(pCmd, pLocal, vLocalOrigin, pBest, ePriority))
	{
		bWasForce = bShouldForce;
		tStickySupplyLockTimer.Update();
		return true;
	}

	tCooldownTimer.Update();
	return false;
}

void CNavBotSupplies::AddCachedSupplyOrigin(Vector vOrigin, bool bHealth)
{
	SupplyData_t tData;
	tData.m_vOrigin = vOrigin;
	if (bHealth)
	{
		m_vCachedHealthOrigins.push_back(tData);
		m_vCachedHealthOrigins.back().m_pOriginalSelfPtr = &m_vCachedHealthOrigins.back();
	}
	else
	{
		m_vCachedAmmoOrigins.push_back(tData);
		m_vCachedAmmoOrigins.back().m_pOriginalSelfPtr = &m_vCachedAmmoOrigins.back();
	}
}

void CNavBotSupplies::ResetCachedOrigins()
{
	m_vCachedHealthOrigins.clear();
	m_vCachedAmmoOrigins.clear();
}

void CNavBotSupplies::ResetTemp()
{
	m_vTempMain.clear();
	m_vTempDispensers.clear();
}
