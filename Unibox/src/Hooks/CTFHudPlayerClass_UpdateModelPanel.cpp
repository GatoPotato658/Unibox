#ifdef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CTFHudPlayerClass_UpdateModelPanel, "client.dll", "40 57 48 83 EC ? 80 B9 ? ? ? ? ? 48 8B F9 0F 84 ? ? ? ? 4C 89 74 24", 0x0);

MAKE_HOOK(CTFHudPlayerClass_UpdateModelPanel, S::CTFHudPlayerClass_UpdateModelPanel(), void,
	void* rcx)
{
}
#endif