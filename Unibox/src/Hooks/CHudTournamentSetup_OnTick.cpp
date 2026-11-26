#include "../SDK/SDK.h"

#include "../Features/Misc/Misc.h"


MAKE_HOOK(CHudTournamentSetup_OnTick, S::CHudTournamentSetup_OnTick(), void,
	void* rcx)
{
	DEBUG_RETURN(CHudTournamentSetup_OnTick, rcx);

	CALL_ORIGINAL(rcx);

	F::Misc.AutoMvmReadyUp();
}
