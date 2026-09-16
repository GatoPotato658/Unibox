#include "NavBotJobs.h"
#include "../NavBotCore.h"
#include "../Hazards.h"
#include "../Objectives.h"

static bool IsHighDanger(const Hazard_t& tHazard)
{
	return tHazard.m_ePolicy != HazardPolicy::SoftCost
		|| tHazard.m_eKind == HazardKind::Sentry
		|| tHazard.m_eKind == HazardKind::Sticky
		|| tHazard.m_eKind == HazardKind::EnemyInvuln;
}

static bool IsMediumDanger(const Hazard_t& tHazard)
{
	return tHazard.m_eKind == HazardKind::SentryMedium || tHazard.m_eKind == HazardKind::EnemyNormal;
}

static bool CanUseDangerArea(const Hazard_t* pHazard, bool bHasTarget, bool bLowHealth)
{
	if (!pHazard)
		return true;

	if (IsHighDanger(*pHazard))
		return false;

	if (IsMediumDanger(*pHazard))
		return bHasTarget && !bLowHealth;

	return true;
}

bool CNavBotDanger::EscapeDanger(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::EscapeDanger))
		return false;

	if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DontEscapeDangerIntel && F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_CTF)
	{
		const int iEnemyTeam = pLocal->m_iTeamNum() == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
		auto iFlagCarrierIdx = F::FlagController.GetCarrier(iEnemyTeam);
		if (iFlagCarrierIdx == pLocal->entindex())
			return false;
	}

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger ||
		F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack ||
		F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload)
		return false;

	auto pLocalArea = F::NavEngine.GetLocalNavArea();
	if (!pLocalArea)
		return false;
	if (pLocalArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED ||
		pLocalArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE)
		return false;

	const Hazard_t* pLocalHazard = F::Hazards.GetHazard(pLocalArea);

	bool bInHighDanger = false;
	bool bInMediumDanger = false;
	bool bInLowDanger = false;

	if (pLocalHazard)
	{
		const bool bActiveEscapeJob = F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger;
		static Timer tRepathCooldown{};
		if (bActiveEscapeJob && F::NavEngine.IsPathing() && !tRepathCooldown.Run(0.35f))
			return true;

		bInHighDanger = IsHighDanger(*pLocalHazard);
		bInMediumDanger = IsMediumDanger(*pLocalHazard);
		bInLowDanger = !bInHighDanger && !bInMediumDanger;

		bool bShouldEscape = bInHighDanger ||
			(bInMediumDanger && pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f);

		bool bImportantTask = (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture ||
			F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetHealth ||
			F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer);

		if (!bShouldEscape && bImportantTask)
			return false;

		if (bInLowDanger && !bInMediumDanger && !bInHighDanger && F::NavEngine.m_eCurrentPriority != 0)
			return false;

		if (bActiveEscapeJob && m_pEscapeTargetArea && !F::Hazards.HasHazard(m_pEscapeTargetArea))
		{
			if (F::NavEngine.IsPathing())
				return true;

			if (F::NavEngine.NavTo(m_pEscapeTargetArea->m_vCenter, PriorityListEnum::EscapeDanger))
				return true;

			if (!m_tEscapeRefresh.Run(1.f))
				return true;
		}

		Vector vReferencePosition;
		bool bHasTarget = false;

		if (F::NavEngine.m_eCurrentPriority != 0 && F::NavEngine.m_eCurrentPriority != PriorityListEnum::EscapeDanger && F::NavEngine.IsPathing())
		{

			vReferencePosition = F::NavEngine.GetCrumbs()->back().m_vPos;
			bHasTarget = true;
		}
		else
		{

			vReferencePosition = pLocal->GetAbsOrigin();
		}

		std::vector<NavAreaScore_t> vSafeAreas;
		std::vector<CNavArea*> vAreaPointers;

		F::NavEngine.GetNavMap()->CollectAreasAround(pLocal->GetAbsOrigin(), 1500.f, vAreaPointers);

		for (auto& pArea : vAreaPointers)
		{
			if (!CanUseDangerArea(F::Hazards.GetHazard(pArea), bHasTarget,
				pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f))
				continue;

			float flDistToReference = pArea->m_vCenter.DistTo(vReferencePosition);
			float flDistToCurrent = pArea->m_vCenter.DistTo(pLocal->GetAbsOrigin());

			if (flDistToCurrent > 200.f)
			{

				float flScore = bHasTarget ? flDistToReference : flDistToCurrent;
				vSafeAreas.push_back({ pArea, flScore });
			}
		}

		std::sort(vSafeAreas.begin(), vSafeAreas.end(), [](const NavAreaScore_t& a, const NavAreaScore_t& b) -> bool
			{
				return a.m_flScore < b.m_flScore;
			});

		int iCalls = 0;

		for (const auto& tPair : vSafeAreas)
		{
			CNavArea* pArea = tPair.m_pArea;
			iCalls++;
			if (iCalls > 10)
				break;

			bool bIsSafe = true;
			auto pWeaponEntity = pLocal->m_hActiveWeapon().Get();
			if (!pWeaponEntity)
				continue;
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
			{
				if (F::BotUtils.ShouldTarget(pLocal, pWeaponEntity->As<CTFWeaponBase>(), pEntity->entindex()) != ShouldTargetEnum::Target)
					continue;

				float flDist = pEntity->GetAbsOrigin().DistTo(pArea->m_vCenter);
				if (flDist < F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger * 1.2f)
				{
					bIsSafe = false;
					break;
				}
			}

			if (!bIsSafe)
				continue;

			if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::EscapeDanger))
			{
				m_pEscapeTargetArea = pArea;
				m_tEscapeRefresh.Update();
				return true;
			}
		}

		if (iCalls <= 0 || (bInHighDanger && iCalls < 10))
		{

			std::sort(vAreaPointers.begin(), vAreaPointers.end(), [&](CNavArea* a, CNavArea* b) -> bool
				{
					return a->m_vCenter.DistTo(pLocal->GetAbsOrigin()) < b->m_vCenter.DistTo(pLocal->GetAbsOrigin());
				});

			for (auto& pArea : vAreaPointers)
			{
				const Hazard_t* pHazard = F::Hazards.GetHazard(pArea);
				if (!pHazard || (bInHighDanger && !IsHighDanger(*pHazard) && !IsMediumDanger(*pHazard)))
				{
					iCalls++;
					if (iCalls > 5)
						break;
					if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::EscapeDanger))
					{
						m_pEscapeTargetArea = pArea;
						m_tEscapeRefresh.Update();
						return true;
					}
				}
			}
		}
	}

	else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger)
	{
		m_pEscapeTargetArea = nullptr;
		F::NavEngine.CancelPath();
	}

	return false;
}

static bool IsPositionSafe(Vector vPos, int iLocalTeam)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
		!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
		return true;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (pEntity->m_iTeamNum() == iLocalTeam)
			continue;

		auto iClassId = pEntity->GetClassID();

		if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies && iClassId == ETFClassID::CTFGrenadePipebombProjectile)
		{

			if (pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() != TF_GL_MODE_REMOTE_DETONATE)
				continue;

			float flDist = pEntity->m_vecOrigin().DistTo(vPos);
			if (flDist < Vars::Misc::Movement::NavBot::StickyDangerRange.Value)
				return false;
		}

		if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles)
		{
			if (iClassId == ETFClassID::CTFProjectile_Rocket ||
				(iClassId == ETFClassID::CTFGrenadePipebombProjectile && pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() == TF_GL_MODE_REGULAR))
			{
				float flDist = pEntity->m_vecOrigin().DistTo(vPos);
				if (flDist < Vars::Misc::Movement::NavBot::ProjectileDangerRange.Value)
					return false;
			}
		}
	}
	return true;
}

bool CNavBotDanger::EscapeProjectiles(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
		!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
		return false;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger)
		return false;

	if (IsPositionSafe(pLocal->GetAbsOrigin(), pLocal->m_iTeamNum()))
	{
		m_pProjectileTargetArea = nullptr;
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger)
			F::NavEngine.CancelPath();
		return false;
	}

	const bool bActiveEscapeJob = F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger;
	static Timer tProjectileRepathCooldown{};
	if (bActiveEscapeJob && F::NavEngine.IsPathing() && !tProjectileRepathCooldown.Run(0.35f))
		return true;

	if (bActiveEscapeJob && m_pProjectileTargetArea &&
		!F::Hazards.HasHazard(m_pProjectileTargetArea) &&
		IsPositionSafe(m_pProjectileTargetArea->m_vCenter, pLocal->m_iTeamNum()))
	{
		if (F::NavEngine.IsPathing())
			return true;

		if (F::NavEngine.NavTo(m_pProjectileTargetArea->m_vCenter, PriorityListEnum::EscapeDanger))
			return true;

		if (!m_tEscapeRefresh.Run(1.f))
			return true;
	}

	auto pLocalArea = F::NavEngine.GetLocalNavArea();

	std::vector<NavAreaScore_t> vSafeAreas;
	std::vector<CNavArea*> vAreaPointers;

	F::NavEngine.GetNavMap()->CollectAreasAround(pLocal->GetAbsOrigin(), 1000.f, vAreaPointers);

	for (auto& pArea : vAreaPointers)
	{

		if (pArea == pLocalArea)
			continue;

		if (F::Hazards.HasHazard(pArea))
			continue;

		if (IsPositionSafe(pArea->m_vCenter, pLocal->m_iTeamNum()))
		{
			float flDist = pArea->m_vCenter.DistTo(pLocal->GetAbsOrigin());
			vSafeAreas.push_back({ pArea, flDist });
		}
	}

	std::sort(vSafeAreas.begin(), vSafeAreas.end(),
		[](const NavAreaScore_t& a, const NavAreaScore_t& b)
		{
			return a.m_flScore < b.m_flScore;
		});

	for (const auto& tAreaScore : vSafeAreas)
	{
		if (F::NavEngine.NavTo(tAreaScore.m_pArea->m_vCenter, PriorityListEnum::EscapeDanger))
		{
			m_pProjectileTargetArea = tAreaScore.m_pArea;
			m_tEscapeRefresh.Update();
			return true;
		}
	}

	return false;
}

bool CNavBotDanger::EscapeSpawn(CTFPlayer* pLocal)
{
	CNavArea* pLocalArea = F::NavEngine.GetLocalNavArea();
	if (!pLocalArea)
		return false;

	if (!(pLocalArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)))
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeSpawn)
			F::NavEngine.CancelPath();
		return false;
	}

	static Timer tSpawnEscapeCooldown{};
	bool bActive = F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeSpawn;
	if (bActive || !tSpawnEscapeCooldown.Run(2.f))
		return bActive;

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	if (!m_pSpawnExitArea || m_pSpawnExitArea->m_vCenter.DistTo(vLocalOrigin) > 1500.f)
	{

		float flMinDist = FLT_MAX;
		for (auto pArea : *F::NavEngine.GetRespawnRoomExitAreas())
		{
			float flDist = pArea->m_vCenter.DistTo(vLocalOrigin);
			if (flMinDist > flDist)
			{
				m_pSpawnExitArea = pArea;
				flMinDist = flDist;
			}
		}
	}

	if (m_pSpawnExitArea)
	{

		if (F::NavEngine.NavTo(m_pSpawnExitArea->m_vCenter, PriorityListEnum::EscapeSpawn))
			return true;
	}

	return false;
}

void CNavBotDanger::ResetSpawn()
{
	m_pSpawnExitArea = nullptr;
	m_pEscapeTargetArea = nullptr;
	m_pProjectileTargetArea = nullptr;
}
