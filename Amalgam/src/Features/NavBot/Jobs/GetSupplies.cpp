#include "NavBotJobs.h"

#include <algorithm>

static void SortSuppliesByDistance(std::vector<SupplyData_t>& vSupplies, const Vector& vLocalOrigin)
{
	std::sort(vSupplies.begin(), vSupplies.end(), [&](const SupplyData_t& a, const SupplyData_t& b) -> bool
		{
			return a.m_vOrigin.DistToSqr(vLocalOrigin) < b.m_vOrigin.DistToSqr(vLocalOrigin);
		});
}

static PriorityListEnum::PriorityListEnum GetSupplyPriority(int iFlags)
{
	if (iFlags & GetSupplyEnum::Health)
		return iFlags & GetSupplyEnum::LowPrio ? PriorityListEnum::LowPrioGetHealth : PriorityListEnum::GetHealth;

	return PriorityListEnum::GetAmmo;
}

static SupplyData_t BuildRememberedDispenser(const Vector& vOrigin)
{
	SupplyData_t tRemembered{};
	tRemembered.m_bDispenser = true;
	tRemembered.m_vOrigin = vOrigin;
	return tRemembered;
}

bool CNavBotSupplies::GetSuppliesData(CTFPlayer* pLocal, bool& bClosestTaken, bool bAmmo)
{
	if (bAmmo)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PickupAmmo))
		{
			if (pEntity->IsDormant())
				continue;

			SupplyData_t tData;
			tData.m_vOrigin = pEntity->GetAbsOrigin();
			m_vTempMain.emplace_back(tData);
		}
		m_vTempMain.insert(m_vTempMain.end(), m_vCachedAmmoOrigins.begin(), m_vCachedAmmoOrigins.end());
	}
	else
		m_vTempMain = m_vCachedHealthOrigins;

	if (m_vTempMain.empty())
		return false;

	SortSuppliesByDistance(m_vTempMain, pLocal->GetAbsOrigin());
	bClosestTaken = m_vTempMain.front().m_flRespawnTime != 0.f;
	return true;
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

		auto pClosestArea = F::NavEngine.FindClosestNavArea(vOrigin);
		if (!pClosestArea)
			continue;

		Vector vNearestPoint = pClosestArea->GetNearestPoint(Vec2(vOrigin.x, vOrigin.y));
		if (vNearestPoint.DistTo(vOrigin) > 300.f ||
			vOrigin.z - vNearestPoint.z > PLAYER_CROUCHED_JUMP_HEIGHT)
			continue;

		SupplyData_t tData;
		tData.m_bDispenser = true;
		tData.m_vOrigin = vOrigin;
		m_vTempDispensers.emplace_back(tData);
	}

	if (m_vTempDispensers.empty())
		return false;

	SortSuppliesByDistance(m_vTempDispensers, pLocal->GetAbsOrigin());
	return true;
}

bool CNavBotSupplies::ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio)
{
	if (!NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::SearchHealth))
		return false;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::GetHealth)
		return false;

	const float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
	const bool bAlreadyGettingHealth = F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetHealth || F::NavEngine.m_eCurrentPriority == PriorityListEnum::LowPrioGetHealth;

	if (bAlreadyGettingHealth)
		return flHealthPercent < (bLowPrio ? NavJobTuning::HEALTH_RESUME_LOW_PRIO : NavJobTuning::HEALTH_RESUME);

	if (NavJobUtils::IsBeingHealed(pLocal))
		return false;

	if (flHealthPercent < NavJobTuning::HEALTH_START)
		return true;

	return bLowPrio && F::NavEngine.m_eCurrentPriority <= PriorityListEnum::Patrol && flHealthPercent <= NavJobTuning::HEALTH_START_LOW_PRIO;
}

float CNavBotSupplies::GetAmmoNeed(bool bActive) const
{
	float flNeed = 0.f;
	for (int i = 0; i <= SLOT_PDA2; i++)
	{
		const int iActualSlot = G::SavedWepSlots[i];
		if ((iActualSlot != SLOT_PRIMARY && iActualSlot != SLOT_SECONDARY) || !G::AmmoInSlot[iActualSlot].m_bUsesAmmo)
			continue;

		const int iWeaponID = G::SavedWepIds[iActualSlot];
		const int iReserveAmmo = G::AmmoInSlot[iActualSlot].m_iReserve;
		if (iReserveAmmo <= (bActive ? 10 : 5) &&
			(iWeaponID == TF_WEAPON_SNIPERRIFLE ||
			iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC ||
			iWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP))
		{
			flNeed = std::max(flNeed, 760.f);
			continue;
		}

		const int iClip = G::AmmoInSlot[iActualSlot].m_iClip;
		const int iMaxClip = G::AmmoInSlot[iActualSlot].m_iMaxClip;
		const int iMaxReserveAmmo = G::AmmoInSlot[iActualSlot].m_iMaxReserve;
		if (!iMaxReserveAmmo)
			continue;

		const float flClipThreshold = bActive ? 0.35f : 0.25f;
		const float flReserveCriticalThreshold = bActive ? 0.35f : 0.25f;
		const float flReserveSearchThreshold = bActive ? 0.45f : (1.f / 3.f);

		if (iMaxClip > 0 &&
			iClip <= iMaxClip * flClipThreshold &&
			iReserveAmmo <= iMaxReserveAmmo * flReserveCriticalThreshold)
		{
			flNeed = std::max(flNeed, 700.f);
			continue;
		}

		if (iReserveAmmo <= iMaxReserveAmmo * flReserveSearchThreshold)
		{
			const float flReserveRatio = 1.f - static_cast<float>(iReserveAmmo) / iMaxReserveAmmo;
			flNeed = std::max(flNeed, 520.f + flReserveRatio * 180.f);
		}
	}

	return flNeed;
}

bool CNavBotSupplies::ShouldSearchAmmo(CTFPlayer* pLocal)
{
	if (!NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::SearchAmmo))
		return false;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::GetAmmo)
		return false;

	return GetAmmoNeed(F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetAmmo) > 0.f;
}

bool CNavBotSupplies::GetSupply(CUserCmd* pCmd, CTFPlayer* pLocal, Vector vLocalOrigin, SupplyData_t* pSupplyData, PriorityListEnum::PriorityListEnum ePriority)
{
	const float flDist = pSupplyData->m_vOrigin.DistTo(vLocalOrigin);
	if (!pSupplyData->m_bDispenser)
	{
		if (flDist < 75.0f)
		{
			CNavArea* pLocalArea = F::NavEngine.GetLocalNavArea(vLocalOrigin);
			if (!pLocalArea)
				return false;

			Vector vPathPoint = pLocalArea->GetNearestPoint(Vec2(pSupplyData->m_vOrigin.x, pSupplyData->m_vOrigin.y));
			vPathPoint.z = pSupplyData->m_vOrigin.z;

			if (!pSupplyData->m_flRespawnTime && flDist <= 20.f)
			{
				auto& vCache = pSupplyData->m_bHealthCache ? m_vCachedHealthOrigins : m_vCachedAmmoOrigins;
				const int iIndex = pSupplyData->m_iCacheIndex;
				if (iIndex >= 0 && iIndex < static_cast<int>(vCache.size()))
					vCache[iIndex].m_flRespawnTime = I::GlobalVars->curtime + 10.f;
			}

			SDK::WalkTo(pCmd, pLocal, vPathPoint);
			return true;
		}
	}
	else if (flDist <= 150.f)
	{
		if (F::NavEngine.m_eCurrentPriority != ePriority && !F::NavEngine.NavTo(pSupplyData->m_vOrigin, ePriority))
			return false;

		return true;
	}

	return F::NavEngine.NavTo(pSupplyData->m_vOrigin, ePriority);
}

void CNavBotSupplies::UpdateTakenState()
{
	const float flCurTime = I::GlobalVars->curtime;
	for (auto& tHealthData : m_vCachedHealthOrigins)
	{
		if (tHealthData.m_flRespawnTime < flCurTime)
			tHealthData.m_flRespawnTime = 0.f;
	}
	for (auto& tAmmoData : m_vCachedAmmoOrigins)
	{
		if (tAmmoData.m_flRespawnTime < flCurTime)
			tAmmoData.m_flRespawnTime = 0.f;
	}
}

bool CNavBotSupplies::Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iFlags)
{
	m_vTempMain.clear();
	m_vTempDispensers.clear();
	const bool bLowPrio = iFlags & GetSupplyEnum::LowPrio;
	const bool bShouldForce = iFlags & GetSupplyEnum::Forced;
	const auto ePriority = GetSupplyPriority(iFlags);
	const bool bIsAmmo = ePriority == PriorityListEnum::GetAmmo;

	const auto eCurrentPriority = F::NavEngine.m_eCurrentPriority;
	const bool bActiveHealthJob = eCurrentPriority == PriorityListEnum::GetHealth || eCurrentPriority == PriorityListEnum::LowPrioGetHealth;
	const bool bActiveSupplyJob = bIsAmmo ? eCurrentPriority == PriorityListEnum::GetAmmo : bActiveHealthJob;

	const float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
	const bool bNeedsHealthStill = flHealthPercent < (bLowPrio ? NavJobTuning::HEALTH_RESUME_LOW_PRIO : NavJobTuning::HEALTH_RESUME);
	const bool bCanKeepStickyLock = bIsAmmo || bNeedsHealthStill;

	if (!bShouldForce && !(bIsAmmo ? ShouldSearchAmmo(pLocal) : ShouldSearchHealth(pLocal, bLowPrio)))
	{
		if (!bIsAmmo && m_bHasRememberedDispenser && bNeedsHealthStill && !m_tRememberedDispenser.Check(2.f))
		{
			auto tRemembered = BuildRememberedDispenser(m_vRememberedDispenser);
			if (GetSupply(pCmd, pLocal, pLocal->GetAbsOrigin(), &tRemembered, ePriority))
				return true;
		}

		if (bActiveSupplyJob && bCanKeepStickyLock && !m_tStickyLock.Check(1.25f))
			return true;

		if (bActiveSupplyJob && (!bIsAmmo || !m_bWasForce))
			F::NavEngine.CancelPath();
		return false;
	}
	m_tStickyLock.Update();

	if (!bShouldForce && !m_tCooldown.Check(1.f))
		return bActiveSupplyJob;

	if (bActiveSupplyJob && !m_tRepathCooldown.Run(2.f))
		return true;

	UpdateTakenState();
	m_bWasForce = false;
	bool bClosestSupplyWasTaken = false;
	const bool bGotSupplies = GetSuppliesData(pLocal, bClosestSupplyWasTaken, bIsAmmo);
	const bool bGotDispensers = GetDispensersData(pLocal);
	if (!bIsAmmo)
	{
		if (bGotDispensers)
		{
			m_bHasRememberedDispenser = true;
			m_vRememberedDispenser = m_vTempDispensers.front().m_vOrigin;
			m_tRememberedDispenser.Update();
		}
		else if (m_bHasRememberedDispenser && bNeedsHealthStill && !m_tRememberedDispenser.Check(2.f))
		{
			auto tRemembered = BuildRememberedDispenser(m_vRememberedDispenser);
			if (GetSupply(pCmd, pLocal, pLocal->GetAbsOrigin(), &tRemembered, ePriority))
				return true;
		}
		else if (m_bHasRememberedDispenser && (m_tRememberedDispenser.Check(2.f) || !bNeedsHealthStill))
			m_bHasRememberedDispenser = false;
	}
	if (!bGotSupplies && !bGotDispensers)
	{
		if (bActiveSupplyJob && bCanKeepStickyLock && !m_tStickyLock.Check(1.25f))
			return true;

		m_tCooldown.Update();
		return false;
	}

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	if (bGotDispensers)
	{
		m_vTempMain.insert(m_vTempMain.end(), m_vTempDispensers.begin(), m_vTempDispensers.end());
		SortSuppliesByDistance(m_vTempMain, vLocalOrigin);
	}

	SupplyData_t* pBest = nullptr, * pSecondBest = nullptr;
	if (bClosestSupplyWasTaken)
	{
		for (auto& tSupplyData : m_vTempMain)
		{
			if (tSupplyData.m_flRespawnTime)
				continue;

			if (pBest)
			{
				pSecondBest = &tSupplyData;
				break;
			}
			pBest = &tSupplyData;
		}
	}

	if (!pBest)
	{
		pBest = &m_vTempMain.front();
		if (bGotDispensers)
		{
			if (bClosestSupplyWasTaken)
				pBest = &m_vTempDispensers.front();
		}
		else if (m_vTempMain.size() > 1)
			pSecondBest = &m_vTempMain.at(1);
	}

	if (pSecondBest)
	{
		const float flFirstTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, pBest->m_vOrigin);
		const float flSecondTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, pSecondBest->m_vOrigin);
		if (flSecondTargetCost < flFirstTargetCost)
			pBest = pSecondBest;
	}

	if (GetSupply(pCmd, pLocal, vLocalOrigin, pBest, ePriority))
	{
		m_bWasForce = bShouldForce;
		m_tStickyLock.Update();
		return true;
	}

	m_tCooldown.Update();
	return false;
}

void CNavBotSupplies::AddCachedSupplyOrigin(Vector vOrigin, bool bHealth)
{
	auto& vCache = bHealth ? m_vCachedHealthOrigins : m_vCachedAmmoOrigins;

	SupplyData_t tData;
	tData.m_vOrigin = vOrigin;
	tData.m_iCacheIndex = static_cast<int>(vCache.size());
	tData.m_bHealthCache = bHealth;
	vCache.push_back(tData);
}

void CNavBotSupplies::ResetCachedOrigins()
{
	m_vCachedHealthOrigins.clear();
	m_vCachedAmmoOrigins.clear();
}

void CNavBotSupplies::Reset()
{
	m_vTempMain.clear();
	m_vTempDispensers.clear();
	m_bWasForce = false;
	m_bHasRememberedDispenser = false;
	m_tRememberedDispenser = Timer();
	m_tStickyLock = Timer();
	m_tCooldown = Timer();
	m_tRepathCooldown = Timer();
}
