#include "../SDK/SDK.h"

#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/Ticks/Ticks.h"

MAKE_SIGNATURE(CPrediction_RunSimulation, "client.dll", "48 83 EC 38 4C 8B 44", 0x0);

MAKE_HOOK(CPrediction_RunSimulation, S::CPrediction_RunSimulation(), void,
	void* rcx, int current_command, float curtime, CUserCmd* cmd, CTFPlayer* localPlayer)
{
	DEBUG_RETURN(CPrediction_RunSimulation, rcx, current_command, curtime, cmd, localPlayer);

	if (G::Unload)
		return CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);

	if (F::Ticks.AdjustTickbase(localPlayer, current_command))
		curtime = TICKS_TO_TIME(localPlayer->m_nTickBase());

	F::EnginePrediction.AdjustPlayers(localPlayer);
	CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);
	F::EnginePrediction.RestorePlayers();

	F::NetworkFix.StorePredicted(localPlayer, current_command);
}
