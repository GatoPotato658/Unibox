#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"

MAKE_SIGNATURE(CBasePlayer_CalcView, "client.dll", "40 57 48 83 EC ? 44 8B 91", 0x0);
MAKE_SIGNATURE(CTFPlayer_HandleTaunting, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 8B F8 48 85 C0 74 ? 48 8B 00 48 8B CF FF 90 ? ? ? ? 84 C0 75 ? 33 FF 80 BB", 0x0);
MAKE_SIGNATURE(CThirdPersonManager_GetFinalCameraOffset, "client.dll", "F2 0F 10 41 ? 8B 41", 0x0);

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