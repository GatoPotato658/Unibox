#include "../SDK/SDK.h"

#include "../Core/Core.h"

MAKE_SIGNATURE(HostState_Shutdown, "engine.dll", "48 83 EC ? 80 3D ? ? ? ? ? 74 ? 48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 50 ? 84 C0", 0x0);
MAKE_SIGNATURE(HostState_Restart, "engine.dll", "C7 05 ? ? ? ? ? ? ? ? C3 CC CC CC CC CC 48 83 EC", 0x0);

MAKE_HOOK(HostState_Shutdown, S::HostState_Shutdown(), void)
{
	DEBUG_RETURN(HostState_Shutdown);

	U::Core.m_bUnload = G::Unload = true;
	CALL_ORIGINAL();
}

MAKE_HOOK(HostState_Restart, S::HostState_Restart(), void)
{
	DEBUG_RETURN(HostState_Restart);

	U::Core.m_bUnload = G::Unload = true;
	CALL_ORIGINAL();
}