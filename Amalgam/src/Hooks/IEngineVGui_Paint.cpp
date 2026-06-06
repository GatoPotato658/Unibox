#include "../SDK/SDK.h"

#include "../Features/Visuals/Notifications/Notifications.h"
#include "../Features/Visuals/ESP/ESP.h"
#include "../Features/Visuals/OffscreenArrows/OffscreenArrows.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Ticks/Ticks.h"
#include "../Features/CritHack/CritHack.h"
#include "../Features/Visuals/SpectatorList/SpectatorList.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Visuals/PlayerConditions/PlayerConditions.h"
#include "../Features/NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Features/Aimbot/Aimbot.h"
#include "../Features/Visuals/ESP/ESP.h"
#include "../Features/Visuals/OffscreenArrows/OffscreenArrows.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/Visuals/Notifications/Notifications.h"
#include "../Features/PacketManip/AntiAim/AntiAim.h"
#include "../Features/NavBot/NavBotCore.h"
#include "../Features/Aimbot/AutoHeal/AutoHeal.h"
#include "../Features/Misc/AutoQueue/AutoQueue.h"
#include "../Features/Visuals/Materials/Materials.h"

MAKE_HOOK(IEngineVGui_Paint, U::Memory.GetVirtual(I::EngineVGui, 14), void,
	void* rcx, int iMode)
{
	DEBUG_RETURN(IEngineVGui_Paint, rcx, iMode);

	if (G::Unload)
		return CALL_ORIGINAL(rcx, iMode);

	F::AutoQueue.Run();

	if (iMode & PAINT_INGAMEPANELS && I::EngineClient->IsInGame() && !SDK::CleanScreenshot() && F::Materials.m_bLoaded)
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

#ifdef DEBUG_VACCINATOR
			F::AutoHeal.Draw(pLocal);
#endif
			F::NoSpreadHitscan.Draw(pLocal);
			F::PlayerConditions.Draw(pLocal);
			F::Backtrack.Draw(pLocal);
			F::SpectatorList.Draw(pLocal);
			F::CritHack.Draw(pLocal);
			F::NavBotCore.Draw(pLocal);
			F::Ticks.Draw(pLocal);
			F::Visuals.DrawDebugInfo(pLocal);
		}
		H::Draw.End();
	}

	CALL_ORIGINAL(rcx, iMode);

	if (iMode & PAINT_UIPANELS && !SDK::CleanScreenshot() && F::Materials.m_bLoaded)
	{
		H::Draw.UpdateScreenSize();
		H::Draw.UpdateKeyStrings();

		H::Draw.Start();
		{
			F::Notifications.Draw();
#ifdef DEBUG_UNI
			F::Visuals.DrawUni();
#endif
		}
		H::Draw.End();
	}
}
