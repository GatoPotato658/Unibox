#include "../SDK/SDK.h"


static Vec3 s_vEyePosition;
static Vec3 s_vEyeAngles;

MAKE_HOOK(CSniperDot_GetRenderingPositions, S::CSniperDot_GetRenderingPositions(), bool,
	void* rcx, CTFPlayer* pPlayer, Vec3& vecAttachment, Vec3& vecEndPos, float& flSize)
{
	DEBUG_RETURN(CSniperDot_GetRenderingPositions, rcx, pPlayer, vecAttachment, vecEndPos, flSize);

	if (pPlayer && pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		auto pDot = reinterpret_cast<CSniperDot*>(rcx);

		s_vEyePosition = pPlayer->m_vecOrigin() + pPlayer->GetViewOffset();
		s_vEyeAngles = Math::VectorAngles(pDot->GetAbsOrigin() - s_vEyePosition);
	}

	return CALL_ORIGINAL(rcx, pPlayer, vecAttachment, vecEndPos, flSize);
}

MAKE_HOOK(CBasePlayer_EyePosition, S::CBasePlayer_EyePosition(), Vec3*,
	void* rcx, void* rdx)
{
	DEBUG_RETURN(CBasePlayer_EyePosition, rcx, rdx);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CSniperDot_GetRenderingPositions_EyePosition_Call();

	if (dwRetAddr == dwDesired)
		return &s_vEyePosition;

	return CALL_ORIGINAL(rcx, rdx);
}

MAKE_HOOK(CTFPlayer_EyeAngles, S::CTFPlayer_EyeAngles(), Vec3*,
	void* rcx)
{
	DEBUG_RETURN(CTFPlayer_EyeAngles, rcx);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CSniperDot_GetRenderingPositions_EyeAngles_Call();

	if (dwRetAddr == dwDesired)
		return &s_vEyeAngles;

	return CALL_ORIGINAL(rcx);
}