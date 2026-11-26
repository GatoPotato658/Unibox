#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"


MAKE_HOOK(CBasePlayer_CalcView, S::CBasePlayer_CalcView(), void,
	void* rcx, Vector& eyeOrigin, QAngle& eyeAngles, float& zNear, float& zFar, float& fov)
{
	DEBUG_RETURN(CBasePlayer_CalcView, rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);

	if ((!Vars::Visuals::Removals::ViewPunch.Value || SDK::CleanScreenshot()) && !F::Spectate.HasTarget())
		return CALL_ORIGINAL(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);

	auto pPlayer = reinterpret_cast<CBasePlayer*>(rcx);

	Vec3 vOriginalPunch = pPlayer->m_vecPunchAngle();
	pPlayer->m_vecPunchAngle() = {};
	CALL_ORIGINAL(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);
	pPlayer->m_vecPunchAngle() = vOriginalPunch;
}

MAKE_HOOK(CTFPlayer_HandleTaunting, S::CTFPlayer_HandleTaunting(), void,
	void* rcx)
{
	DEBUG_RETURN(CTFPlayer_HandleTaunting, rcx);

	if (!F::Spectate.HasTarget())
		return CALL_ORIGINAL(rcx);

	I::ThirdPersonManager->m_bOverrideThirdPerson = false;
}

MAKE_HOOK(CThirdPersonManager_GetFinalCameraOffset, S::CThirdPersonManager_GetFinalCameraOffset(), Vector,
	void* rcx)
{
	DEBUG_RETURN(CThirdPersonManager_GetFinalCameraOffset, rcx);

	if (!F::Spectate.HasTarget())
		return CALL_ORIGINAL(rcx);

	float flOriginalUpOffset = I::ThirdPersonManager->m_flUpOffset;
	I::ThirdPersonManager->m_flUpOffset = 0.f;
	Vec3 vReturn = CALL_ORIGINAL(rcx);
	I::ThirdPersonManager->m_flUpOffset = flOriginalUpOffset;
	return vReturn;
}
#endif