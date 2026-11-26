#include "../SDK/SDK.h"


MAKE_HOOK(CTFPlayerShared_ShouldSuppressPrediction, S::CTFPlayerShared_ShouldSuppressPrediction(), bool,
	void* rcx)
{
	DEBUG_RETURN(CTFPlayerShared_ShouldSuppressPrediction, rcx);

	return false;
}