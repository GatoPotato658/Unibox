#include "../SDK/SDK.h"

#include "../Features/Visuals/ESP/ESP.h"
#include "../Features/Visuals/OffscreenArrows/OffscreenArrows.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Aimbot/Aimbot.h"
#include "../Features/Visuals/ESP/ESP.h"
#include "../Features/Visuals/OffscreenArrows/OffscreenArrows.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/PacketManip/AntiAim/AntiAim.h"
#include "../Features/NavBot/NavBotCore.h"
#include "../Features/Misc/AutoQueue/AutoQueue.h"
#if __has_include("../Features/Misc/ProfileStalker/ProfileStalker.h")
#include "../Features/Misc/ProfileStalker/ProfileStalker.h"
#endif
#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Debug/Debug.h"

MAKE_HOOK(IEngineVGui_Paint, U::Memory.GetVirtual(I::EngineVGui, 14), void,
	void* rcx, int iMode)
{
	DEBUG_RETURN(IEngineVGui_Paint, rcx, iMode);

	if (G::Unload)
		return CALL_ORIGINAL(rcx, iMode);

	F::AutoQueue.Run();
#if __has_include("../Features/Misc/ProfileStalker/ProfileStalker.h")
	F::ProfileStalker.Run();
#endif

	if (iMode & PAINT_UIPANELS)
	{
		H::Draw.UpdateKeyStrings();
#ifdef DEBUG_UNI
		F::Visuals.DrawUni();
#endif
	}
	else if (iMode & PAINT_INGAMEPANELS)
	{
		H::Draw.UpdateScreenSize();
		H::Draw.UpdateW2SMatrix();

		auto pLocal = H::Entities.GetLocal();

		{
			CTFPlayer* pCacheFor = SDK::TakingScreenshot() ? nullptr : pLocal;
			F::ESP.CacheDrawInfo(pCacheFor);
			F::Aimbot.CacheDrawInfo(pCacheFor);
			F::Visuals.CacheBestAimPos(pCacheFor);
		}

		if (!SDK::CleanScreenshot())
		{
			H::Draw.Start(true);
			if (pLocal)
			{
				F::CameraWindow.Draw();

				F::AntiAim.Draw(pLocal);
				F::Visuals.DrawPickupTimers();
				F::OffscreenArrows.Draw(pLocal);

				F::NavBotCore.DrawDangerOverlay(pLocal);

#ifdef DEBUG_INFO
				F::Debug.Draw(pLocal);
#endif
			}
			H::Draw.End();
		}
	}

	CALL_ORIGINAL(rcx, iMode);
}
