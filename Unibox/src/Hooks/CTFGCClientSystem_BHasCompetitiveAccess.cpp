#include "../SDK/SDK.h"

MAKE_SIGNATURE(CTFGCClientSystem_BHasCompetitiveAccess, "client.dll", "48 83 EC ? E8 ? ? ? ? 48 8B C8 48 8B 10 FF 92 ? ? ? ? 48 85 C0 74 ? 48 8B 88 ? ? ? ? 48 85 C9 74 ? BA ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 83 78 ? ? 75 ? 48 8B 40 ? 48 8B 08 48 85 C9 74 ? 8B 41 ? C1 E8 ? A8 ? 74 ? 80 79 ? ? 74 ? B0 ? 48 83 C4 ? C3 32 C0 48 83 C4 ? C3 CC CC CC CC CC CC CC CC CC CC CC 40 53", 0x0);

MAKE_HOOK(CTFGCClientSystem_BHasCompetitiveAccess, S::CTFGCClientSystem_BHasCompetitiveAccess(), bool)
{
	return true;
}