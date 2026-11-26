#ifndef TEXTMODE
#include "../SDK/SDK.h"


MAKE_HOOK(ClientModeTFNormal_BIsFriendOrPartyMember, S::ClientModeTFNormal_BIsFriendOrPartyMember(), bool,
	void* rcx, CBaseEntity* pEntity)
{
	DEBUG_RETURN(ClientModeTFNormal_BIsFriendOrPartyMember, rcx, pEntity);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CHudInspectPanel_UserCmd_InspectTarget_BIsFriendOrPartyMember_Call();

	if (dwRetAddr == dwDesired && Vars::Misc::MannVsMachine::AllowInspect.Value)
		return true;

	return CALL_ORIGINAL(rcx, pEntity);
}
#endif
