#include "../SDK/SDK.h"


MAKE_HOOK(R_DrawSkyBox, S::R_DrawSkyBox(), void,
	float zFar, int nDrawFlags)
{
	DEBUG_RETURN(R_DrawSkyBox, zFar, nDrawFlags);

#ifndef TEXTMODE
	if (FNV1A::Hash32(Vars::Visuals::World::SkyboxChanger.Value.c_str()) == FNV1A::Hash32Const("Off") || SDK::CleanScreenshot())
		return CALL_ORIGINAL(zFar, nDrawFlags);

	static auto sv_skyname = H::ConVars.FindVar("sv_skyname");
	if (!sv_skyname)
		return CALL_ORIGINAL(zFar, nDrawFlags);

	std::string sOriginal = sv_skyname->GetString();
	sv_skyname->SetValue(Vars::Visuals::World::SkyboxChanger.Value.c_str());
	CALL_ORIGINAL(zFar, nDrawFlags);
	sv_skyname->SetValue(sOriginal.c_str());
#endif
}
