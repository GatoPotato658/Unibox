#include "../SDK/SDK.h"


MAKE_HOOK(CBaseViewModel_CalcViewModelView, S::CBaseViewModel_CalcViewModelView(), void,
	void* rcx, CBasePlayer* owner, /*const*/ Vector& eyePosition, /*const*/ QAngle& eyeAngles)
{
	DEBUG_RETURN(CBaseViewModel_CalcViewModelView, rcx, owner, eyePosition, eyeAngles);

#ifndef TEXTMODE
	Vec3 vOffset = { Vars::Visuals::Viewmodel::OffsetX.Value, Vars::Visuals::Viewmodel::OffsetY.Value, Vars::Visuals::Viewmodel::OffsetZ.Value };
	Vec3 vAngles = { Vars::Visuals::Viewmodel::Pitch.Value, Vars::Visuals::Viewmodel::Yaw.Value, Vars::Visuals::Viewmodel::Roll.Value };
	if (!Vars::Visuals::Viewmodel::ViewmodelAim.Value && vOffset.IsZero() && vAngles.IsZero() || SDK::CleanScreenshot())
		return CALL_ORIGINAL(rcx, owner, eyePosition, eyeAngles);

	bool bFlip = G::FlipViewmodels;

	if (Vars::Visuals::Viewmodel::ViewmodelAim.Value)
	{
		if (auto pLocal = H::Entities.GetLocal(); pLocal && pLocal->IsAlive() && G::AimPoint.m_iTickCount)
		{
			Vec3 vDiff = I::EngineClient->GetViewAngles() - Math::CalcAngle(eyePosition, G::AimPoint.m_vOrigin);
			if (bFlip)
				vDiff.y *= -1;
			eyeAngles = I::EngineClient->GetViewAngles() - vDiff;
		}
	}

	if (!vOffset.IsZero())
	{
		Vec3 vForward, vRight, vUp; Math::AngleVectors(eyeAngles, &vForward, &vRight, &vUp);
		eyePosition += vForward * vOffset.y;
		eyePosition += vRight * vOffset.x * (bFlip ? -1 : 1);
		eyePosition += vUp * vOffset.z;
	}
	if (vAngles.x)
		eyeAngles.x += vAngles.x;
	if (vAngles.y)
		eyeAngles.y += vAngles.y * (bFlip ? -1 : 1);
	if (vAngles.z)
		eyeAngles.z += vAngles.z * (bFlip ? -1 : 1);

	CALL_ORIGINAL(rcx, owner, eyePosition, eyeAngles);
#endif
}

MAKE_HOOK(CBasePlayer_CalcViewModelView, S::CBasePlayer_CalcViewModelView(), void,
	void* rcx, /*const*/ Vector& eyeOrigin, /*const*/ QAngle& eyeAngles)
{
	DEBUG_RETURN(CBasePlayer_CalcViewModelView, rcx, eyeOrigin, eyeAngles);

	Vector vOldEyeOrigin = eyeOrigin, vOldEyeAngles = eyeAngles;
	CALL_ORIGINAL(rcx, eyeOrigin, eyeAngles);
	eyeOrigin = vOldEyeOrigin, eyeAngles = vOldEyeAngles;
}