#include "NavBotJobs.h"
#include "../../Misc/NamedPipe/NamedPipe.h"

#include <algorithm>

bool CNavBotGroup::GetFormationOffset(CTFPlayer* pLeader, int iPositionIndex, Vector& vOut)
{
	if (iPositionIndex <= 0)
		return false;

	Vector vDirection = pLeader->m_vecVelocity();
	if (vDirection.Length() < 10.f)
		Math::AngleVectors(pLeader->GetEyeAngles(), &vDirection);

	vOut = NavJobUtils::NormalizePlanar(vDirection) * -m_flFormationDistance * static_cast<float>(iPositionIndex);
	return true;
}

void CNavBotGroup::UpdateLocalBots(CTFPlayer* pLocal)
{
	if (!m_tUpdateFormationTimer.Run(0.5f))
		return;

	m_vLocalBotUserIds.clear();
	m_iPositionInFormation = -1;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	const int iLocalIdx = pLocal->entindex();
	const int iLocalTeam = pLocal->m_iTeamNum();

	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		if (i == iLocalIdx || !pResource->m_bValid(i))
			continue;
#ifdef TEXTMODE
		if (!F::NamedPipe.IsLocalBot(pResource->m_iAccountID(i)))
			continue;
#endif

		auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
		auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
		if (!pEntity || pEntity->IsDormant() || !pEntity->IsPlayer() || pEntity->m_iTeamNum() != iLocalTeam)
			continue;

		if (!pEntity->As<CTFPlayer>()->IsAlive())
			continue;

		m_vLocalBotUserIds.push_back(pResource->m_iUserID(i));
	}

	std::sort(m_vLocalBotUserIds.begin(), m_vLocalBotUserIds.end());

	const uint32_t uLocalUserId = pResource->m_iUserID(iLocalIdx);
	m_iPositionInFormation = static_cast<int>(std::count_if(m_vLocalBotUserIds.begin(), m_vLocalBotUserIds.end(), [uLocalUserId](uint32_t uUserId) { return uUserId < uLocalUserId; }));
}

bool CNavBotGroup::Run(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::GroupWithOthers))
		return false;

	if (m_iPositionInFormation <= 0 || m_vLocalBotUserIds.empty())
		return false;

	auto pLeaderEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(m_vLocalBotUserIds.front()));
	auto pLeader = pLeaderEntity && pLeaderEntity->As<CBaseEntity>()->IsPlayer() ? pLeaderEntity->As<CTFPlayer>() : nullptr;
	if (!pLeader || !pLeader->IsAlive() || pLeader->m_iTeamNum() != pLocal->m_iTeamNum())
		return false;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Patrol)
		return false;

	Vector vOffset;
	if (!GetFormationOffset(pLeader, m_iPositionInFormation, vOffset))
		return false;

	const Vector vTargetPos = pLeader->GetAbsOrigin() + vOffset;

	if (pLocal->GetAbsOrigin().DistTo(vTargetPos) <= 30.f)
	{
		if (F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();

		m_iConsecutiveFailures = 0;
		m_vLastTargetPos = vTargetPos;
		return true;
	}

	if (m_vLastTargetPos.DistTo(vTargetPos) <= 50.f)
	{
		if (!F::NavEngine.IsPathing() && ++m_iConsecutiveFailures >= 3)
		{
			m_iConsecutiveFailures = 0;
			m_flFormationDistance += 50.f;
			if (m_flFormationDistance > 300.f)
				m_flFormationDistance = 120.f;

			return false;
		}
	}
	else
		m_iConsecutiveFailures = 0;
	m_vLastTargetPos = vTargetPos;

	if (!m_tFormationNavTimer.Run(0.5f) && F::NavEngine.IsPathing())
		return true;

	return F::NavEngine.NavTo(vTargetPos, PriorityListEnum::Patrol);
}

void CNavBotGroup::Reset()
{
	m_iPositionInFormation = -1;
	m_flFormationDistance = 120.f;
	m_iConsecutiveFailures = 0;
	m_vLastTargetPos = {};
	m_vLocalBotUserIds.clear();
}
