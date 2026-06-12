#include "../SDK/SDK.h"

#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Ticks/Ticks.h"
#include "../Features/NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Features/CheatDetection/CheatDetection.h"
#include "../Features/Resolver/Resolver.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/NavBot/NavEngine/Controllers/Controller.h"
#include "../Features/NavBot/NavBotCore.h"
#include "../Features/NavBot/NavEngine/NavEngine.h"
#include "../Features/Killstreak/Killstreak.h"
#include "../Features/FollowBot/FollowBot.h"

MAKE_HOOK(CViewRender_LevelInit, U::Memory.GetVirtual(I::ViewRender, 1), void,
	void* rcx)
{
	DEBUG_RETURN(CViewRender_LevelInit, rcx);

#ifndef TEXTMODE
	F::Materials.ReloadMaterials();
	F::Visuals.OverrideWorldTextures();
	F::Killstreak.Reset();
	F::Spectate.Reset();
#endif

	F::Backtrack.Reset();
	F::Ticks.Reset();
	F::NoSpreadHitscan.Reset();
	F::CheatDetection.Reset();
	F::Resolver.Reset();
	F::GameObjectiveController.Reset();
	F::NavEngine.Reset();
	F::NavBotCore.Reset();
	F::BotUtils.Reset();
	F::FollowBot.Reset();

	G::WranglerSecondFireTime = 0.f;

	CALL_ORIGINAL(rcx);
}