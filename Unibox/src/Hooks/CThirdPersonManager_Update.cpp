#include "../SDK/SDK.h"


MAKE_HOOK(CThirdPersonManager_Update, S::CThirdPersonManager_Update(), void,
	void* rcx)
{
	DEBUG_RETURN(CThirdPersonManager_Update, rcx);
}