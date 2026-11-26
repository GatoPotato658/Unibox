#include "../SDK/SDK.h"


MAKE_HOOK(CTFPlayer_AvoidPlayers, S::CTFPlayer_AvoidPlayers(), void,
	void* rcx, CUserCmd* pCmd)
{
	DEBUG_RETURN(CTFPlayer_AvoidPlayers, rcx, pCmd);

	if (Vars::Misc::Movement::NoPush.Value)
		return;

	CALL_ORIGINAL(rcx, pCmd);
}