#include "../SDK/SDK.h"

#include "../Features/CritHack/CritHack.h"


MAKE_HOOK(CTFWeaponBase_CanFireRandomCriticalShot, S::CTFWeaponBase_CanFireRandomCriticalShot(), bool,
	void* rcx, float flCritChance)
{	// not present on the client so it will always be a crit behind otherwise
	DEBUG_RETURN(CTFWeaponBase_CanFireRandomCriticalShot, rcx, flCritChance);

	int nRandomRangedCritDamage = F::CritHack.GetCritDamage();
	int nTotalDamage = F::CritHack.GetRangedDamage();
	if (!nTotalDamage)
		return true;

	auto pWeapon = reinterpret_cast<CTFWeaponBase*>(rcx);
	float flNormalizedDamage = nRandomRangedCritDamage / TF_DAMAGE_CRIT_MULTIPLIER;
	pWeapon->m_flObservedCritChance() = flNormalizedDamage / (flNormalizedDamage + (nTotalDamage - nRandomRangedCritDamage));

	return CALL_ORIGINAL(rcx, flCritChance);
}