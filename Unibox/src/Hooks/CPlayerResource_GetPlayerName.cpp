#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Players/PlayerUtils.h"


MAKE_HOOK(CPlayerResource_GetPlayerName, S::CPlayerResource_GetPlayerName(), const char*,
	void* rcx, int iIndex)
{
	DEBUG_RETURN(CPlayerResource_GetPlayerName, rcx, iIndex);

	return F::PlayerUtils.GetPlayerName(iIndex, CALL_ORIGINAL(rcx, iIndex));
}
#endif