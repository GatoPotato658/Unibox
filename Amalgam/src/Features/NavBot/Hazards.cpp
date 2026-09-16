#include "Hazards.h"
#include "BotUtils.h"
#include "NavEngine.h"

inline float GetPlayerDangerRadius(int iClass)
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
	case HazardKind::SentryMedium:   return 70;
	case HazardKind::SentryLow:      return 50;
	case HazardKind::EnemyNormal:    return 30;
	case HazardKind::EnemyDormant:   return 20;
	default:                         return 0;
	}
}

bool CHazards::RecordHazard(CNavArea* pArea, HazardKind eKind, HazardPolicy ePolicy, float flCost, const Vector& vOrigin, int iExpireTick)
{
	if (!pArea) return false;

	auto& tHazard = m_mAreaHazards[pArea];
	const bool bWasAbsent = tHazard.m_eKind == HazardKind::None;
	const int iIncomingPriority = PriorityForKind(eKind);
	const int iExistingPriority = PriorityForKind(tHazard.m_eKind);

	if (iIncomingPriority < iExistingPriority)
		return false;
	tHazard.m_iLastUpdateTick = m_iLastUpdateTick;

	const bool bMaterialChange =
		bWasAbsent
		|| tHazard.m_eKind != eKind
		|| tHazard.m_ePolicy != ePolicy;

	tHazard.m_eKind = eKind;
	tHazard.m_ePolicy = ePolicy;
	tHazard.m_flCost = std::max(tHazard.m_flCost, flCost);
	tHazard.m_vOrigin = vOrigin;
	if (iExpireTick) tHazard.m_iExpireTick = std::max(tHazard.m_iExpireTick, iExpireTick);

	return bMaterialChange;
}

void CHazards::AddHazard(CNavArea* pArea, HazardKind eKind, float flCost, int iExpireTick, HazardPolicy ePolicy)
{
	if (!pArea) return;
	if (flCost <= 0.f) flCost = CostForKind(eKind);

	if (RecordHazard(pArea, eKind, ePolicy, flCost, pArea->m_vCenter, iExpireTick))
		++m_iGenerationId;
}

void CHazards::ClearByKind(HazardKind eKind)
{
	const size_t nBefore = m_mAreaHazards.size();
	std::erase_if(m_mAreaHazards, [eKind](const auto& e) { return e.second.m_eKind == eKind; });
	if (m_pStandingHazardArea && !m_mAreaHazards.contains(m_pStandingHazardArea))
		m_pStandingHazardArea = nullptr;
	if (m_mAreaHazards.size() != nBefore) ++m_iGenerationId;
}

void CHazards::ClearAll()
{
	if (m_mAreaHazards.empty()) { m_pStandingHazardArea = nullptr; return; }
	m_mAreaHazards.clear();
	m_pStandingHazardArea = nullptr;
	++m_iGenerationId;
}

void CHazards::Reset()
{
	m_mAreaHazards.clear();
	m_iGenerationId = 1;
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

bool CHazards::IsHardBlocked(CNavArea* pArea) const
{
	if (!pArea || pArea == m_pStandingHazardArea) return false;
	auto it = m_mAreaHazards.find(pArea);
	if (it == m_mAreaHazards.end()) return false;
	return it->second.m_ePolicy == HazardPolicy::HardBlock || it->second.m_ePolicy == HazardPolicy::TempForbid;
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
	bool bAnyChange = false;
	const int iNow = I::GlobalVars->tickcount;

	std::erase_if(m_mAreaHazards, [&](const auto& e)
		{
			const auto& tHazard = e.second;
			const bool bExpiredByTick = tHazard.m_iExpireTick && tHazard.m_iExpireTick < iNow;
			const bool bStale = std::abs(iNow - tHazard.m_iLastUpdateTick) > TIME_TO_TICKS(2.0f);
			if (bExpiredByTick || bStale) { bAnyChange = true; return true; }
			return false;
		});
	if (m_pStandingHazardArea && !m_mAreaHazards.contains(m_pStandingHazardArea))
		m_pStandingHazardArea = nullptr;

	if (bAnyChange) ++m_iGenerationId;
}

void CHazards::Update(CTFPlayer* pLocal)
{
	if (!pLocal || !F::NavEngine.IsNavMeshLoaded()) return;

	static Timer tUpdate;
	if (!tUpdate.Run(0.1f)) return;

	m_iLastUpdateTick = I::GlobalVars->tickcount;
	ExpireStale();

	m_flPlayerScanRadius = GetPlayerDangerRadius(pLocal->m_iClass());

	UpdatePlayers(pLocal);
	UpdateBuildings(pLocal);
	UpdateProjectiles(pLocal);
}

void CHazards::UpdatePlayers(CTFPlayer* pLocal)
{
	const auto eBlMask = Vars::Misc::Movement::NavBot::Blacklist.Value;
	if (!(eBlMask & Vars::Misc::Movement::NavBot::BlacklistEnum::Players))
		return;

	auto* pMap = F::NavEngine.GetNavMap();
	if (!pMap) return;

	bool bAnyChange = false;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer || !pPlayer->IsAlive() || pPlayer == pLocal) continue;

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

			bAnyChange |= RecordHazard(pArea, eKind, HazardPolicy::SoftCost, flFinal, vOrigin, 0);
		}
	}

	if (bAnyChange) ++m_iGenerationId;
}

void CHazards::UpdateBuildings(CTFPlayer* pLocal)
{
	const auto eBlMask = Vars::Misc::Movement::NavBot::Blacklist.Value;
	if (!(eBlMask & Vars::Misc::Movement::NavBot::BlacklistEnum::Sentries))
		return;

	auto* pMap = F::NavEngine.GetNavMap();
	if (!pMap) return;

	bool bAnyChange = false;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (!pBuilding || pBuilding->m_iHealth() <= 0) continue;
		if (pBuilding->GetClassID() != ETFClassID::CObjectSentrygun) continue;

		auto pSentry = pBuilding->As<CObjectSentrygun>();
		if (!pSentry || pSentry->m_iState() == SENTRY_STATE_INACTIVE) continue;

		const bool bStrongClass = pLocal->m_iClass() == TF_CLASS_HEAVY || pLocal->m_iClass() == TF_CLASS_SOLDIER;
		if (bStrongClass && (pSentry->m_bMiniBuilding() || pSentry->m_iUpgradeLevel() == 1))
			continue;

		const int iBullets = pSentry->m_iAmmoShells();
		const int iRockets = pSentry->m_iAmmoRockets();
		if (iBullets == 0 && (pSentry->m_iUpgradeLevel() != 3 || iRockets == 0))
			continue;

		if ((!pSentry->m_bCarryDeploy() && pSentry->m_bBuilding()) || pSentry->m_bPlacing() || pSentry->m_bHasSapper())
			continue;

		const float flBaseScore = pSentry->m_bMiniBuilding() ? HAZARD_COST_SENTRY * 0.8f : HAZARD_COST_SENTRY;
		const Vector vOrigin = pSentry->GetAbsOrigin();
		const Vector vEyePos = vOrigin + Vector(0, 0, 40.f);

		constexpr float flHighRadius = 900.0f;
		constexpr float flMedRadius = 1050.0f;
		constexpr float flLowRadius = 1200.0f;

		std::vector<CNavArea*> vAreas;
		pMap->CollectAreasAround(vOrigin, flLowRadius, vAreas);

		for (auto* pArea : vAreas)
		{
			if (!pArea) continue;
			const float flDist = pArea->m_vCenter.DistTo(vOrigin);
			if (flDist > flLowRadius) continue;

			if (!F::NavEngine.IsVectorVisibleNavigation(vEyePos, pArea->m_vCenter + Vector(0, 0, 40), MASK_SHOT | CONTENTS_GRATE))
				continue;

			HazardKind eKind = HazardKind::SentryLow;
			float flScore = HAZARD_COST_SENTRY_LOW;
			if (flDist <= flHighRadius) { eKind = HazardKind::Sentry;       flScore = flBaseScore; }
			else if (flDist <= flMedRadius) { eKind = HazardKind::SentryMedium; flScore = HAZARD_COST_SENTRY_MEDIUM; }
			else if (bStrongClass)              continue;

			bAnyChange |= RecordHazard(pArea, eKind, HazardPolicy::SoftCost, flScore, vOrigin, 0);
		}
	}

	if (bAnyChange) ++m_iGenerationId;
}

void CHazards::UpdateProjectiles(CTFPlayer* pLocal)
{
	const auto eBlMask = Vars::Misc::Movement::NavBot::Blacklist.Value;
	if (!(eBlMask & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies))
		return;

	auto* pMap = F::NavEngine.GetNavMap();
	if (!pMap) return;

	const int iExpireTick = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::StickyIgnoreTime.Value);
	bool bAnyChange = false;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (pEntity->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile) continue;
		if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum()) continue;

		auto pPipe = pEntity->As<CTFGrenadePipebombProjectile>();
		if (!pPipe || !pPipe->HasStickyEffects() || pPipe->IsDormant() || !pPipe->m_vecVelocity().IsZero(1.f))
			continue;

		constexpr float flRadius = 150.0f;
		std::vector<CNavArea*> vAreas;
		pMap->CollectAreasAround(pPipe->GetAbsOrigin(), flRadius, vAreas);

		for (auto* pArea : vAreas)
		{
			if (!pArea) continue;
			bAnyChange |= RecordHazard(pArea, HazardKind::Sticky, HazardPolicy::SoftCost, HAZARD_COST_STICKY, pPipe->GetAbsOrigin(), iExpireTick);
		}
	}

	if (bAnyChange) ++m_iGenerationId;
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
		case HazardKind::EnemyDormant:   tColor = { 255, 128,   0, 255 }; break;
		case HazardKind::Sticky:         tColor = { 255, 255,   0, 255 }; break;
		default:                          tColor = Vars::Colors::NavbotBlacklist.Value; break;
		}

		H::Draw.RenderBox(pArea->m_vCenter, Vector(-6.f, -6.f, -6.f), Vector(6.f, 6.f, 6.f), Vector(), tColor, false);
		if (tHazard.m_flCost >= HAZARD_COST_SENTRY * 0.9f)
			H::Draw.RenderWireframeBox(pArea->m_vCenter, Vector(-6.f, -6.f, -6.f), Vector(6.f, 6.f, 6.f), Vector(), tColor, false);
	}
}
