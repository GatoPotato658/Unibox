#include "SnipeSentry.h"
#include "NavJobUtils.h"
#include "../NavEngine/NavEngine.h"

bool CNavBotSnipe::IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bShortRangeClass, bool bFixSentryZ)
{
	if (bFixSentryZ)
		vEntOrigin.z += 40.0f;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	float flMinDist = (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange && bShortRangeClass) ? 0.f : 1100.f + HALF_PLAYER_WIDTH;
	if (vEntOrigin.DistTo(vAreaOrigin) <= flMinDist)
		return false;

	// Fails vischeck, bad
	if (!F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vEntOrigin, MASK_SHOT | CONTENTS_GRATE))
		return false;
	return true;
}

bool CNavBotSnipe::TryToSnipe(int iEntIdx, bool bShortRangeClass)
{
	Vector vOrigin;
	if (!F::BotUtils.GetDormantOrigin(iEntIdx, &vOrigin))
		return false;

	vOrigin.z += 40.0f;

	auto pNavFile = F::NavEngine.GetNavFile();
	if (!pNavFile)
		return false;

	std::vector<NavAreaScore_t> vGoodAreas;
	for (auto& area : pNavFile->m_vAreas)
	{
		// Not usable
		if (!IsAreaValidForSnipe(vOrigin, area.m_vCenter, bShortRangeClass, false))
			continue;
		vGoodAreas.push_back({ &area, area.m_vCenter.DistTo(vOrigin) });
	}

	return NavJobUtils::TryNavToAreaScores(vGoodAreas, PriorityListEnum::SnipeSentry, !F::NavBotCore.m_tSelectedConfig.m_bPreferFar);
}

bool CNavBotSnipe::Run(CTFPlayer* pLocal)
{
	static Timer tSentrySnipeCooldown;
	static Timer tInvalidTargetTimer{};

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentries))
	{
		m_iTargetIdx = -1;
		return false;
	}

	// Make sure we don't try to do it on shortrange classes unless specified
	bool bShortRangeClass = pLocal->m_iClass() == TF_CLASS_SCOUT || pLocal->m_iClass() == TF_CLASS_PYRO;
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange) &&
		bShortRangeClass)
	{
		m_iTargetIdx = -1;
		return false;
	}

	// Sentries don't move often, so we can use a slightly longer timer
	if (!tSentrySnipeCooldown.Run(2.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	int iPreviousTargetValid = F::BotUtils.ShouldTargetBuilding(pLocal, m_iTargetIdx);
	if (iPreviousTargetValid == ShouldTargetEnum::Target)
	{
		tInvalidTargetTimer.Update();

		Vector vOrigin;
		if (F::BotUtils.GetDormantOrigin(m_iTargetIdx, &vOrigin))
		{
			// We cannot just use the last crumb, as it is always nullptr
			if (F::NavEngine.m_tLastCrumb.m_pNavArea)
			{
				// Area is still valid, stay on it
				if (IsAreaValidForSnipe(vOrigin, F::NavEngine.m_tLastCrumb.m_pNavArea->m_vCenter, bShortRangeClass))
					return true;
			}
			if (TryToSnipe(m_iTargetIdx, bShortRangeClass))
				return true;
		}
	}
	// Our previous target wasn't properly checked
	else if (iPreviousTargetValid == ShouldTargetEnum::Invalid && !tInvalidTargetTimer.Check(0.1f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	tInvalidTargetTimer.Update();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (pEntity->IsDormant())
			continue;

		int iEntIdx = pEntity->entindex();
		
		// Invalid sentry
		if (F::BotUtils.ShouldTargetBuilding(pLocal, iEntIdx) != ShouldTargetEnum::Target)
			continue;

		// Succeeded in trying to snipe it
		if (TryToSnipe(iEntIdx, bShortRangeClass))
		{
			m_iTargetIdx = iEntIdx;
			return true;
		}
	}

	m_iTargetIdx = -1;
	return false;
}
