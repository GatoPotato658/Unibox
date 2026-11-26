#include "../SDK/SDK.h"


MAKE_HOOK(CTFPlayerInventory_GetMaxItemCount, S::CTFPlayerInventory_GetMaxItemCount(), int,
	void* rcx)
{
	DEBUG_RETURN(CTFPlayerInventory_GetMaxItemCount, rcx);

#ifdef TEXTMODE
	return 4000;
#else
	if (Vars::Misc::Exploits::BackpackExpander.Value)
		return 4000;

	return CALL_ORIGINAL(rcx);
#endif
}