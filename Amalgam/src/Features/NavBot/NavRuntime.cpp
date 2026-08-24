#include "NavRuntime.h"

namespace NavRuntime
{
	bool IsMovementLocked(CTFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive())
		return true;

	if (pLocal->m_fFlags() & FL_FROZEN)
		return true;

	if (pLocal->InCond(TF_COND_STUNNED) && (pLocal->m_iStunFlags() & (TF_STUN_CONTROLS | TF_STUN_LOSER_STATE)))
		return true;

	if (pLocal->IsTaunting() && !pLocal->m_bAllowMoveDuringTaunt())
		return true;

	const auto pGameRules = I::TFGameRules();
	if (!pGameRules)
		return false;

	const bool bMvM = pGameRules->m_bPlayingMannVsMachine();
	if (bMvM)
	{
		auto pObjRes = H::Entities.GetObjectiveResource();
		const bool bBetween = pObjRes && pObjRes->m_bMannVsMachineBetweenWaves();
		if (bBetween)
			return false;
		if (pGameRules->m_iRoundState() == GR_STATE_PREROUND || pGameRules->m_iRoundState() == GR_STATE_BETWEEN_RNDS)
			return false;
		if (pGameRules->m_bInWaitingForPlayers())
			return false;
		return false;
	}

	if (pGameRules->m_bInWaitingForPlayers())
		return true;

	const int iRoundState = pGameRules->m_iRoundState();
	if (iRoundState == GR_STATE_PREROUND || iRoundState == GR_STATE_BETWEEN_RNDS)
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
		if (!pLocal || !pWeapon || pLocal->m_fFlags() & FL_INWATER)
			return true;

		if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
		{
			const int iState = pWeapon->As<CTFMinigun>()->m_iWeaponState();
			if (iState == AC_STATE_STARTFIRING || iState == AC_STATE_FIRING || iState == AC_STATE_SPINNING || (G::LastUserCmd && (G::LastUserCmd->buttons & IN_ATTACK2)))
				return false;
		}

		return pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC
			? !pWeapon->As<CTFSniperRifleClassic>()->m_bCharging()
			: !pLocal->InCond(TF_COND_ZOOMED);
	}
}
