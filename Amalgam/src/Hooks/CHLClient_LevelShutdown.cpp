#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/NavBot/NavEngine.h"
#include "../Features/NavBot/Hazards.h"
#include "../Features/NavBot/Jobs/NavBotJobs.h"
#include "../Features/Misc/AutoVote/AutoVote.h"

MAKE_HOOK(CHLClient_LevelShutdown, U::Memory.GetVirtual(I::Client, 7), void,
	void* rcx)
{
	DEBUG_RETURN(CHLClient_LevelShutdown, rcx);

	F::Backtrack.Reset();
	F::Chams.Reset();
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
