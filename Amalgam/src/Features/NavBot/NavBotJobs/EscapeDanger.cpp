#include "NavBotJobs.h"
#include "../NavBotCore.h"
#include "../NavEngine/Controllers/FlagController/FlagController.h"
#include "../NavEngine/Controllers/Controller.h"

inline bool IsHighDangerReason(BlacklistReasonEnum::BlacklistReasonEnum eReason)
{
	return eReason == BlacklistReasonEnum::Sentry || eReason == BlacklistReasonEnum::Sticky || eReason == BlacklistReasonEnum::EnemyInvuln;
}

inline bool IsMediumDangerReason(BlacklistReasonEnum::BlacklistReasonEnum eReason)
{
	return eReason == BlacklistReasonEnum::SentryMedium || eReason == BlacklistReasonEnum::EnemyNormal;
}

inline bool CanUseDangerArea(BlacklistReasonEnum::BlacklistReasonEnum eReason, bool bHasTarget, bool bLowHealth)
{
	if (IsHighDangerReason(eReason))
		return false;

	if (IsMediumDangerReason(eReason))
		return bHasTarget && !bLowHealth;

	return true;
}

bool CNavBotDanger::EscapeDanger(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::EscapeDanger))
		return false;

	// Don't escape while we have the intel
	if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DontEscapeDangerIntel && F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_CTF)
	{
		auto iFlagCarrierIdx = F::FlagController.GetCarrier(pLocal->m_iTeamNum());
		if (iFlagCarrierIdx == pLocal->entindex())
			return false;
	}

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger ||
		F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack ||
		F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload)
		return false;


	// Check if we're in spawn - if so, ignore danger and focus on getting out
	auto pLocalArea = F::NavEngine.GetLocalNavArea();
	if (pLocalArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED ||
		pLocalArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE)
		return false;

	auto pBlacklist = F::NavEngine.GetFreeBlacklist();

	// Check if we're in any danger
	bool bInHighDanger = false;
	bool bInMediumDanger = false;
	bool bInLowDanger = false;

	if (pBlacklist && pBlacklist->contains(pLocalArea))
	{
		const bool bActiveEscapeJob = F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger;
		static Timer tRepathCooldown{};
		if (bActiveEscapeJob && F::NavEngine.IsPathing() && !tRepathCooldown.Run(0.35f))
			return true;

		// Check building spot - don't run away from that
		if ((*pBlacklist)[pLocalArea].m_eValue == BlacklistReasonEnum::BadBuildSpot)
			return false;

		// Determine danger level
		switch ((*pBlacklist)[pLocalArea].m_eValue)
		{
		case BlacklistReasonEnum::Sentry:
		case BlacklistReasonEnum::Sticky:
		case BlacklistReasonEnum::EnemyInvuln:
			bInHighDanger = true;
			break;
		case BlacklistReasonEnum::SentryMedium:
		case BlacklistReasonEnum::EnemyNormal:
			bInMediumDanger = true;
			break;
		case BlacklistReasonEnum::SentryLow:
		case BlacklistReasonEnum::EnemyDormant:
			bInLowDanger = true;
			break;
		}

		// Only escape from high danger by default
		// Also escape from medium danger if health is low
		bool bShouldEscape = bInHighDanger ||
			(bInMediumDanger && pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f);

		// If we're not in high danger and on an important task, we might not need to escape
		bool bImportantTask = (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture ||
			F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetHealth ||
			F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer);

		if (!bShouldEscape && bImportantTask)
			return false;

		// If we're in low danger only and on any task, don't escape
		if (bInLowDanger && !bInMediumDanger && !bInHighDanger && F::NavEngine.m_eCurrentPriority != 0)
			return false;

		static CNavArea* pTargetArea = nullptr;
		// Already escaping and our target is still valid: keep moving, but recover if pathing was lost.
		if (bActiveEscapeJob && pTargetArea && !pBlacklist->contains(pTargetArea))
		{
			if (F::NavEngine.IsPathing())
				return true;

			if (F::NavEngine.NavTo(pTargetArea->m_vCenter, PriorityListEnum::EscapeDanger))
				return true;
		}

		// Determine the reference position to stay close to
		Vector vReferencePosition;
		bool bHasTarget = false;

		// If we were pursuing a specific objective or following a target, try to stay close to it
		if (F::NavEngine.m_eCurrentPriority != 0 && F::NavEngine.m_eCurrentPriority != PriorityListEnum::EscapeDanger && F::NavEngine.IsPathing())
		{
			// Use the last crumb in our path as the reference position
			vReferencePosition = F::NavEngine.GetCrumbs()->back().m_vPos;
			bHasTarget = true;
		}
		else
		{
			// Use current position if we don't have a target
			vReferencePosition = pLocal->GetAbsOrigin();
		}

		std::vector<NavAreaScore_t> vSafeAreas;
		std::vector<CNavArea*> vAreaPointers;

		// Find areas around current position to escape to
		F::NavEngine.GetNavMap()->CollectAreasAround(pLocal->GetAbsOrigin(), 1500.f, vAreaPointers);

		for (auto& pArea : vAreaPointers)
		{
			// Skip if area is blacklisted with high danger
			auto it = pBlacklist->find(pArea);
			if (it != pBlacklist->end())
			{
				if (!CanUseDangerArea(it->second.m_eValue, bHasTarget, pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f))
					continue;
			}

			float flDistToReference = pArea->m_vCenter.DistTo(vReferencePosition);
			float flDistToCurrent = pArea->m_vCenter.DistTo(pLocal->GetAbsOrigin());

			// Only consider areas that are reachable and not too close to the current dangerous area
			if (flDistToCurrent > 200.f)
			{
				// If we have a target, prioritize staying near it
				float flScore = bHasTarget ? flDistToReference : flDistToCurrent;
				vSafeAreas.push_back({ pArea, flScore });
			}
		}

		// Sort by score (closer to reference position is better)
		std::sort(vSafeAreas.begin(), vSafeAreas.end(), [](const NavAreaScore_t& a, const NavAreaScore_t& b) -> bool
			{
				return a.m_flScore < b.m_flScore;
			});

		int iCalls = 0;
		// Try to path to safe areas
		for (const auto& tPair : vSafeAreas)
		{
			CNavArea* pArea = tPair.m_pArea;
			iCalls++;
			if (iCalls > 10)
				break;

			// Check if this area is safe (not near enemy)
			bool bIsSafe = true;
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
			{
				if (!F::BotUtils.ShouldTarget(pLocal, pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>(), pEntity->entindex()))
					continue;

				// If enemy is too close to this area, mark it as unsafe
				float flDist = pEntity->GetAbsOrigin().DistTo(pArea->m_vCenter);
				if (flDist < F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger * 1.2f)
				{
					bIsSafe = false;
					break;
				}
			}

			// Skip unsafe areas
			if (!bIsSafe)
				continue;

			if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::EscapeDanger))
			{
				pTargetArea = pArea;
				return true;
			}
		}

		// If we couldn't find a safe area close to the target, fall back to any safe area
		if (iCalls <= 0 || (bInHighDanger && iCalls < 10))
		{
			// Sort by distance to player
			std::sort(vAreaPointers.begin(), vAreaPointers.end(), [&](CNavArea* a, CNavArea* b) -> bool
				{
					return a->m_vCenter.DistTo(pLocal->GetAbsOrigin()) < b->m_vCenter.DistTo(pLocal->GetAbsOrigin());
				});

			// Try to path to any non-blacklisted area
			for (auto& pArea : vAreaPointers)
			{
				auto it = pBlacklist->find(pArea);
				if (it == pBlacklist->end() ||
					(bInHighDanger && !IsHighDangerReason(it->second.m_eValue) && !IsMediumDangerReason(it->second.m_eValue)))
				{
					iCalls++;
					if (iCalls > 5)
						break;
					if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::EscapeDanger))
					{
						pTargetArea = pArea;
						return true;
					}
				}
			}
		}
	}
	// No longer in danger
	else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger)
		F::NavEngine.CancelPath();

	return false;
}

// Check if a position is safe from stickies and projectiles
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
		// Check for stickies
		if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies && iClassId == ETFClassID::CTFGrenadePipebombProjectile)
		{
			// Skip non-sticky projectiles
			if (pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() != TF_GL_MODE_REMOTE_DETONATE)
				continue;

			float flDist = pEntity->m_vecOrigin().DistTo(vPos);
			if (flDist < Vars::Misc::Movement::NavBot::StickyDangerRange.Value)
				return false;
		}

		// Check for rockets and pipes
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
	static CNavArea* pProjectileTargetArea = nullptr;

	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
		!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
		return false;

	// Don't interrupt higher priority tasks
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger)
		return false;

	// Check if current position is unsafe
	if (IsPositionSafe(pLocal->GetAbsOrigin(), pLocal->m_iTeamNum()))
	{
		pProjectileTargetArea = nullptr;
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger)
			F::NavEngine.CancelPath();
		return false;
	}

	const bool bActiveEscapeJob = F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger;
	static Timer tProjectileRepathCooldown{};
	if (bActiveEscapeJob && F::NavEngine.IsPathing() && !tProjectileRepathCooldown.Run(0.35f))
		return true;

	if (bActiveEscapeJob && pProjectileTargetArea &&
		F::NavEngine.GetFreeBlacklist()->find(pProjectileTargetArea) == F::NavEngine.GetFreeBlacklist()->end() &&
		IsPositionSafe(pProjectileTargetArea->m_vCenter, pLocal->m_iTeamNum()))
	{
		if (F::NavEngine.IsPathing())
			return true;

		if (F::NavEngine.NavTo(pProjectileTargetArea->m_vCenter, PriorityListEnum::EscapeDanger))
			return true;
	}

	auto pLocalArea = F::NavEngine.GetLocalNavArea();

	// Find safe nav areas sorted by distance
	std::vector<NavAreaScore_t> vSafeAreas;
	std::vector<CNavArea*> vAreaPointers;

	F::NavEngine.GetNavMap()->CollectAreasAround(pLocal->GetAbsOrigin(), 1000.f, vAreaPointers);

	for (auto& pArea : vAreaPointers)
	{
		// Skip current area
		if (pArea == pLocalArea)
			continue;

		// Skip if area is blacklisted
		if (F::NavEngine.GetFreeBlacklist()->find(pArea) != F::NavEngine.GetFreeBlacklist()->end())
			continue;

		if (IsPositionSafe(pArea->m_vCenter, pLocal->m_iTeamNum()))
		{
			float flDist = pArea->m_vCenter.DistTo(pLocal->GetAbsOrigin());
			vSafeAreas.push_back({ pArea, flDist });
		}
	}

	// Sort by distance
	std::sort(vSafeAreas.begin(), vSafeAreas.end(),
		[](const NavAreaScore_t& a, const NavAreaScore_t& b)
		{
			return a.m_flScore < b.m_flScore;
		});

	// Try to path to closest safe area
	for (const auto& tAreaScore : vSafeAreas)
	{
		if (F::NavEngine.NavTo(tAreaScore.m_pArea->m_vCenter, PriorityListEnum::EscapeDanger))
		{
			pProjectileTargetArea = tAreaScore.m_pArea;
			return true;
		}
	}

	return false;
}

bool CNavBotDanger::EscapeSpawn(CTFPlayer* pLocal)
{
	// Cancel if we're not in spawn and this is running
	if (!(F::NavEngine.GetLocalNavArea()->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)))
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeSpawn)
			F::NavEngine.CancelPath();
		return false;
	}

	// Don't try too often
	static Timer tSpawnEscapeCooldown{};
	bool bActive = F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeSpawn;
	if (bActive || !tSpawnEscapeCooldown.Run(2.f))
		return bActive;

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	if (!m_pSpawnExitArea || m_pSpawnExitArea->m_vCenter.DistTo(vLocalOrigin) > 1500.f)
	{
		// Try to find a closest exit
		float flMinDist = FLT_MAX;	CNavArea* pClosest = nullptr;
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
		// Try to get there
		if (F::NavEngine.NavTo(m_pSpawnExitArea->m_vCenter, PriorityListEnum::EscapeSpawn))
			return true;
	}

	return false;
}

void CNavBotDanger::ResetSpawn()
{
	m_pSpawnExitArea = nullptr;
}
