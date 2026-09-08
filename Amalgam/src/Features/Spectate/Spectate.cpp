#include "Spectate.h"

void CSpectate::Reset()
{
	m_iIntendedTarget = m_iTarget = -1;
}

void CSpectate::NetUpdateEnd(CTFPlayer* pLocal)
{
	if (!pLocal)
		return;

	m_iTarget = m_iIntendedTarget;
	CTFPlayer* pEntity = nullptr;
	if (HasTarget())
	{
		auto pEntityBase = I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(m_iTarget));
		pEntity = pEntityBase ? pEntityBase->As<CTFPlayer>() : nullptr;
		if (!pEntity || !pEntity->IsPlayer() || pEntity == pLocal)
			m_iTarget = m_iIntendedTarget = -1;
	}
	if (!HasTarget())
	{
		if (pLocal->IsAlive() && pLocal->m_hObserverTarget())
		{
			pLocal->m_iObserverMode() = OBS_MODE_NONE;
			pLocal->m_hObserverTarget().Set(nullptr);
		}
		return;
	}

	m_hOriginalTarget = pLocal->m_hObserverTarget();
	m_iOriginalMode = pLocal->m_iObserverMode();
	if (!pEntity)
		return;

	switch (pEntity->m_hObserverTarget() ? pEntity->m_iObserverMode() : OBS_MODE_NONE)
	{
	case OBS_MODE_FIRSTPERSON:
	case OBS_MODE_THIRDPERSON:
		pLocal->m_hObserverTarget().Set(pEntity->m_hObserverTarget());
		break;
	default:
		pLocal->m_hObserverTarget().Set(pEntity);
	}
	pLocal->m_iObserverMode() = Vars::Visuals::Thirdperson::Enabled.Value ? OBS_MODE_THIRDPERSON : OBS_MODE_FIRSTPERSON;
	Vars::Visuals::Thirdperson::Enabled.Value ? I::Input->CAM_ToThirdPerson() : I::Input->CAM_ToFirstPerson();
}

void CSpectate::NetUpdateStart(CTFPlayer* pLocal)
{
	if (!pLocal || !HasTarget())
		return;

	pLocal->m_hObserverTarget().Set(m_hOriginalTarget);
	pLocal->m_iObserverMode() = m_iOriginalMode;
}

void CSpectate::CreateMove(CUserCmd* pCmd)
{
	int iButtons = pCmd->buttons & ~IN_SCORE;
	if (iButtons)
		m_iIntendedTarget = -1;

	static bool bStaticView = false;
	const bool bLastView = bStaticView;
	const bool bCurrView = bStaticView = HasTarget();
	if (!bCurrView && !bLastView)
		m_vOldView = pCmd->viewangles;
	else
	{
		if (!bCurrView)
			I::EngineClient->SetViewAngles(m_vOldView);
		pCmd->viewangles = m_vOldView;
	}
}

void CSpectate::SetTarget(int iTarget)
{
	m_iIntendedTarget = m_iIntendedTarget == iTarget ? -1 : iTarget;
}

int CSpectate::GetTarget(bool bIntended)
{
	return bIntended ? m_iIntendedTarget : m_iTarget;
}
