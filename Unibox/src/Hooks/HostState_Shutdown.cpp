#include "../SDK/SDK.h"

#include "../Core/Core.h"


MAKE_HOOK(HostState_Shutdown, S::HostState_Shutdown(), void)
{
	DEBUG_RETURN(HostState_Shutdown);

	G::Unload = true;
	CALL_ORIGINAL();
	U::Core.m_bUnload = true;
}

MAKE_HOOK(HostState_Restart, S::HostState_Restart(), void)
{
	DEBUG_RETURN(HostState_Restart);

	G::Unload = true;
	CALL_ORIGINAL();
	U::Core.m_bUnload = true;
}
