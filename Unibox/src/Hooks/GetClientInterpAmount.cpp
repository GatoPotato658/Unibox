#include "../SDK/SDK.h"

MAKE_SIGNATURE(GetClientInterpAmount, "client.dll", "40 53 48 83 EC ? 8B 05 ? ? ? ? A8 ? 75 ? 48 8B 0D ? ? ? ? 48 8D 15", 0x0);
MAKE_SIGNATURE(CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call1, "client.dll", "48 8B 05 ? ? ? ? 4C 8D 05 ? ? ? ? F3 44 0F 10 0D", 0x0);
MAKE_SIGNATURE(CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call2, "client.dll", "48 8B 05 ? ? ? ? F3 0F 10 48 ? F3 0F 5E C1", 0x0);

MAKE_HOOK(GetClientInterpAmount, S::GetClientInterpAmount(), float)
{
	DEBUG_RETURN(GetClientInterpAmount);

	if (!Vars::Visuals::Removals::Lerp.Value && !Vars::Visuals::Removals::Interpolation.Value)
		return CALL_ORIGINAL();

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwUndesired1 = S::CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call1();
	const auto dwUndesired2 = S::CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call2();

	if (dwRetAddr != dwUndesired1 && dwRetAddr != dwUndesired2)
		return 0.f;

	return CALL_ORIGINAL();
}