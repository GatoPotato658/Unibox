#include "../SDK/SDK.h"

MAKE_SIGNATURE(CTFParty_BAnyMemberWithoutTicket, "client.dll", "48 63 51 ? 33 C0 85 D2 7E ? 4C 8B C2 48 8B 51 ? 48 8B 0A 80 79 ? ? 74 ? 48 FF C0 48 83 C2 ? 49 3B C0 7C ? 32 C0 C3 B0 ? C3 CC CC CC CC 85 D2", 0x0);

MAKE_HOOK(CTFParty_BAnyMemberWithoutTicket, S::CTFParty_BAnyMemberWithoutTicket(), bool,
	void* rcx)
{
	return false;
}