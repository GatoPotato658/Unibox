#include "NavBotJobs.h"
#include "../NavBotCore.h"
#include "../Hazards/Hazards.h"
#include "../../World/World.h"
#include "../../Players/PlayerUtils.h"

struct StalkProfile_t
{
	float m_flPreferredRadius = 420.f;
	float m_flMinRadius = 120.f;
	float m_flMaxRadius = 1200.f;
	float m_flLeadBase = 0.18f;
	float m_flAheadDistance = 0.f;
	float m_flSideDistance = 0.f;
	float m_flRangeWeight = 1.4f;
	float m_flAnchorWeight = 0.9f;
	float m_flTravelWeight = 0.12f;
	float m_flHazardWeight = 0.08f;
	float m_flCoverWeight = 1.f;
	bool m_bPreferAhead = true;
	bool m_bPreferSightline = false;
	bool m_bPreferBackstab = false;
};

struct StalkCandidate_t
{
	CNavArea* m_pArea = nullptr;
	float m_flScore = 0.f;
};

struct CoverCache_t
{
	int m_iTick = 0;
	float m_flScore = 0.f;
};

static StalkProfile_t GetStalkProfile(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	StalkProfile_t tProfile{};
	if (!pLocal)
		return tProfile;

	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
		tProfile.m_flPreferredRadius = 210.f;
		tProfile.m_flMinRadius = 80.f;
		tProfile.m_flMaxRadius = 520.f;
		tProfile.m_flLeadBase = 0.13f;
		tProfile.m_flAheadDistance = 70.f;
		tProfile.m_flSideDistance = 90.f;
		break;
	case TF_CLASS_SOLDIER:
		tProfile.m_flPreferredRadius = 430.f;
		tProfile.m_flMinRadius = 220.f;
		tProfile.m_flMaxRadius = 820.f;
		tProfile.m_flLeadBase = 0.21f;
		tProfile.m_flAheadDistance = 120.f;
		tProfile.m_flSideDistance = 110.f;
		break;
	case TF_CLASS_PYRO:
		tProfile.m_flPreferredRadius = 115.f;
		tProfile.m_flMinRadius = 45.f;
		tProfile.m_flMaxRadius = 330.f;
		tProfile.m_flLeadBase = 0.12f;
		tProfile.m_flAheadDistance = 45.f;
		tProfile.m_flSideDistance = 55.f;
		tProfile.m_flRangeWeight = 2.4f;
		break;
	case TF_CLASS_DEMOMAN:
		tProfile.m_flPreferredRadius = 470.f;
		tProfile.m_flMinRadius = 260.f;
		tProfile.m_flMaxRadius = 900.f;
		tProfile.m_flLeadBase = 0.22f;
		tProfile.m_flAheadDistance = 130.f;
		tProfile.m_flSideDistance = 130.f;
		break;
	case TF_CLASS_HEAVY:
		tProfile.m_flPreferredRadius = 240.f;
		tProfile.m_flMinRadius = 110.f;
		tProfile.m_flMaxRadius = 620.f;
		tProfile.m_flLeadBase = 0.17f;
		tProfile.m_flAheadDistance = 85.f;
		tProfile.m_flSideDistance = 90.f;
		break;
	case TF_CLASS_ENGINEER:
		tProfile.m_flPreferredRadius = pWeapon && pWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger ? 150.f : 260.f;
		tProfile.m_flMinRadius = 90.f;
		tProfile.m_flMaxRadius = 650.f;
		tProfile.m_flLeadBase = 0.16f;
		tProfile.m_flAheadDistance = 60.f;
		tProfile.m_flSideDistance = 100.f;
		break;
	case TF_CLASS_MEDIC:
		tProfile.m_flPreferredRadius = 360.f;
		tProfile.m_flMinRadius = 180.f;
		tProfile.m_flMaxRadius = 760.f;
		tProfile.m_flLeadBase = 0.18f;
		tProfile.m_flAheadDistance = 70.f;
		tProfile.m_flSideDistance = 120.f;
		break;
	case TF_CLASS_SNIPER:
		tProfile.m_flPreferredRadius = pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? 620.f : 920.f;
		tProfile.m_flMinRadius = 420.f;
		tProfile.m_flMaxRadius = 1600.f;
		tProfile.m_flLeadBase = pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? 0.28f : 0.38f;
		tProfile.m_flAheadDistance = 220.f;
		tProfile.m_flSideDistance = 180.f;
		tProfile.m_flCoverWeight = 0.45f;
		tProfile.m_bPreferSightline = true;
		break;
	case TF_CLASS_SPY:
		tProfile.m_flPreferredRadius = 120.f;
		tProfile.m_flMinRadius = 55.f;
		tProfile.m_flMaxRadius = 420.f;
		tProfile.m_flLeadBase = 0.14f;
		tProfile.m_flAheadDistance = -80.f;
		tProfile.m_flSideDistance = 65.f;
		tProfile.m_flRangeWeight = 2.1f;
		tProfile.m_bPreferAhead = false;
		tProfile.m_bPreferBackstab = true;
		break;
	default:
		break;
	}

	tProfile.m_flPreferredRadius = std::clamp(tProfile.m_flPreferredRadius, F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger, F::NavBotCore.m_tSelectedConfig.m_flMax);
	tProfile.m_flMinRadius = std::clamp(tProfile.m_flMinRadius, F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger, tProfile.m_flPreferredRadius);
	tProfile.m_flMaxRadius = std::clamp(tProfile.m_flMaxRadius, tProfile.m_flPreferredRadius, F::NavBotCore.m_tSelectedConfig.m_flMax);
	return tProfile;
}

static float GetStalkLeadTime(const StalkProfile_t& tProfile, float flTargetDistance, float flTargetSpeed)
{
	float flLeadTime = tProfile.m_flLeadBase;
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

static bool TraceVisible(const Vector& vFrom, const Vector& vTo)
{
	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};
	SDK::Trace(vFrom, vTo, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	return trace.fraction == 1.f;
}

static float GetHidingSpotCoverScore(CNavArea* pArea, const Vector& vTargetOrigin, bool bPreferSightline)
{
	float flScore = 0.f;
	for (const auto& tHidingSpot : pArea->m_vHidingSpots)
	{
		const float flDistance = tHidingSpot.m_vPos.DistTo(pArea->m_vCenter);
		if (flDistance > 220.f)
			continue;

		if (tHidingSpot.HasGoodCover())
			flScore -= bPreferSightline ? 30.f : 120.f;
		if (tHidingSpot.IsGoodSniperSpot())
			flScore += bPreferSightline ? -90.f : 20.f;
		if (tHidingSpot.IsIdealSniperSpot())
			flScore += bPreferSightline ? -140.f : 30.f;
		if (tHidingSpot.IsExposed())
			flScore += bPreferSightline ? -20.f : 100.f;
		if (!TraceVisible(vTargetOrigin, tHidingSpot.m_vPos + Vector(0.f, 0.f, PLAYER_CROUCHED_JUMP_HEIGHT)))
			flScore -= bPreferSightline ? -140.f : 70.f;
	}
	return flScore;
}

static float GetFaceCoverScore(int iEntIndex, CNavArea* pArea, const Vector& vTargetOrigin, bool bPreferSightline)
{
	static std::unordered_map<uint64_t, CoverCache_t> mCoverCache{};

	const uint64_t uKey = (static_cast<uint64_t>(iEntIndex) << 32) ^ pArea->m_uId;
	const int iNow = I::GlobalVars ? I::GlobalVars->tickcount : 0;
	auto it = mCoverCache.find(uKey);
	if (it != mCoverCache.end() && iNow - it->second.m_iTick <= TIME_TO_TICKS(0.35f))
		return it->second.m_flScore;

	if (mCoverCache.size() > 512)
		std::erase_if(mCoverCache, [iNow](const auto& tEntry) { return iNow - tEntry.second.m_iTick > TIME_TO_TICKS(1.5f); });

	const Vector vMins(pArea->m_vNwCorner.x - 56.f, pArea->m_vNwCorner.y - 56.f, pArea->m_flMinZ);
	const Vector vMaxs(pArea->m_vSeCorner.x + 56.f, pArea->m_vSeCorner.y + 56.f, pArea->m_flMaxZ + PLAYER_CROUCHED_JUMP_HEIGHT);
	CTraceFilterWorldAndPropsOnly filter = {};
	std::vector<Face_t> vFaces = F::World.GetFacesInAABB(vMins, vMaxs, MASK_SOLID, &filter, FaceTypeEnum::Cache);
	Vector vToTarget = Normalize2D(vTargetOrigin - pArea->m_vCenter);

	float flScore = 0.f;
	for (const auto& tFace : vFaces)
	{
		Vector vNormal = Normalize2D(tFace.m_vNormal);
		if (vNormal.IsZero() || vToTarget.IsZero())
			continue;

		const float flFacing = vNormal.Dot(vToTarget);
		if (flFacing > 0.25f)
			flScore -= bPreferSightline ? 8.f : 28.f;
		else if (flFacing < -0.25f)
			flScore += bPreferSightline ? 4.f : 12.f;
	}

	flScore = std::clamp(flScore, -180.f, 140.f);
	mCoverCache[uKey] = { iNow, flScore };
	return flScore;
}

static float GetBackstabScore(const Vector& vAreaOrigin, const Vector& vTargetOrigin, const Vector& vTargetForward)
{
	if (vTargetForward.IsZero())
		return 0.f;

	Vector vToArea = Normalize2D(vAreaOrigin - vTargetOrigin);
	if (vToArea.IsZero())
		return 240.f;

	const float flBehindDot = vToArea.Dot(vTargetForward * -1.f);
	const float flSideDot = std::fabs(vToArea.Dot(Vector(-vTargetForward.y, vTargetForward.x, 0.f)));
	return (1.f - std::clamp(flBehindDot, -1.f, 1.f)) * 280.f + flSideDot * 45.f;
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

	const StalkProfile_t tProfile = GetStalkProfile(pLocal, pWeapon);
	const float flLeadTime = GetStalkLeadTime(tProfile, flTargetDistance, flTargetSpeed);
	const Vector vPredictedOrigin = vTargetOrigin + vTargetVelocity * flLeadTime;

	Vector vForward = Normalize2D(vTargetVelocity);
	if (vForward.IsZero())
		vForward = Normalize2D(vPredictedOrigin - pLocal->GetAbsOrigin());

	if (tProfile.m_bPreferBackstab && pPlayer && !pPlayer->IsDormant())
	{
		Vec3 vTargetForward;
		Math::AngleVectors(pPlayer->GetEyeAngles(), &vTargetForward);
		vForward = Normalize2D(vTargetForward);
	}

	Vector vSide(-vForward.y, vForward.x, 0.f);
	const float flSideSign = (iEntIndex + pLocal->entindex()) % 2 ? 1.f : -1.f;
	const Vector vAnchor = vPredictedOrigin + vForward * tProfile.m_flAheadDistance + vSide * (tProfile.m_flSideDistance * flSideSign);

	std::vector<StalkCandidate_t> vCandidates{};
	vCandidates.reserve(F::NavEngine.GetNavFile()->m_vAreas.size());

	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		auto vAreaOrigin = tArea.m_vCenter;

		if (!IsAreaValidForStayNear(vOrigin, &tArea, false))
			continue;

		const float flDistToPredicted = vAreaOrigin.DistTo(vPredictedOrigin);
		if (flDistToPredicted < tProfile.m_flMinRadius || flDistToPredicted > tProfile.m_flMaxRadius)
			continue;

		const float flRangePenalty = std::fabs(flDistToPredicted - tProfile.m_flPreferredRadius);
		const float flAnchorPenalty = vAreaOrigin.DistTo(vAnchor) * tProfile.m_flAnchorWeight;
		const float flTravelPenalty = pLocalArea->m_vCenter.DistTo(vAreaOrigin);
		const float flHazardCost = F::Hazards.GetCost(&tArea);
		if (!std::isfinite(flHazardCost))
			continue;

		float flAheadPenalty = 0.f;
		if (!vForward.IsZero())
		{
			Vector vToArea = Normalize2D(vAreaOrigin - vPredictedOrigin);
			float flAheadDot = std::clamp(vToArea.Dot(vForward), -1.f, 1.f);
			flAheadPenalty = tProfile.m_bPreferAhead ? (1.f - flAheadDot) * 120.f : (1.f + flAheadDot) * 120.f;
		}

		float flScore = flRangePenalty * tProfile.m_flRangeWeight + flAnchorPenalty + flTravelPenalty * tProfile.m_flTravelWeight + flAheadPenalty;
		flScore += std::clamp(flHazardCost, 0.f, 8000.f) * tProfile.m_flHazardWeight;
		if (tProfile.m_bPreferBackstab)
			flScore += GetBackstabScore(vAreaOrigin, vPredictedOrigin, vForward);

		vCandidates.push_back({ &tArea, flScore });
	}

	std::sort(vCandidates.begin(), vCandidates.end(), [](const StalkCandidate_t& a, const StalkCandidate_t& b) -> bool
		{
			return a.m_flScore < b.m_flScore;
		});

	const size_t nTraceCandidates = std::min<size_t>(vCandidates.size(), 28);
	for (size_t i = 0; i < nTraceCandidates; i++)
	{
		StalkCandidate_t& tCandidate = vCandidates[i];
		Vector vAreaOrigin = tCandidate.m_pArea->m_vCenter + Vector(0.f, 0.f, PLAYER_CROUCHED_JUMP_HEIGHT);
		const bool bVisible = TraceVisible(vTargetOrigin, vAreaOrigin);
		tCandidate.m_flScore += bVisible == tProfile.m_bPreferSightline ? -160.f : 180.f;
		tCandidate.m_flScore += GetHidingSpotCoverScore(tCandidate.m_pArea, vTargetOrigin, tProfile.m_bPreferSightline) * tProfile.m_flCoverWeight;

		const float flPathCost = F::NavEngine.GetPathCost(pLocalArea, tCandidate.m_pArea);
		if (!std::isfinite(flPathCost))
			tCandidate.m_flScore += 100000.f;
		else
			tCandidate.m_flScore += std::clamp(flPathCost, 0.f, 6000.f) * 0.05f;
	}

	std::sort(vCandidates.begin(), vCandidates.begin() + nTraceCandidates, [](const StalkCandidate_t& a, const StalkCandidate_t& b) -> bool
		{
			return a.m_flScore < b.m_flScore;
		});

	const size_t nFaceCandidates = std::min<size_t>(nTraceCandidates, 8);
	for (size_t i = 0; i < nFaceCandidates; i++)
		vCandidates[i].m_flScore += GetFaceCoverScore(iEntIndex, vCandidates[i].m_pArea, vTargetOrigin, tProfile.m_bPreferSightline) * tProfile.m_flCoverWeight;

	std::vector<NavAreaScore_t> vGoodAreas{};
	vGoodAreas.reserve(nTraceCandidates);
	for (size_t i = 0; i < nTraceCandidates; i++)
		vGoodAreas.push_back({ vCandidates[i].m_pArea, vCandidates[i].m_flScore });

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

	return true;
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
