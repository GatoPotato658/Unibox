#ifndef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(g_bAllowSecureServers, "engine.dll", "40 88 35 ? ? ? ? 40 84 FF", 0x0);
MAKE_SIGNATURE(Host_IsSecureServerAllowed, "engine.dll", "48 83 EC ? FF 15 ? ? ? ? 48 8D 15 ? ? ? ? 48 8B C8 4C 8B 00 41 FF 50 ? 85 C0 75", 0x0);

MAKE_HOOK(Host_IsSecureServerAllowed, S::Host_IsSecureServerAllowed(), bool)
{
	DEBUG_RETURN(Host_IsSecureServerAllowed);

	if (Vars::Misc::Game::InsecureBypass.Value)
		return *reinterpret_cast<bool*>(U::Memory.RelToAbs(S::g_bAllowSecureServers())) = true;
	return CALL_ORIGINAL();
}
#endif 