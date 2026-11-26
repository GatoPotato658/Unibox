#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"


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
