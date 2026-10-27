#include "../SDK/SDK.h"

#include "../Features/Misc/Misc.h"

MAKE_SIGNATURE(CHudTournamentSetup_OnTick, "client.dll", "40 57 48 83 EC ? 48 8B F9 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 89 5C 24", 0x0);

MAKE_HOOK(CHudTournamentSetup_OnTick, S::CHudTournamentSetup_OnTick(), void,
	void* rcx)
{
	DEBUG_RETURN(CHudTournamentSetup_OnTick, rcx);

	CALL_ORIGINAL(rcx);

	F::Misc.AutoMvmReadyUp();
}
