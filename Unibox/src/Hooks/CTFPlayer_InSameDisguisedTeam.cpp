#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"


MAKE_HOOK(CTFPlayer_InSameDisguisedTeam, S::CTFPlayer_InSameDisguisedTeam(), bool,
	void* rcx, CBaseEntity* pEnt)
{
	DEBUG_RETURN(CTFPlayer_InSameDisguisedTeam, rcx, pEnt);

	if (F::Spectate.HasTarget())
		return true;

	return CALL_ORIGINAL(rcx, pEnt);
}

MAKE_HOOK(CTFFreezePanel_ShouldDraw, S::CTFFreezePanel_ShouldDraw(), bool,
	void* rcx)
{
	DEBUG_RETURN(CTFFreezePanel_ShouldDraw, rcx);

	if (F::Spectate.HasTarget())
		return false;

	return CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CTFFreezePanel_FireGameEvent, S::CTFFreezePanel_FireGameEvent(), void,
	void* rcx, IGameEvent* event)
{
	DEBUG_RETURN(CTFFreezePanel_FireGameEvent, rcx, event);

	if (F::Spectate.HasTarget())
		return;

	CALL_ORIGINAL(rcx, event);
}
#endif