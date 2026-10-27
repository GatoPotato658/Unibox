#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"

MAKE_SIGNATURE(CBasePlayer_CalcObserverView, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 01 49 8B F9 49 8B F0", 0x0);

MAKE_HOOK(CBasePlayer_CalcObserverView, S::CBasePlayer_CalcObserverView(), void,
	void* rcx, Vector& eyeOrigin, QAngle& eyeAngles, float& fov)
{
	DEBUG_RETURN(CBasePlayer_CalcObserverView, rcx, eyeOrigin, eyeAngles, fov);

	if (!F::Spectate.HasTarget())
		return CALL_ORIGINAL(rcx, eyeOrigin, eyeAngles, fov);

	auto pPlayer = reinterpret_cast<CBasePlayer*>(rcx);
	auto pTargetEntity = pPlayer->m_hObserverTarget().Get();
	auto pTarget = pTargetEntity ? pTargetEntity->As<CTFPlayer>() : nullptr;
	if (!pTarget || !pTarget->IsPlayer())
		return CALL_ORIGINAL(rcx, eyeOrigin, eyeAngles, fov);

	Vec3 vOldOffset = pPlayer->m_vecViewOffset();
	pPlayer->m_vecViewOffset() = pTarget->GetViewOffset();
	CALL_ORIGINAL(rcx, eyeOrigin, eyeAngles, fov);
	pPlayer->m_vecViewOffset() = vOldOffset;
}
#endif
