#include "../SDK/SDK.h"


MAKE_HOOK(CTFRocketLauncher_CheckReloadMisfire, S::CTFRocketLauncher_CheckReloadMisfire(), bool,
	void* rcx)
{
	DEBUG_RETURN(CTFRocketLauncher_CheckReloadMisfire, rcx);

	auto pWeapon = reinterpret_cast<CTFWeaponBase*>(rcx);
	if (!SDK::AttribHookValue(0, "can_overload", pWeapon))
		return false;

	auto pOwner = pWeapon->m_hOwner()->As<CTFPlayer>();
	int iClip1 = pWeapon->m_iClip1();
	if (pWeapon->m_bRemoveable()) // just using this var since it's in the datamap and doesn't seem to be used on the client
	{
		if (iClip1 > 0)
		{
			pWeapon->CalcIsAttackCritical();
			return true;
		}
		else
			pWeapon->m_bRemoveable() = false;
	}
	else if (iClip1 >= pWeapon->GetMaxClip1() || iClip1 > 0 && pOwner && pOwner->GetAmmoCount(pWeapon->m_iPrimaryAmmoType()) == 0)
	{
		pWeapon->CalcIsAttackCritical();
		pWeapon->m_bRemoveable() = true;
		return true;
	}

	return false;
}

MAKE_HOOK(CTFRocketLauncher_FireProjectile, S::CTFRocketLauncher_FireProjectile(), CBaseEntity*,
	void* rcx, CTFPlayer* pPlayer)
{
	DEBUG_RETURN(CTFRocketLauncher_FireProjectile, rcx, pPlayer);

	auto pWeapon = reinterpret_cast<CTFWeaponBase*>(rcx);
	pWeapon->m_bRemoveable() = false;
	return CALL_ORIGINAL(rcx, pPlayer);
}

// more accurate way to track this? either way doesn't matter too much
/*

MAKE_HOOK(CTFBat_Wood_LaunchBall, S::CTFBat_Wood_LaunchBall(), void,
	void* rcx)
{
	DEBUG_RETURN(CTFBat_Wood_LaunchBall, rcx);

	auto pWeapon = reinterpret_cast<CTFWeaponBase*>(rcx);
	pWeapon->CalcIsAttackCritical();

	return CALL_ORIGINAL(rcx);
}
*/