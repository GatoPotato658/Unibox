#include "Hazards.h"
#include "BotUtils.h"
#include "NavEngine.h"
#include "Objectives.h"

static float GetPlayerDangerRadius(int iClass)
{
	switch (iClass)
	{
	case TF_CLASS_SCOUT:
	case TF_CLASS_HEAVY:
	case TF_CLASS_ENGINEER:  return 350.f;
	case TF_CLASS_SNIPER:    return 600.f;
	default:                 return 500.f;
	}
}

float CHazards::CostForKind(HazardKind eKind)
{
	switch (eKind)
	{
	case HazardKind::Sentry:         return HAZARD_COST_SENTRY;
	case HazardKind::SentryMedium:   return HAZARD_COST_SENTRY_MEDIUM;
	case HazardKind::SentryLow:      return HAZARD_COST_SENTRY_LOW;
	case HazardKind::EnemyInvuln:    return HAZARD_COST_ENEMY_INVULN;
	case HazardKind::Sticky:         return HAZARD_COST_STICKY;
	case HazardKind::Boss:           return HAZARD_COST_BOSS;
	case HazardKind::EnemyNormal:    return HAZARD_COST_ENEMY_NORMAL;
	case HazardKind::EnemyDormant:   return HAZARD_COST_ENEMY_DORMANT;
	default:                         return 0.f;
	}
}

int CHazards::PriorityForKind(HazardKind eKind)
{
	switch (eKind)
	{
	case HazardKind::Sentry:         return 100;
	case HazardKind::EnemyInvuln:    return 90;
	case HazardKind::Sticky:         return 80;
	case HazardKind::Boss:           return 85;
	case HazardKind::SentryMedium:   return 70;
	case HazardKind::SentryLow:      return 50;
	case HazardKind::EnemyNormal:    return 30;
	case HazardKind::EnemyDormant:   return 20;
	default:                         return 0;
	}
}

void CHazards::RecordHazard(CNavArea* pArea, HazardKind eKind, HazardPolicy ePolicy, float flCost, const Vector& vOrigin, int iExpireTick)
{
	if (!pArea) return;

	auto& tHazard = m_mAreaHazards[pArea];
	if (PriorityForKind(eKind) < PriorityForKind(tHazard.m_eKind))
		return;

	tHazard.m_iLastUpdateTick = m_iLastUpdateTick;
	tHazard.m_eKind = eKind;
	tHazard.m_ePolicy = ePolicy;
	tHazard.m_flCost = std::max(tHazard.m_flCost, flCost);
	tHazard.m_vOrigin = vOrigin;
	if (iExpireTick) tHazard.m_iExpireTick = std::max(tHazard.m_iExpireTick, iExpireTick);
}

void CHazards::Reset()
{
	m_mAreaHazards.clear();
	m_mSentryCoverage.clear();
	m_flStandingEyeHeight = TFGame::VIEW_HEIGHT_DEFAULT;
	m_iLastUpdateTick = 0;
	m_pStandingHazardArea = nullptr;
	m_bIgnoreSentries = false;
}

float CHazards::GetCost(CNavArea* pArea) const
{
	if (!pArea || pArea == m_pStandingHazardArea) return 0.f;

	auto it = m_mAreaHazards.find(pArea);
	if (it == m_mAreaHazards.end()) return 0.f;

	const auto& tHazard = it->second;
	if (m_bIgnoreSentries
		&& (tHazard.m_eKind == HazardKind::Sentry
		|| tHazard.m_eKind == HazardKind::SentryMedium
		|| tHazard.m_eKind == HazardKind::SentryLow))
		return 0.f;

	if (tHazard.m_ePolicy == HazardPolicy::HardBlock || tHazard.m_ePolicy == HazardPolicy::TempForbid)
		return std::numeric_limits<float>::infinity();

	return tHazard.m_flCost;
}

const Hazard_t* CHazards::GetHazard(CNavArea* pArea) const
{
	const auto it = m_mAreaHazards.find(pArea);
	return it != m_mAreaHazards.end() ? &it->second : nullptr;
}

void CHazards::SnapshotCosts(std::unordered_map<CNavArea*, float>& mOut) const
{
	mOut.clear();
	mOut.reserve(m_mAreaHazards.size());
	for (const auto& [pArea, tHazard] : m_mAreaHazards)
	{
		if (pArea == m_pStandingHazardArea)
			continue;
		if (m_bIgnoreSentries
			&& (tHazard.m_eKind == HazardKind::Sentry
			|| tHazard.m_eKind == HazardKind::SentryMedium
			|| tHazard.m_eKind == HazardKind::SentryLow))
			continue;

		if (tHazard.m_ePolicy == HazardPolicy::HardBlock || tHazard.m_ePolicy == HazardPolicy::TempForbid)
			mOut[pArea] = std::numeric_limits<float>::infinity();
		else
			mOut[pArea] = tHazard.m_flCost;
	}
}

bool CHazards::HasHazard(CNavArea* pArea) const
{
	if (!pArea) return false;
	return m_mAreaHazards.contains(pArea);
}

void CHazards::UpdateBotStanding(CNavArea* pLocalArea)
{
	if (!pLocalArea) { m_pStandingHazardArea = nullptr; return; }
	auto it = m_mAreaHazards.find(pLocalArea);
	m_pStandingHazardArea = it != m_mAreaHazards.end()
		&& it->second.m_ePolicy != HazardPolicy::SoftCost
		&& PriorityForKind(it->second.m_eKind) >= PriorityForKind(HazardKind::SentryMedium)
		? pLocalArea : nullptr;
}

void CHazards::ExpireStale()
{
	const int iNow = I::GlobalVars->tickcount;

	std::erase_if(m_mAreaHazards, [iNow](const auto& e)
		{
			const auto& tHazard = e.second;
			const bool bExpiredByTick = tHazard.m_iExpireTick && tHazard.m_iExpireTick < iNow;
			const bool bStale = std::abs(iNow - tHazard.m_iLastUpdateTick) > TIME_TO_TICKS(2.0f);
			return bExpiredByTick || bStale;
		});
	if (m_pStandingHazardArea && !m_mAreaHazards.contains(m_pStandingHazardArea))
		m_pStandingHazardArea = nullptr;
}

void CHazards::Update(CTFPlayer* pLocal)
{
	if (!pLocal || !F::NavEngine.IsNavMeshLoaded()) return;

	static Timer tUpdate;
	if (!tUpdate.Run(0.1f)) return;

	m_iLastUpdateTick = I::GlobalVars->tickcount;
	ExpireStale();

	m_flPlayerScanRadius = GetPlayerDangerRadius(pLocal->m_iClass());

	if (!F::ZIController.IsZombie())
	{
		UpdatePlayers(pLocal);
		UpdateBoss(pLocal);
		UpdateBuildings(pLocal);
	}
	UpdateProjectiles(pLocal);
}

void CHazards::UpdatePlayers(CTFPlayer* pLocal)
{
	const auto eBlMask = Vars::Misc::Movement::NavBot::Blacklist.Value;
	if (!(eBlMask & Vars::Misc::Movement::NavBot::BlacklistEnum::Players))
		return;

	auto* pMap = F::NavEngine.GetNavMap();
	if (!pMap) return;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer || !pPlayer->IsAlive() || pPlayer == pLocal || F::VSHController.IsBossLocal() || F::VSHController.IsBoss(pPlayer->entindex())) continue;

		const bool bDormant = pPlayer->IsDormant();
		const bool bInvuln = pPlayer->InCond(TF_COND_INVULNERABLE) || pPlayer->InCond(TF_COND_PHASE);

		HazardKind eKind = bInvuln ? HazardKind::EnemyInvuln
			: bDormant ? HazardKind::EnemyDormant
			: HazardKind::EnemyNormal;
		float flBaseScore = CostForKind(eKind);

		if (pPlayer->m_iClass() == TF_CLASS_SNIPER && !bDormant)
			flBaseScore *= 2.0f;

		Vector vOrigin = {};
		if (bDormant)
		{
			if (!F::BotUtils.GetDormantOrigin(pPlayer->entindex(), &vOrigin))
				continue;
		}
		else
		{
			vOrigin = pPlayer->GetAbsOrigin();
		}
		if (vOrigin.IsZero()) continue;

		const float flRadius = m_flPlayerScanRadius;
		std::vector<CNavArea*> vAreas;
		pMap->CollectAreasAround(vOrigin, flRadius, vAreas);

		for (auto* pArea : vAreas)
		{
			if (!pArea) continue;
			const float flDist = pArea->m_vCenter.DistTo(vOrigin);
			const float flDistFactor = 1.0f - std::clamp(flDist / flRadius, 0.f, 1.f);
			const float flFinal = flBaseScore * (0.5f + 0.5f * flDistFactor);

			if (!bDormant && !F::NavEngine.IsVectorVisibleNavigation(vOrigin + Vector(0, 0, 60), pArea->m_vCenter + Vector(0, 0, 40)))
				continue;

			RecordHazard(pArea, eKind, HazardPolicy::SoftCost, flFinal, vOrigin, 0);
		}
	}
}

static float DistToSegment2D(const Vector& vPoint, const Vector& vFrom, const Vector& vTo)
{
	const Vector2D vSegment(vTo.x - vFrom.x, vTo.y - vFrom.y);
	const Vector2D vDelta(vPoint.x - vFrom.x, vPoint.y - vFrom.y);
	const float flLengthSqr = vSegment.x * vSegment.x + vSegment.y * vSegment.y;
	const float flT = flLengthSqr > 0.001f ? std::clamp((vDelta.x * vSegment.x + vDelta.y * vSegment.y) / flLengthSqr, 0.f, 1.f) : 0.f;
	const float flDx = vDelta.x - vSegment.x * flT;
	const float flDy = vDelta.y - vSegment.y * flT;
	return std::sqrt(flDx * flDx + flDy * flDy);
}

void CHazards::UpdateBoss(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Players))
		return;

	auto* pMap = F::NavEngine.GetNavMap();
	if (!pMap) return;

	std::vector<VSHZone_t> vZones;
	F::VSHController.GetBossZones(pLocal, vZones);
	const int iExpireTick = I::GlobalVars->tickcount + TIME_TO_TICKS(0.35f);

	for (const auto& tZone : vZones)
	{
		const Vector vCenter = tZone.m_bLane ? (tZone.m_vFrom + tZone.m_vTo) * 0.5f : tZone.m_vFrom;
		const float flReach = tZone.m_bLane ? tZone.m_vFrom.DistTo(tZone.m_vTo) * 0.5f + tZone.m_flRadius : tZone.m_flRadius;

		std::vector<CNavArea*> vAreas;
		pMap->CollectAreasAround(vCenter, flReach, vAreas);

		for (auto* pArea : vAreas)
		{
			if (!pArea) continue;

			float flCost = tZone.m_bStrict ? HAZARD_COST_BOSS * 2.f : HAZARD_COST_BOSS;
			if (tZone.m_bLane)
			{
				if (DistToSegment2D(pArea->m_vCenter, tZone.m_vFrom, tZone.m_vTo) > tZone.m_flRadius)
					continue;
			}
			else
			{
				const float flDist = pArea->m_vCenter.DistTo(tZone.m_vFrom);
				if (flDist > tZone.m_flRadius || flDist < tZone.m_flInnerRadius)
					continue;

				if (!tZone.m_bStrict && flDist > 350.f
					&& !F::NavEngine.IsVectorVisibleNavigation(tZone.m_vFrom + Vector(0, 0, 60), pArea->m_vCenter + Vector(0, 0, 40)))
					continue;

				flCost *= 0.5f + 0.5f * (1.f - flDist / tZone.m_flRadius);
			}

			RecordHazard(pArea, HazardKind::Boss, HazardPolicy::SoftCost, flCost, tZone.m_vFrom, iExpireTick);
		}
	}
}

static bool IsLocalIgnoredBySentry(CTFPlayer* pLocal, CObjectSentrygun* pSentry)
{
	if (pLocal->m_iClass() != TF_CLASS_SPY)
		return false;

	auto pEnemy = pSentry->m_hEnemy().Get();
	if (pEnemy && pEnemy->entindex() == pLocal->entindex())
		return false;

	if (pLocal->m_flInvisibility() > TFGame::SENTRY_IGNORE_INVIS)
		return true;

	return pLocal->InCond(TF_COND_DISGUISED) && pLocal->m_nDisguiseTeam() == pSentry->m_iTeamNum();
}

void CHazards::UpdateBuildings(CTFPlayer* pLocal)
{
	const auto eBlMask = Vars::Misc::Movement::NavBot::Blacklist.Value;
	if (!(eBlMask & Vars::Misc::Movement::NavBot::BlacklistEnum::Sentries))
		return;

	auto* pMap = F::NavEngine.GetNavMap();
	if (!pMap) return;

	const float flViewHeight = pLocal->m_vecViewOffset().z;
	if (!(pLocal->m_fFlags() & FL_DUCKING) && flViewHeight > 0.f)
		m_flStandingEyeHeight = flViewHeight;
	const float flTargetEye = m_flStandingEyeHeight;

	const int iNow = I::GlobalVars->tickcount;
	const int iLocalClass = pLocal->m_iClass();
	const bool bStrongClass = iLocalClass == TF_CLASS_HEAVY || iLocalClass == TF_CLASS_SOLDIER;

	constexpr float flHighRadius = 900.0f;
	constexpr float flMedRadius = TFGame::SENTRY_MAX_RANGE;
	constexpr float flLowRadius = TFGame::SENTRY_MAX_RANGE + 100.0f;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (!pBuilding || pBuilding->m_iHealth() <= 0) continue;
		if (pBuilding->GetClassID() != ETFClassID::CObjectSentrygun) continue;

		auto pSentry = pBuilding->As<CObjectSentrygun>();
		if (!pSentry || pSentry->m_iState() == SENTRY_STATE_INACTIVE) continue;

		const bool bMini = pSentry->m_bMiniBuilding();
		const int iLevel = pSentry->m_iUpgradeLevel();
		if (bStrongClass && (bMini || iLevel == 1))
			continue;

		const int iBullets = pSentry->m_iAmmoShells();
		const int iRockets = pSentry->m_iAmmoRockets();
		if (iBullets == 0 && (iLevel != 3 || iRockets == 0))
			continue;

		if ((!pSentry->m_bCarryDeploy() && pSentry->m_bBuilding()) || pSentry->m_bPlacing() || pSentry->IsDisabled() || pSentry->m_bPlasmaDisable())
			continue;

		if (IsLocalIgnoredBySentry(pLocal, pSentry))
			continue;

		const Vector vOrigin = pSentry->GetAbsOrigin();
		auto& tCoverage = m_mSentryCoverage[pSentry->entindex()];
		tCoverage.m_iSeenTick = iNow;

		const bool bCacheValid = tCoverage.m_iExpireTick > iNow
			&& tCoverage.m_iExpireTick - iNow <= TIME_TO_TICKS(1.0f)
			&& tCoverage.m_vOrigin.DistToSqr(vOrigin) <= 1.f
			&& tCoverage.m_iLevel == iLevel
			&& tCoverage.m_bMini == bMini
			&& tCoverage.m_iLocalClass == iLocalClass
			&& std::fabs(tCoverage.m_flTargetEye - flTargetEye) <= 1.f;

		if (!bCacheValid)
		{
			tCoverage.m_vOrigin = vOrigin;
			tCoverage.m_iLevel = iLevel;
			tCoverage.m_bMini = bMini;
			tCoverage.m_iLocalClass = iLocalClass;
			tCoverage.m_flTargetEye = flTargetEye;
			tCoverage.m_iExpireTick = iNow + TIME_TO_TICKS(1.0f);
			tCoverage.m_vAreas.clear();

			const Vector vEyePos = vOrigin + Vector(0, 0, TFGame::SentryEyeOffset(iLevel));

			std::vector<CNavArea*> vAreas;
			pMap->CollectAreasAround(vOrigin, flLowRadius + flTargetEye, vAreas);

			for (auto* pArea : vAreas)
			{
				if (!pArea) continue;
				const Vector vTargetEye = pArea->m_vCenter + Vector(0, 0, flTargetEye);
				const float flDist = vTargetEye.DistTo(vEyePos);
				if (flDist > flLowRadius) continue;

				HazardKind eKind = HazardKind::SentryLow;
				if (flDist <= flHighRadius) eKind = HazardKind::Sentry;
				else if (flDist <= flMedRadius) eKind = HazardKind::SentryMedium;
				else if (bStrongClass) continue;

				if (!F::NavEngine.IsVectorVisibleNavigation(vEyePos, vTargetEye, MASK_SHOT | CONTENTS_GRATE))
					continue;

				tCoverage.m_vAreas.emplace_back(pArea, eKind);
			}
		}

		const float flBaseScore = bMini ? HAZARD_COST_SENTRY * 0.8f : HAZARD_COST_SENTRY;
		for (const auto& [pArea, eKind] : tCoverage.m_vAreas)
		{
			const float flScore = eKind == HazardKind::Sentry ? flBaseScore
				: eKind == HazardKind::SentryMedium ? HAZARD_COST_SENTRY_MEDIUM
				: HAZARD_COST_SENTRY_LOW;
			RecordHazard(pArea, eKind, HazardPolicy::SoftCost, flScore, vOrigin, 0);
		}
	}

	std::erase_if(m_mSentryCoverage, [iNow](const auto& tEntry) { return tEntry.second.m_iSeenTick != iNow; });
}

void CHazards::UpdateProjectiles(CTFPlayer* pLocal)
{
	const auto eBlMask = Vars::Misc::Movement::NavBot::Blacklist.Value;
	if (!(eBlMask & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies))
		return;

	auto* pMap = F::NavEngine.GetNavMap();
	if (!pMap) return;

	const int iExpireTick = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::StickyIgnoreTime.Value);

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (pEntity->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile) continue;
		if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum()) continue;

		auto pPipe = pEntity->As<CTFGrenadePipebombProjectile>();
		if (!pPipe || !pPipe->HasStickyEffects() || pPipe->IsDormant() || !pPipe->m_vecVelocity().IsZero(1.f))
			continue;

		const float flRadius = static_cast<float>(Vars::Misc::Movement::NavBot::StickyDangerRange.Value);
		std::vector<CNavArea*> vAreas;
		pMap->CollectAreasAround(pPipe->GetAbsOrigin(), flRadius, vAreas);

		for (auto* pArea : vAreas)
		{
			if (!pArea) continue;
			RecordHazard(pArea, HazardKind::Sticky, HazardPolicy::SoftCost, HAZARD_COST_STICKY, pPipe->GetAbsOrigin(), iExpireTick);
		}
	}
}

void CHazards::Render()
{
	if (!F::NavEngine.IsReady()) return;
	if (!(Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Blacklist))
		return;

	auto* pMap = F::NavEngine.GetNavMap();
	if (!pMap) return;

	for (auto& [pArea, tHazard] : m_mAreaHazards)
	{
		if (!pMap->IsAreaValid(pArea) || tHazard.m_flCost <= 0.f) continue;

		Color_t tColor;
		switch (tHazard.m_eKind)
		{
		case HazardKind::Sentry:
		case HazardKind::SentryMedium:
		case HazardKind::SentryLow:      tColor = { 255,   0,   0, 255 }; break;
		case HazardKind::EnemyInvuln:
		case HazardKind::EnemyNormal:
		case HazardKind::EnemyDormant:
		case HazardKind::Boss:           tColor = { 255, 128,   0, 255 }; break;
		case HazardKind::Sticky:         tColor = { 255, 255,   0, 255 }; break;
		default:                          tColor = Vars::Colors::NavbotBlacklist.Value; break;
		}

		H::Draw.RenderBox(pArea->m_vCenter, Vector(-6.f, -6.f, -6.f), Vector(6.f, 6.f, 6.f), Vector(), tColor, false);
		if (tHazard.m_flCost >= HAZARD_COST_SENTRY * 0.9f)
			H::Draw.RenderWireframeBox(pArea->m_vCenter, Vector(-6.f, -6.f, -6.f), Vector(6.f, 6.f, 6.f), Vector(), tColor, false);
	}
}
