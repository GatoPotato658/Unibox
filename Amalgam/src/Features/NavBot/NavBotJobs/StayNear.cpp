#include "NavBotJobs.h"
#include "../NavBotCore.h"
#include "../../Players/PlayerUtils.h"

static float GetPreferredStalkRadius(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!pLocal)
		return 400.f;

	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
		return 260.f;
	case TF_CLASS_SOLDIER:
		return 350.f;
	case TF_CLASS_PYRO:
		return 100.f;
	case TF_CLASS_DEMOMAN:
		return 350.f;
	case TF_CLASS_HEAVY:
		return 250.f;
	case TF_CLASS_ENGINEER:
		return pWeapon && pWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger ? 140.f : 260.f;
	case TF_CLASS_MEDIC:
		return 360.f;
	case TF_CLASS_SNIPER:
		return pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? 600.f : 800.f;
	case TF_CLASS_SPY:
		return 220.f;
	default:
		return 420.f;
	}
}

static float GetStalkLeadTime(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, float flTargetDistance, float flTargetSpeed)
{
	float flLeadTime = 0.15f;

	if (!pLocal)
		return flLeadTime;

	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
	case TF_CLASS_PYRO:
	case TF_CLASS_SPY:
		flLeadTime = 0.14f;
		break;
	case TF_CLASS_SOLDIER:
	case TF_CLASS_DEMOMAN:
		flLeadTime = 0.2f;
		break;
	case TF_CLASS_SNIPER:
		flLeadTime = pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? 0.28f : 0.38f;
		break;
	default:
		flLeadTime = 0.18f;
		break;
	}

	flLeadTime += std::clamp(flTargetDistance / 2500.f, 0.f, 0.2f);
	if (flTargetSpeed < 25.f)
		flLeadTime *= 0.6f;

	return std::clamp(flLeadTime, 0.08f, 0.55f);
}

static Vector Normalize2D(const Vector& v)
{
	Vector vOut = v;
	vOut.z = 0.f;
	float flLength = vOut.Length();
	if (flLength > 0.01f)
		vOut /= flLength;
	else
		vOut = {};
	return vOut;
}

bool CNavBotStayNear::StayNearTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIndex);
	if (!pEntity)
		return false;
	auto pPlayer = pEntity->As<CTFPlayer>();

	Vector vOrigin;

	// No origin recorded, don't bother
	if (!F::BotUtils.GetDormantOrigin(iEntIndex, &vOrigin))
		return false;

	auto pLocalArea = F::NavEngine.GetLocalNavArea();
	if (!pLocalArea)
		return false;

	// Add the vischeck height
	vOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	Vector vTargetOrigin = vOrigin;

	Vector vTargetVelocity{};
	if (pPlayer && !pPlayer->IsDormant())
		vTargetVelocity = pPlayer->GetAbsVelocity();
	vTargetVelocity.z = 0.f;

	const float flTargetDistance = vTargetOrigin.DistTo(pLocal->GetAbsOrigin());
	const float flTargetSpeed = vTargetVelocity.Length2D();

	const float flPreferredRadiusBase = GetPreferredStalkRadius(pLocal, pWeapon);
	const float flPreferredRadius = std::clamp(flPreferredRadiusBase, F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger, F::NavBotCore.m_tSelectedConfig.m_flMax);
	const float flLeadTime = GetStalkLeadTime(pLocal, pWeapon, flTargetDistance, flTargetSpeed);
	const Vector vPredictedOrigin = vTargetOrigin + vTargetVelocity * flLeadTime;

	Vector vForward = Normalize2D(vTargetVelocity);
	if (vForward.IsZero())
		vForward = Normalize2D(vPredictedOrigin - pLocal->GetAbsOrigin());

	Vector vSide(-vForward.y, vForward.x, 0.f);
	const float flOutrunDistance = std::clamp(flPreferredRadius * 0.35f, 80.f, 450.f);
	const float flSideDistance = std::clamp(flPreferredRadius * 0.28f, 70.f, 300.f);
	const float flSideSign = (iEntIndex + pLocal->entindex()) % 2 ? 1.f : -1.f;
	const Vector vAnchor = vPredictedOrigin + vForward * flOutrunDistance + vSide * (flSideDistance * flSideSign);

	// Use std::pair to avoid using the distance functions more than once
	std::vector<NavAreaScore_t> vGoodAreas{};

	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		auto vAreaOrigin = tArea.m_vCenter;

		// Is this area valid for stay near purposes?
		if (!IsAreaValidForStayNear(vOrigin, &tArea, false))
			continue;

		const float flDistToPredicted = vAreaOrigin.DistTo(vPredictedOrigin);
		const float flRangePenalty = std::fabs(flDistToPredicted - flPreferredRadius);
		const float flAnchorPenalty = vAreaOrigin.DistTo(vAnchor);
		const float flTravelPenalty = pLocalArea->m_vCenter.DistTo(vAreaOrigin);

		float flAheadPenalty = 0.f;
		if (!vForward.IsZero())
		{
			Vector vToArea = Normalize2D(vAreaOrigin - vPredictedOrigin);
			float flAheadDot = std::clamp(vToArea.Dot(vForward), -1.f, 1.f);
			flAheadPenalty = (1.f - flAheadDot) * 140.f;
		}

		const float flScore = flRangePenalty * 1.4f + flAnchorPenalty + flTravelPenalty * 0.15f + flAheadPenalty;
		vGoodAreas.push_back({ &tArea, flScore });
	}
	if (NavJobUtils::TryNavToAreaScores(vGoodAreas, PriorityListEnum::StayNear))
	{
		m_iStayNearTargetIdx = pEntity->entindex();
		if (auto pPlayerResource = H::Entities.GetResource())
			m_sFollowTargetName = SDK::ConvertUtf8ToWide(pPlayerResource->GetName(pEntity->entindex()));
		return true;
	}

	return false;
}

bool CNavBotStayNear::IsAreaValidForStayNear(Vector vEntOrigin, CNavArea* pArea, bool bFixLocalZ)
{
	if (bFixLocalZ)
		vEntOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	auto vAreaOrigin = pArea->m_vCenter;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	float flDist = vEntOrigin.DistTo(vAreaOrigin);
	// Too close
	if (flDist < F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger)
		return false;

	// Blacklisted
	if (F::NavEngine.GetFreeBlacklist()->find(pArea) != F::NavEngine.GetFreeBlacklist()->end())
		return false;

	// Too far away
	if (flDist > F::NavBotCore.m_tSelectedConfig.m_flMax)
		return false;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	// Attempt to vischeck
	SDK::Trace(vEntOrigin, vAreaOrigin, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	return trace.fraction == 1.f;
}

int CNavBotStayNear::IsStayNearTargetValid(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex)
{
	if (!pLocal || iEntIndex <= 0 || iEntIndex == pLocal->entindex())
		return 0;

	return F::BotUtils.ShouldTarget(pLocal, pWeapon, iEntIndex);
}

bool CNavBotStayNear::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tStaynearCooldown{};
	static Timer tInvalidTargetTimer{};
	static Timer tTargetSwitchTimer{};
	static int iStayNearTargetIdx = -1;

	// Stay near is off
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
	{
		iStayNearTargetIdx = -1;
		m_iStayNearTargetIdx = -1;
		return false;
	}

	// Don't constantly path, it's slow.
	// Far range classes do not need to repath nearly as often as close range ones.
	if (!tStaynearCooldown.Run(F::NavBotCore.m_tSelectedConfig.m_bPreferFar ? 2.f : 0.5f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear;

	// Too high priority, so don't try
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::StayNear)
	{
		iStayNearTargetIdx = -1;
		m_iStayNearTargetIdx = -1;
		return false;
	}

	int iPreviousTargetValid = IsStayNearTargetValid(pLocal, pWeapon, iStayNearTargetIdx);
	// Check and use our previous target if available
	if (iPreviousTargetValid)
	{
		tInvalidTargetTimer.Update();

		Vector vOrigin;
		if (F::BotUtils.GetDormantOrigin(iStayNearTargetIdx, &vOrigin))
		{
			// Check if current target area is valid
			if (F::NavEngine.IsPathing())
			{
				auto pCrumbs = F::NavEngine.GetCrumbs();
				// We cannot just use the last crumb, as it is always nullptr
				if (pCrumbs->size() > 2)
				{
					auto tLastCrumb = (*pCrumbs)[pCrumbs->size() - 2];
					// Area is still valid, stay on it
					if (IsAreaValidForStayNear(vOrigin, tLastCrumb.m_pNavArea))
						return true;
				}
			}
			// Else Check our origin for validity (Only for ranged classes)
			else if (F::NavBotCore.m_tSelectedConfig.m_bPreferFar && IsAreaValidForStayNear(vOrigin, F::NavEngine.GetLocalNavArea()))
				return true;
		}
		// Else we try to path again
		if (StayNearTarget(pLocal, pWeapon, iStayNearTargetIdx))
			return true;

		// Keep previous target for a short grace window to avoid rapid switching.
		if (!tInvalidTargetTimer.Check(0.75f))
			return F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear;

	}
	// Our previous target wasn't properly checked, try again unless
	else if (iPreviousTargetValid == -1 && !tInvalidTargetTimer.Check(0.35f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear;

	// Failed, invalidate previous target and try others
	iStayNearTargetIdx = -1;
	tInvalidTargetTimer.Update();

	// Cancel path so that we dont follow old target
	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear)
		F::NavEngine.CancelPath();

	const int iDefaultPriority = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(DEFAULT_TAG)].m_iPriority;
	std::vector<std::pair<int, float>> vPriorityPlayers{};
	std::vector<std::pair<int, float>> vSortedPlayers{};
	auto TryCandidates = [&](std::vector<std::pair<int, float>>& vCandidates) -> bool
		{
			if (vCandidates.empty())
				return false;

			std::sort(vCandidates.begin(), vCandidates.end(), [](const std::pair<int, float>& a, const std::pair<int, float>& b) -> bool
				{
					return a.second < b.second;
				});

			// Stickiness: do not immediately replace a target unless it has been held for a minimum time.
			if (iStayNearTargetIdx != -1 && !tTargetSwitchTimer.Check(1.0f) && vCandidates.front().first != iStayNearTargetIdx)
				return F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear;

			for (auto iIdx : vCandidates | std::views::keys)
			{
				if (!StayNearTarget(pLocal, pWeapon, iIdx))
					continue;

				if (iStayNearTargetIdx != iIdx)
					tTargetSwitchTimer.Update();
				iStayNearTargetIdx = iIdx;
				return true;
			}

			return false;
		};

	for (const auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto iPlayerIdx = pEntity->entindex();
		if (!IsStayNearTargetValid(pLocal, pWeapon, iPlayerIdx))
			continue;

		Vector vOrigin;
		if (!F::BotUtils.GetDormantOrigin(iPlayerIdx, &vOrigin))
			continue;

		const float flDistance = vOrigin.DistTo(pLocal->GetAbsOrigin());
		if (H::Entities.GetPriority(iPlayerIdx) > iDefaultPriority)
		{
			vPriorityPlayers.push_back({ iPlayerIdx, flDistance });
			continue;
		}
		vSortedPlayers.push_back({ iPlayerIdx, flDistance });
	}

	if (TryCandidates(vPriorityPlayers) || TryCandidates(vSortedPlayers))
		return true;

	// Stay near failed to find any good targets, add extra delay
	tStaynearCooldown += 3.f;
	return false;
}
