#include "../SDK/SDK.h"

#include "../Features/Aimbot/AimbotProjectile/AimbotProjectile.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/EnginePrediction/Doors.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/Visuals/Hitmarker/Hitmarker.h"
#include "../Features/NavBot/NavEngine.h"
#include "../Features/NavBot/Hazards.h"
#include "../Features/NavBot/Jobs/NavBotJobs.h"
#include "../Features/Misc/AutoVote/AutoVote.h"
#include "../Features/Misc/AutoTranslate/AutoTranslate.h"
#include "../Features/Misc/Radio/Radio.h"
#ifndef TEXTMODE
#include "../Features/Visuals/Radar/Radar.h"
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#endif

MAKE_HOOK(CHLClient_LevelShutdown, U::Memory.GetVirtual(I::Client, 7), void,
	void* rcx)
{
	DEBUG_RETURN(CHLClient_LevelShutdown, rcx);

	F::AimbotProjectile.ResetShotDiagnostics();
	F::Backtrack.Reset();
	F::Radio.OnLevelShutdown();
	F::AutoTranslate.OnLevelShutdown();
	F::NetworkFix.Reset();
	F::Chams.Reset();
	F::D3DMaterials.ClearCache();
	F::Hitmarker.Clear();
	H::Entities.RemovePrecipitation(false);
	H::Entities.Clear(true);
	F::Doors.Clear();
	F::EnginePrediction.Unload();
	F::Spectate.Reset();
#ifndef TEXTMODE
	G::TriggerStorage.clear();
	G::PasstimeGoalStorage.clear();
#endif
	G::DoorStorage.clear();
	G::DoorTriggerStorage.clear();
	SDK::InvalidateMapEntityStorage();
	F::NavEngine.ClearRespawnRooms();
	F::Hazards.Reset();
	F::NavBotSupplies.ResetCachedOrigins();
	F::AutoVote.Reset();
#ifndef TEXTMODE
	F::Radar.Reset();
	F::SkinChanger.OnLevelShutdown();
#endif

	CALL_ORIGINAL(rcx);
}
