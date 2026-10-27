#include "../SDK/SDK.h"

MAKE_SIGNATURE(CTFInventoryManager_ShowItemsPickedUp, "client.dll", "44 88 4C 24 ? 44 88 44 24 ? 53 41 56", 0x0);

MAKE_HOOK(CTFInventoryManager_ShowItemsPickedUp, S::CTFInventoryManager_ShowItemsPickedUp(), bool,
	void* rcx, bool bForce, bool bReturnToGame, bool bNoPanel)
{
	DEBUG_RETURN(CTFInventoryManager_ShowItemsPickedUp, rcx, bForce, bReturnToGame, bNoPanel);

#ifndef TEXTMODE
	if (Vars::Misc::Automation::AcceptItemDrops.Value)
#endif
	{
		CALL_ORIGINAL(rcx, true, true, true);
		return false;
	}
	return CALL_ORIGINAL(rcx, bForce, bReturnToGame, bNoPanel);
}