#include "../SDK/SDK.h"

MAKE_HOOK(CEngineClient_GetScreenAspectRatio, U::Memory.GetVirtual(I::EngineClient, 95), float,
	void* rcx)
{
	if (Vars::Visuals::UI::AspectRatio.Value && !SDK::CleanScreenshot())
		return Vars::Visuals::UI::AspectRatio.Value;

	return CALL_ORIGINAL(rcx);
}