#include "../SDK/SDK.h"

#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/NavBot/NavEngine/NavEngine.h"
#include "../Features/NavBot/Hazards/Hazards.h"
#include "../Features/NavBot/NavBotJobs/NavBotJobs.h"
#include "../Features/Misc/AutoVote/AutoVote.h"

MAKE_HOOK(CHLClient_LevelShutdown, U::Memory.GetVirtual(I::Client, 7), void,
	void* rcx)
{
	DEBUG_RETURN(CHLClient_LevelShutdown, rcx);

	H::Entities.Clear(true);
	F::EnginePrediction.Unload();
	F::Spectate.Reset();
#ifndef TEXTMODE
	G::TriggerStorage.clear();
	G::PasstimeGoalStorage.clear();
#endif
	F::NavEngine.ClearRespawnRooms();
	F::Hazards.Reset();
	F::NavBotSupplies.ResetCachedOrigins();
	F::AutoVote.Reset();

	CALL_ORIGINAL(rcx);
}
