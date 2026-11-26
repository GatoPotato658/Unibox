#include "../SDK/SDK.h"


MAKE_HOOK(CL_CheckForPureServerWhitelist, S::CL_CheckForPureServerWhitelist(), void,
	void **pFilesToReload)
{
	DEBUG_RETURN(CL_CheckForPureServerWhitelist, pFilesToReload);

	if (Vars::Misc::Exploits::PureBypass.Value)
		return;

	CALL_ORIGINAL(pFilesToReload);
}