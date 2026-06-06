#include "../SDK/SDK.h"

#include "../Features/Aimbot/Aimbot.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Binds/Binds.h"
#include "../Features/CheatDetection/CheatDetection.h"
#include "../Features/CritHack/CritHack.h"
#include "../Features/Players/PlayerUtils.h"
#include "../Features/Resolver/Resolver.h"
#include "../Features/Simulation/MovementSimulation/MovementSimulation.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Visuals/ESP/ESP.h"
#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/Visuals/Glow/Glow.h"
#include "../Features/Visuals/Groups/Groups.h"
#include "../Features/Visuals/OffscreenArrows/OffscreenArrows.h"
#ifdef TEXTMODE
#include "../Features/Misc/AutoQueue/AutoQueue.h"
#include "../Features/Misc/NamedPipe/NamedPipe.h"
#endif 

MAKE_HOOK(CHLClient_FrameStageNotify, U::Memory.GetVirtual(I::Client, 35), void,
	void* rcx, ClientFrameStage_t curStage)
{
	DEBUG_RETURN(CHLClient_FrameStageNotify, rcx, curStage);

	CALL_ORIGINAL(rcx, curStage);

	if (G::Unload) 
		return;

	switch (curStage)
	{
	case FRAME_NET_UPDATE_START:
	{
#ifndef TEXTMODE
		auto pLocal = H::Entities.GetLocal();
		F::Spectate.NetUpdateStart(pLocal);
#endif
		H::Entities.Clear();
		break;
	}
	case FRAME_NET_UPDATE_END:
	{
		H::Entities.Store();
		F::PlayerUtils.Store();

		F::Backtrack.Store();
		F::MoveSim.Store();
		F::CritHack.Store();
#ifndef TEXTMODE
		F::Aimbot.Store();
		auto pLocal = H::Entities.GetLocal();
		F::Groups.Store(pLocal);
		F::ESP.Store(pLocal);
		F::Chams.Store(pLocal);
		F::Glow.Store(pLocal);
		F::OffscreenArrows.Store();
		F::Visuals.Store();
#endif

		F::CheatDetection.Run();
#ifndef TEXTMODE
		F::Spectate.NetUpdateEnd(pLocal);

		F::Visuals.Modulate();
		F::Visuals.DrawHitboxes(1);
#endif
		break;
	}
	case FRAME_RENDER_START:
#ifdef TEXTMODE
		F::AutoQueue.Run();
		F::NamedPipe.ProcessCommandQueue();
#endif
		for (auto& tBind : F::Binds.m_vBinds)
		{	// don't drop inputs for binds
			if (tBind.m_iType != BindEnum::Key)
				continue;

			auto& tKey = tBind.m_tKeyStorage;

			bool bOldIsDown = tKey.m_bIsDown;
			bool bOldIsPressed = tKey.m_bIsPressed;
			bool bOldIsDouble = tKey.m_bIsDouble;
			bool bOldIsReleased = tKey.m_bIsReleased;

			U::KeyHandler.StoreKey(tBind.m_iKey, &tKey);

			tKey.m_bIsDown = tKey.m_bIsDown || bOldIsDown;
			tKey.m_bIsPressed = tKey.m_bIsPressed || bOldIsPressed;
			tKey.m_bIsDouble = tKey.m_bIsDouble || bOldIsDouble;
			tKey.m_bIsReleased = tKey.m_bIsReleased || bOldIsReleased;
		}
	}
}
