#include "NavBotJobs.h"
#include "../NavBotCore.h"

#include <algorithm>

static float GetSnipeMinDistance(bool bShortRangeClass)
{
	const bool bLowRangeAllowed = (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange) && bShortRangeClass;
	return bLowRangeAllowed ? 0.f : TFGame::SENTRY_MAX_RANGE + HALF_PLAYER_WIDTH;
}

bool CNavBotSnipe::IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bShortRangeClass)
{
	vEntOrigin.z += 40.f;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	if (vEntOrigin.DistTo(vAreaOrigin) <= GetSnipeMinDistance(bShortRangeClass))
		return false;

	return F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vEntOrigin, MASK_SHOT | CONTENTS_GRATE);
}

bool CNavBotSnipe::TryToSnipe(int iEntIdx, bool bShortRangeClass)
{
	Vector vOrigin;
	if (!F::BotUtils.GetDormantOrigin(iEntIdx, &vOrigin))
		return false;

	auto pNavFile = F::NavEngine.GetNavFile();
	if (!pNavFile)
		return false;

	if (!F::NavEngine.IsPriorityAllowed(PriorityListEnum::SnipeSentry))
		return false;

	const float flMinDist = GetSnipeMinDistance(bShortRangeClass);
	const float flMaxDist = std::max(flMinDist + 512.f, static_cast<float>(F::NavBotCore.m_tSelectedConfig.m_flMax));

	std::vector<NavAreaScore_t> vCandidates;
	std::vector<CNavArea*> vAreas;
	pNavFile->QueryOverlapping(vOrigin, flMaxDist, vAreas);
	vCandidates.reserve(vAreas.size());
	for (CNavArea* pArea : vAreas)
	{
		if (!pArea)
			continue;

		Vector vAreaOrigin = pArea->m_vCenter;
		vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		if (vOrigin.DistTo(vAreaOrigin) <= flMinDist)
			continue;
		vCandidates.push_back({ pArea, pArea->m_vCenter.DistTo(vOrigin) });
	}

	const bool bLowestFirst = !F::NavBotCore.m_tSelectedConfig.m_bPreferFar;
	std::sort(vCandidates.begin(), vCandidates.end(), [bLowestFirst](const NavAreaScore_t& a, const NavAreaScore_t& b)
		{
			return bLowestFirst ? a.m_flScore < b.m_flScore : a.m_flScore > b.m_flScore;
		});

	const size_t nTraceCandidates = std::min<size_t>(vCandidates.size(), 64);
	for (size_t i = 0; i < nTraceCandidates; ++i)
	{
		const auto& tCandidate = vCandidates[i];
		Vector vAreaOrigin = tCandidate.m_pArea->m_vCenter;
		vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		if (!F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vOrigin, MASK_SHOT | CONTENTS_GRATE))
			continue;
		if (F::NavEngine.NavTo(tCandidate.m_pArea->m_vCenter, PriorityListEnum::SnipeSentry))
			return true;
	}

	return false;
}

bool CNavBotSnipe::Run(CTFPlayer* pLocal)
{
	const auto Release = [this]() -> bool
		{
			m_iTargetIdx = -1;
			if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry)
				F::NavEngine.CancelPath();
			return false;
		};

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentries))
		return Release();

	const bool bShortRangeClass = NavJobUtils::IsShortRangeClass(pLocal);
	if (bShortRangeClass && !(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange))
		return Release();

	if (!m_tSnipeCooldown.Run(2.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	const auto eTargetState = F::BotUtils.ShouldTargetBuilding(pLocal, m_iTargetIdx);
	if (eTargetState == ShouldTargetEnum::Target)
	{
		m_tInvalidTargetTimer.Update();

		Vector vOrigin;
		if (F::BotUtils.GetDormantOrigin(m_iTargetIdx, &vOrigin))
		{
			auto pLocalArea = F::NavEngine.GetLocalNavArea();
			if (pLocalArea && IsAreaValidForSnipe(vOrigin, pLocalArea->m_vCenter, bShortRangeClass))
				return true;
			if (TryToSnipe(m_iTargetIdx, bShortRangeClass))
				return true;
		}
	}
	else if (eTargetState == ShouldTargetEnum::Invalid && !m_tInvalidTargetTimer.Check(0.1f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	m_tInvalidTargetTimer.Update();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (pEntity->IsDormant())
			continue;

		const int iEntIdx = pEntity->entindex();
		if (F::BotUtils.ShouldTargetBuilding(pLocal, iEntIdx) != ShouldTargetEnum::Target)
			continue;

		if (TryToSnipe(iEntIdx, bShortRangeClass))
		{
			m_iTargetIdx = iEntIdx;
			return true;
		}
	}

	return Release();
}

void CNavBotSnipe::Reset()
{
	m_iTargetIdx = -1;
	m_tSnipeCooldown = Timer();
	m_tInvalidTargetTimer = Timer();
}
