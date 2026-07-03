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
#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Debug/Debug.h"

MAKE_HOOK(IEngineVGui_Paint, U::Memory.GetVirtual(I::EngineVGui, 14), void,
	void* rcx, int iMode)
{
	DEBUG_RETURN(IEngineVGui_Paint, rcx, iMode);

	if (G::Unload)
		return CALL_ORIGINAL(rcx, iMode);

	F::AutoQueue.Run();

	if (iMode & PAINT_UIPANELS)
	{
		H::Draw.UpdateKeyStrings();
#ifdef DEBUG_UNI
		F::Visuals.DrawUni();
#endif
	}
	else if (iMode & PAINT_INGAMEPANELS && !SDK::CleanScreenshot())
	{
		H::Draw.UpdateScreenSize();
		H::Draw.UpdateW2SMatrix();

		H::Draw.Start(true);
		if (auto pLocal = H::Entities.GetLocal())
		{
			F::CameraWindow.Draw();

			F::AntiAim.Draw(pLocal);
			F::Visuals.DrawPickupTimers();
			F::ESP.Draw();
			F::OffscreenArrows.Draw(pLocal);
			F::Aimbot.Draw(pLocal);

			F::NavBotCore.DrawDangerOverlay(pLocal);

#ifdef DEBUG_INFO
			F::Debug.Draw(pLocal);
#endif
		}
		H::Draw.End();
	}

	CALL_ORIGINAL(rcx, iMode);
}
