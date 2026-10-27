#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"

MAKE_SIGNATURE(CTFPlayer_InSameDisguisedTeam, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B DA 48 8B F9 48 85 D2 75 ? 32 C0", 0x0);
MAKE_SIGNATURE(CTFFreezePanel_ShouldDraw, "client.dll", "48 81 C1 ? ? ? ? 48 8B 01 48 FF A0 ? ? ? ? CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC 40 55", 0x0);
MAKE_SIGNATURE(CTFFreezePanel_FireGameEvent, "client.dll", "40 55 53 56 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 02", 0x0);

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