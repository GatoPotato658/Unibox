#ifndef TEXTMODE
#include "../SDK/SDK.h"


MAKE_HOOK(Host_IsSecureServerAllowed, S::Host_IsSecureServerAllowed(), bool)
{
	DEBUG_RETURN(Host_IsSecureServerAllowed);

	if (Vars::Misc::Game::InsecureBypass.Value)
		return *reinterpret_cast<bool*>(U::Memory.RelToAbs(S::g_bAllowSecureServers())) = true;
	return CALL_ORIGINAL();
}
#endif 