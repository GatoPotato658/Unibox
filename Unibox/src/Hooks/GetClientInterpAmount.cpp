#include "../SDK/SDK.h"


MAKE_HOOK(GetClientInterpAmount, S::GetClientInterpAmount(), float)
{
	DEBUG_RETURN(GetClientInterpAmount);

	if (G::Unload || !Vars::Visuals::Removals::Lerp.Value && !Vars::Visuals::Removals::Interpolation.Value)
		return CALL_ORIGINAL();

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwUndesired1 = S::CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call1();
	const auto dwUndesired2 = S::CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call2();

	if (dwRetAddr != dwUndesired1 && dwRetAddr != dwUndesired2)
		return 0.f;

	return CALL_ORIGINAL();
}