#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"

MAKE_SIGNATURE(CHLTVCamera_CalcView, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 80 79 ? ? 4D 8B F9", 0x0);
MAKE_SIGNATURE(CHLTVCamera_GetPrimaryTarget, "client.dll", "40 53 48 83 EC ? 48 8B D9 8B 49 ? 85 C9 7E ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B 10 48 8B C8 48 83 C4 ? 5B 48 FF A2 ? ? ? ? 8B 53", 0x0);
MAKE_SIGNATURE(CHLTVCamera_GetMode, "client.dll", "40 53 48 83 EC ? 48 8B D9 8B 49 ? 85 C9 7E ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B 10 48 8B C8 48 83 C4 ? 5B 48 FF A2 ? ? ? ? 8B 43", 0x0);

MAKE_HOOK(CHLTVCamera_CalcView, S::CHLTVCamera_CalcView(), void,
	void* rcx, Vector& origin, QAngle& angles, float& fov)
{
	DEBUG_RETURN(CHLTVCamera_CalcView, rcx, origin, angles, fov);

	auto pHLTVCamera = reinterpret_cast<CHLTVCamera*>(rcx);

	if (F::Spectate.HasTarget())
		pHLTVCamera->m_nCameraMode = Vars::Visuals::Thirdperson::Enabled.Value ? OBS_MODE_THIRDPERSON : OBS_MODE_FIRSTPERSON;

	auto pEntityBase = I::ClientEntityList->GetClientEntity(pHLTVCamera->m_iTraget1);
	auto pEntity = pEntityBase ? pEntityBase->As<CTFPlayer>() : nullptr;
	if (!pEntity || !pEntity->IsPlayer())
		return CALL_ORIGINAL(rcx, origin, angles, fov);

	auto pGameRules = I::TFGameRules();
	auto pViewVectors = pGameRules ? pGameRules->GetViewVectors() : nullptr;
	if (!pViewVectors)
		return CALL_ORIGINAL(rcx, origin, angles, fov);

	Vec3 vOriginalOffset = pViewVectors->m_vView;
	pViewVectors->m_vView = pEntity->GetViewOffset(pHLTVCamera->m_iCameraMan <= 0);
	CALL_ORIGINAL(rcx, origin, angles, fov);
	pViewVectors->m_vView = vOriginalOffset;
}

MAKE_HOOK(CHLTVCamera_GetPrimaryTarget, S::CHLTVCamera_GetPrimaryTarget(), CBaseEntity*,
	void* rcx)
{
	DEBUG_RETURN(CHLTVCamera_GetPrimaryTarget, rcx);

	auto pHLTVCamera = reinterpret_cast<CHLTVCamera*>(rcx);

	if (F::Spectate.HasTarget())
		pHLTVCamera->m_iTraget1 = I::EngineClient->GetPlayerForUserID(F::Spectate.GetTarget()), F::Spectate.Reset();

	return CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CHLTVCamera_GetMode, S::CHLTVCamera_GetMode(), int,
	void* rcx)
{
	DEBUG_RETURN(CHLTVCamera_GetMode, rcx);

	auto pHLTVCamera = reinterpret_cast<CHLTVCamera*>(rcx);

	if (F::Spectate.HasTarget())
		pHLTVCamera->m_nCameraMode = Vars::Visuals::Thirdperson::Enabled.Value ? OBS_MODE_THIRDPERSON : OBS_MODE_FIRSTPERSON;

	return CALL_ORIGINAL(rcx);
}
#endif
