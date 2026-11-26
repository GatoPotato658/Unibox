#include "../SDK/SDK.h"


MAKE_HOOK(CTFPlayer_GetMinFOV, S::CTFPlayer_GetMinFOV(), float,
	/*void* rcx*/)
{
	DEBUG_RETURN(CTFPlayer_GetMinFOV, /*rcx*/);

	return 0.f;
}