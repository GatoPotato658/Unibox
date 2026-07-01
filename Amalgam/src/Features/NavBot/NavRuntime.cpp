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
		return iState == AC_STATE_STARTFIRING || iState == AC_STATE_FIRING || iState == AC_STATE_SPINNING || (G::LastUserCmd->buttons & IN_ATTACK2);
	}
}
