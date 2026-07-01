#pragma once
#include "../../SDK/SDK.h"

namespace NavRuntime
{
	bool IsMovementLocked(CTFPlayer* pLocal);
	bool IsMinigunJumpLocked(CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	inline bool CanIssueNavJump(CTFWeaponBase* pWeapon, CUserCmd* pCmd) { return !IsMinigunJumpLocked(pWeapon, pCmd); }
}
