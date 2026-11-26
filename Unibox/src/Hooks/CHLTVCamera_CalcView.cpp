#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"


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
