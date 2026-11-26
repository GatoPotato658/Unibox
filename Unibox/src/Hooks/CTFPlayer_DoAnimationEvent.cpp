#ifdef TEXTMODE
#include "../SDK/SDK.h"


MAKE_HOOK(CTFPlayer_DoAnimationEvent, S::CTFPlayer_DoAnimationEvent(), void,
	void* rcx, PlayerAnimEvent_t event, int nData)
{
	DEBUG_RETURN(CTFPlayer_DoAnimationEvent, rcx, event, nData);
}
#endif