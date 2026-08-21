#include "NavBotJobs.h"
#include "../NavBotCore.h"

#include <algorithm>
#include <array>
#include <limits>

#include "../NavEngine/Controllers/Controller.h"
#include "../NavEngine/Controllers/FlagController/FlagController.h"

	using nav_priority_t = PriorityListEnum::PriorityListEnum;

	enum class job_kind_t
	{
		escape_spawn,
		escape_projectiles,
		escape_danger,
		get_health,
		engineer,
		run_reload,
		melee,
		get_ammo,
		capture,
		snipe_sentry,
		safe_reload,
		stay_near,
		low_prio_health,
		group_with_others,
		roam
	};

	struct job_candidate_t
	{
		job_kind_t m_eKind = {};
		float m_flScore = 0.f;
	};

	template <size_t nCount>
	static auto FindBestCandidate(std::array<job_candidate_t, nCount>& aCandidates) -> job_candidate_t*
	{
		job_candidate_t* pBestCandidate = nullptr;
		for (auto& tCandidate : aCandidates)
		{
			if (tCandidate.m_flScore <= 0.f)
				continue;

			if (!pBestCandidate || tCandidate.m_flScore > pBestCandidate->m_flScore)
				pBestCandidate = &tCandidate;
		}

		return pBestCandidate;
	}

	static auto get_active_priority_score(nav_priority_t ePriority, float flScore, float flBonus = 140.f, float flCleanupScore = 320.f) -> float
	{
		if (F::NavEngine.m_eCurrentPriority != ePriority)
			return flScore;

		return flScore > 0.f ? flScore + flBonus : flCleanupScore;
	}

	static auto has_reload_target() -> bool
	{
		return F::NavBotReload.m_iLastReloadSlot >= SLOT_PRIMARY && F::NavBotReload.m_iLastReloadSlot <= SLOT_SECONDARY;
	}

	static auto is_spawn_area(CNavArea* pArea) -> bool
	{
		return pArea && (pArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE));
	}

	static auto is_health_job_active() -> bool
	{
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetHealth;
	}

	static auto is_low_prio_health_job_active() -> bool
	{
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::LowPrioGetHealth;
	}

	static auto is_projectile_threat(CBaseEntity* pEntity, int iLocalTeam, float& flOutDistance) -> bool
	{
		if (!pEntity || pEntity->m_iTeamNum() == iLocalTeam)
			return false;

		const auto iClassId = pEntity->GetClassID();
		if ((Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
			iClassId == ETFClassID::CTFGrenadePipebombProjectile)
		{
			auto pPipebomb = pEntity->As<CTFGrenadePipebombProjectile>();
			if (pPipebomb->m_iType() != TF_GL_MODE_REMOTE_DETONATE)
				return false;

			flOutDistance = Vars::Misc::Movement::NavBot::StickyDangerRange.Value;
			return true;
		}

		if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
			return false;

		if (iClassId == ETFClassID::CTFProjectile_Rocket)
		{
			flOutDistance = Vars::Misc::Movement::NavBot::ProjectileDangerRange.Value;
			return true;
		}

		if (iClassId == ETFClassID::CTFGrenadePipebombProjectile)
		{
			auto pPipebomb = pEntity->As<CTFGrenadePipebombProjectile>();
			if (pPipebomb->m_iType() == TF_GL_MODE_REGULAR)
			{
				flOutDistance = Vars::Misc::Movement::NavBot::ProjectileDangerRange.Value;
				return true;
			}
		}

		return false;
	}

	static auto get_projectile_escape_score(CTFPlayer* pLocal) -> float
	{
		if (!pLocal ||
			(!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
			!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles)))
			return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);

		if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger &&
			F::NavEngine.m_eCurrentPriority != PriorityListEnum::EscapeDanger)
			return 0.f;

		const auto vLocalOrigin = pLocal->GetAbsOrigin();
		float flClosestThreat = std::numeric_limits<float>::max();
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
		{
			float flThreatRadius = 0.f;
			if (!is_projectile_threat(pEntity, pLocal->m_iTeamNum(), flThreatRadius))
				continue;

			const float flDist = pEntity->m_vecOrigin().DistTo(vLocalOrigin);
			if (flDist < flThreatRadius)
				flClosestThreat = std::min(flClosestThreat, flDist);
		}

		float flScore = 0.f;
		if (flClosestThreat < std::numeric_limits<float>::max())
			flScore = 1800.f + (400.f - std::min(flClosestThreat, 400.f)) * 0.5f;

		return get_active_priority_score(PriorityListEnum::EscapeDanger, flScore);
	}

	static auto get_escape_danger_score(CTFPlayer* pLocal) -> float
	{
		if (!pLocal)
			return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);

		if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::EscapeDanger))
			return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);

		if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DontEscapeDangerIntel &&
			F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_CTF)
		{
			const int iFlagCarrierIdx = F::FlagController.GetCarrier(pLocal->m_iTeamNum());
			if (iFlagCarrierIdx == pLocal->entindex())
				return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);
		}

		auto pLocalArea = F::NavEngine.GetLocalNavArea();
		auto pBlacklist = F::NavEngine.GetFreeBlacklist();
		if (!pLocalArea || !pBlacklist || is_spawn_area(pLocalArea))
			return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);

		const auto tIt = pBlacklist->find(pLocalArea);
		if (tIt == pBlacklist->end() || tIt->second.m_eValue == BlacklistReasonEnum::BadBuildSpot)
			return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);

		const float flHealth = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
		float flScore = 0.f;
		switch (tIt->second.m_eValue)
		{
		case BlacklistReasonEnum::Sentry:
		case BlacklistReasonEnum::Sticky:
		case BlacklistReasonEnum::EnemyInvuln:
			flScore = 1700.f;
			break;
		case BlacklistReasonEnum::SentryMedium:
		case BlacklistReasonEnum::EnemyNormal:
			flScore = flHealth < 0.5f ? 1425.f : 0.f;
			break;
		case BlacklistReasonEnum::SentryLow:
		case BlacklistReasonEnum::EnemyDormant:
			flScore = 0.f;
			break;
		default:
			break;
		}

		if (flScore <= 0.f && F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger)
			flScore = 300.f;

		return get_active_priority_score(PriorityListEnum::EscapeDanger, flScore);
	}

	static auto get_escape_spawn_score(CTFPlayer* pLocal) -> float
	{
		if (!Vars::Misc::Movement::NavBot::EscapeSpawn.Value)
			return 0.f;

		if (!pLocal)
			return get_active_priority_score(PriorityListEnum::EscapeSpawn, 0.f);

		const auto pLocalArea = F::NavEngine.GetLocalNavArea();
		if (!is_spawn_area(pLocalArea))
			return get_active_priority_score(PriorityListEnum::EscapeSpawn, 0.f);

		return get_active_priority_score(PriorityListEnum::EscapeSpawn, 2000.f);
	}

	static auto get_health_score(CTFPlayer* pLocal, bool bLowPrio) -> float
	{
		const auto ePriority = bLowPrio ? PriorityListEnum::LowPrioGetHealth : PriorityListEnum::GetHealth;
		if (!pLocal || !(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchHealth))
			return get_active_priority_score(ePriority, 0.f);

		const float flHealth = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
		const bool bHealing = pLocal->m_nPlayerCond() & (1 << 21);
		float flScore = 0.f;

		if (bLowPrio)
		{
			if (is_low_prio_health_job_active())
				flScore = flHealth < 0.92f ? 340.f + (0.92f - flHealth) * 400.f : 0.f;
			else if (!bHealing &&
				(F::NavEngine.m_eCurrentPriority <= PriorityListEnum::Patrol || is_low_prio_health_job_active()) &&
				flHealth <= 0.80f)
				flScore = 260.f + (0.80f - flHealth) * 350.f;
		}
		else
		{
			if (is_health_job_active())
				flScore = flHealth < 0.9f ? 900.f + (0.9f - flHealth) * 500.f : 0.f;
			else if (!bHealing)
			{
				if (flHealth < 0.25f)
					flScore = 1500.f;
				else if (flHealth < 0.40f)
					flScore = 1325.f;
				else if (flHealth < 0.64f)
					flScore = 1100.f;
			}
		}

		return get_active_priority_score(ePriority, flScore);
	}

	static auto get_ammo_score(CTFPlayer* pLocal) -> float
	{
		if (!pLocal || !(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchAmmo))
			return get_active_priority_score(PriorityListEnum::GetAmmo, 0.f);

		const bool bAlreadyGettingAmmo = F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetAmmo;
		float flScore = 0.f;
		for (int i = 0; i <= SLOT_PDA2; i++)
		{
			const int iActualSlot = G::SavedWepSlots[i];
			if ((iActualSlot != SLOT_PRIMARY && iActualSlot != SLOT_SECONDARY) || !G::AmmoInSlot[iActualSlot].m_bUsesAmmo)
				continue;

			const int iWeaponID = G::SavedWepIds[iActualSlot];
			const int iReserveAmmo = G::AmmoInSlot[iActualSlot].m_iReserve;
			if (iReserveAmmo <= (bAlreadyGettingAmmo ? 10 : 5) &&
				(iWeaponID == TF_WEAPON_SNIPERRIFLE ||
				iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC ||
				iWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP))
			{
				flScore = std::max(flScore, 760.f);
				continue;
			}

			const int iClip = G::AmmoInSlot[iActualSlot].m_iClip;
			const int iMaxClip = G::AmmoInSlot[iActualSlot].m_iMaxClip;
			const int iMaxReserveAmmo = G::AmmoInSlot[iActualSlot].m_iMaxReserve;
			if (!iMaxReserveAmmo)
				continue;

			const float flClipThreshold = bAlreadyGettingAmmo ? 0.35f : 0.25f;
			const float flReserveCriticalThreshold = bAlreadyGettingAmmo ? 0.35f : 0.25f;
			const float flReserveSearchThreshold = bAlreadyGettingAmmo ? 0.45f : (1.f / 3.f);

			if (iMaxClip > 0 &&
				iClip <= iMaxClip * flClipThreshold &&
				iReserveAmmo <= iMaxReserveAmmo * flReserveCriticalThreshold)
			{
				flScore = std::max(flScore, 700.f);
				continue;
			}

			if (iReserveAmmo <= iMaxReserveAmmo * flReserveSearchThreshold)
			{
				const float flReserveRatio = 1.f - static_cast<float>(iReserveAmmo) / iMaxReserveAmmo;
				flScore = std::max(flScore, 520.f + flReserveRatio * 180.f);
			}
		}

		return get_active_priority_score(PriorityListEnum::GetAmmo, flScore);
	}

	static auto object_needs_engineer_attention(CBaseObject* pBuilding) -> bool
	{
		if (!pBuilding || pBuilding->m_bPlacing())
			return false;

		if (pBuilding->m_iUpgradeLevel() != 3 || pBuilding->m_iHealth() <= pBuilding->m_iMaxHealth() / 1.25f)
			return true;

		if (pBuilding->GetClassID() == ETFClassID::CObjectSentrygun)
			return pBuilding->As<CObjectSentrygun>()->m_iAmmoShells() <= pBuilding->As<CObjectSentrygun>()->MaxAmmoShells() / 2;

		return false;
	}

	static auto get_engineer_score(CTFPlayer* pLocal) -> float
	{
		if (!pLocal || !F::NavBotEngineer.IsEngieMode(pLocal))
			return get_active_priority_score(PriorityListEnum::Engineer, 0.f);

		const bool bHasGunslinger = G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger;
		float flScore = 0.f;
		if (!F::NavBotEngineer.m_pMySentryGun || F::NavBotEngineer.m_pMySentryGun->m_bPlacing())
			flScore = 960.f;
		else if (bHasGunslinger)
			flScore = F::NavBotEngineer.m_flDistToSentry >= 1800.f ? 900.f : 0.f;
		else if (object_needs_engineer_attention(F::NavBotEngineer.m_pMySentryGun))
			flScore = 1050.f;
		else if (!F::NavBotEngineer.m_pMyDispenser || F::NavBotEngineer.m_pMyDispenser->m_bPlacing())
			flScore = 860.f;
		else if (object_needs_engineer_attention(F::NavBotEngineer.m_pMyDispenser))
			flScore = 980.f;

		return get_active_priority_score(PriorityListEnum::Engineer, flScore);
	}

	static auto get_run_reload_score(CTFPlayer* pLocal) -> float
	{
		if (!pLocal ||
			!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies) ||
			(!G::Reloading && !has_reload_target()))
			return get_active_priority_score(PriorityListEnum::RunReload, 0.f);

		float flScore = 640.f;
		if (F::BotUtils.m_tClosestEnemy.m_pPlayer)
		{
			if (F::BotUtils.m_tClosestEnemy.m_flDist < 250.f)
				flScore += 180.f;
			else if (F::BotUtils.m_tClosestEnemy.m_flDist < 500.f)
				flScore += 110.f;
			else
				flScore += 50.f;
		}

		return get_active_priority_score(PriorityListEnum::RunReload, flScore);
	}

	static auto get_safe_reload_score(CTFPlayer* pLocal) -> float
	{
		if (!pLocal ||
			!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) ||
			(F::NavBotReload.m_iLastReloadSlot == -1 && !G::Reloading))
			return get_active_priority_score(PriorityListEnum::RunSafeReload, 0.f);

		float flScore = 430.f;
		if (F::BotUtils.m_tClosestEnemy.m_pPlayer)
		{
			if (F::BotUtils.m_tClosestEnemy.m_flDist < 350.f)
				flScore += 140.f;
			else if (F::BotUtils.m_tClosestEnemy.m_flDist < 700.f)
				flScore += 70.f;
		}

		return get_active_priority_score(PriorityListEnum::RunSafeReload, flScore);
	}

	static auto get_melee_score(CTFPlayer* pLocal) -> float
	{
		if (!pLocal || F::BotUtils.m_iCurrentSlot != SLOT_MELEE || F::NavBotReload.m_iLastReloadSlot != -1)
			return get_active_priority_score(PriorityListEnum::MeleeAttack, 0.f);

		const auto& tClosestEnemy = F::BotUtils.m_tClosestEnemy;
		if (!tClosestEnemy.m_pPlayer || tClosestEnemy.m_flDist > Vars::Misc::Movement::NavBot::MeleeTargetRange.Value)
			return get_active_priority_score(PriorityListEnum::MeleeAttack, 0.f);

		float flScore = 700.f + (Vars::Misc::Movement::NavBot::MeleeTargetRange.Value - tClosestEnemy.m_flDist) * 0.6f;
		if (pLocal->m_iClass() == TF_CLASS_SPY)
			flScore += 80.f;

		return get_active_priority_score(PriorityListEnum::MeleeAttack, flScore);
	}

	static auto can_capture_objective() -> bool
	{
		if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::CaptureObjectives))
			return false;

		if (const auto& pGameRules = I::TFGameRules())
		{
			if (!((pGameRules->m_iRoundState() == GR_STATE_RND_RUNNING || pGameRules->m_iRoundState() == GR_STATE_STALEMATE) &&
				!pGameRules->m_bInWaitingForPlayers()) ||
				pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN ||
				(pGameRules->m_bPlayingSpecialDeliveryMode() && !F::GameObjectiveController.m_bDoomsday))
				return false;
		}

		return true;
	}

	static auto get_capture_score(CTFPlayer* pLocal) -> float
	{
		float flScore = can_capture_objective() ? 520.f : 0.f;
		if (!pLocal || !flScore)
			return get_active_priority_score(PriorityListEnum::Capture, flScore);

		switch (F::GameObjectiveController.m_eGameMode)
		{
		case TF_GAMETYPE_PASSTIME:
		{
			const int iCarrierIdx = F::PasstimeController.GetCarrier();
			if (pLocal->m_bHasPasstimeBall() || iCarrierIdx == pLocal->entindex())
				flScore = 1750.f;
			else if (iCarrierIdx > 0)
				flScore = 900.f;
			else if (F::PasstimeController.GetBall())
				flScore = 760.f;
			break;
		}
		case TF_GAMETYPE_CTF:
		{
			const int iOurTeam = pLocal->m_iTeamNum();
			const int iEnemyTeam = iOurTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
			if (F::FlagController.GetCarrier(iEnemyTeam) == pLocal->entindex())
				flScore = 1700.f;
			else if (F::FlagController.GetCarrier(iEnemyTeam) > 0)
				flScore = 880.f;
			break;
		}
		default:
			break;
		}

		return get_active_priority_score(PriorityListEnum::Capture, flScore);
	}

	static auto has_targetable_building(CTFPlayer* pLocal) -> bool
	{
		if (!pLocal)
			return false;

		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
		{
			if (!pEntity || pEntity->IsDormant())
				continue;

			if (F::BotUtils.ShouldTargetBuilding(pLocal, pEntity->entindex()) == ShouldTargetEnum::Target)
				return true;
		}

		return false;
	}

	static auto get_snipe_sentry_score(CTFPlayer* pLocal) -> float
	{
		if (!pLocal ||
			!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentries))
			return get_active_priority_score(PriorityListEnum::SnipeSentry, 0.f);

		const bool bShortRangeClass = pLocal->m_iClass() == TF_CLASS_SCOUT || pLocal->m_iClass() == TF_CLASS_PYRO;
		if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange) &&
			bShortRangeClass)
			return get_active_priority_score(PriorityListEnum::SnipeSentry, 0.f);

		float flScore = 0.f;
		if (F::NavBotSnipe.m_iTargetIdx > 0 &&
			F::BotUtils.ShouldTargetBuilding(pLocal, F::NavBotSnipe.m_iTargetIdx) == ShouldTargetEnum::Target)
			flScore = 540.f;
		else if (has_targetable_building(pLocal))
			flScore = 500.f;

		return get_active_priority_score(PriorityListEnum::SnipeSentry, flScore);
	}

	static auto get_stay_near_score(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> float
	{
		if (!pLocal || !pWeapon || !(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
			return get_active_priority_score(PriorityListEnum::StayNear, 0.f);

		float flScore = 0.f;
		if (F::NavBotStayNear.m_iStayNearTargetIdx > 0 &&
			F::BotUtils.ShouldTarget(pLocal, pWeapon, F::NavBotStayNear.m_iStayNearTargetIdx) == ShouldTargetEnum::Target)
			flScore = 380.f;
		else if (F::BotUtils.m_tClosestEnemy.m_pPlayer)
		{
			flScore = 340.f;
			const float flDist = F::BotUtils.m_tClosestEnemy.m_flDist;
			if (flDist < F::NavBotCore.m_tSelectedConfig.m_flMax)
				flScore += (F::NavBotCore.m_tSelectedConfig.m_flMax - flDist) * 0.08f;
			if (F::NavBotCore.m_tSelectedConfig.m_bPreferFar)
				flScore += 40.f;
		}

		return get_active_priority_score(PriorityListEnum::StayNear, flScore);
	}

	static auto get_group_with_others_score() -> float
	{
		if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::GroupWithOthers))
			return 0.f;

		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol ? 210.f : 180.f;
	}

	static auto get_roam_score() -> float
	{
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol ? 140.f : 110.f;
	}

void CNavBotJobSystem::RefreshSharedState(CTFPlayer* pLocal)
{
	if (!pLocal)
		return;

	F::NavBotGroup.UpdateLocalBotPositions(pLocal);
	F::NavBotEngineer.RefreshLocalBuildings(pLocal);
	F::NavBotEngineer.RefreshBuildingSpots(pLocal, F::BotUtils.m_tClosestEnemy);
}

auto CNavBotJobSystem::Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> NavBotJobResult_t
{
	NavBotJobResult_t tResult{};
	if (!pLocal || !pWeapon)
		return tResult;

	std::array aCandidates =
	{
		job_candidate_t{ job_kind_t::escape_spawn, get_escape_spawn_score(pLocal) },
		job_candidate_t{ job_kind_t::escape_projectiles, get_projectile_escape_score(pLocal) },
		job_candidate_t{ job_kind_t::escape_danger, get_escape_danger_score(pLocal) },
		job_candidate_t{ job_kind_t::get_health, get_health_score(pLocal, false) },
		job_candidate_t{ job_kind_t::engineer, get_engineer_score(pLocal) },
		job_candidate_t{ job_kind_t::run_reload, get_run_reload_score(pLocal) },
		job_candidate_t{ job_kind_t::melee, get_melee_score(pLocal) },
		job_candidate_t{ job_kind_t::get_ammo, get_ammo_score(pLocal) },
		job_candidate_t{ job_kind_t::capture, get_capture_score(pLocal) },
		job_candidate_t{ job_kind_t::snipe_sentry, get_snipe_sentry_score(pLocal) },
		job_candidate_t{ job_kind_t::safe_reload, get_safe_reload_score(pLocal) },
		job_candidate_t{ job_kind_t::stay_near, get_stay_near_score(pLocal, pWeapon) },
		job_candidate_t{ job_kind_t::low_prio_health, get_health_score(pLocal, true) },
		job_candidate_t{ job_kind_t::group_with_others, get_group_with_others_score() },
		job_candidate_t{ job_kind_t::roam, get_roam_score() }
	};

	while (auto pCandidate = FindBestCandidate(aCandidates))
	{
		bool bHasJob = false;
		switch (pCandidate->m_eKind)
		{
		case job_kind_t::escape_spawn:
			bHasJob = TryEscapeSpawn(pLocal);
			break;
		case job_kind_t::escape_projectiles:
			bHasJob = TryEscapeProjectiles(pLocal);
			break;
		case job_kind_t::escape_danger:
			bHasJob = TryEscapeDanger(pLocal);
			break;
		case job_kind_t::get_health:
			bHasJob = TryGetHealth(pCmd, pLocal, false);
			break;
		case job_kind_t::engineer:
			bHasJob = TryEngineer(pCmd, pLocal);
			break;
		case job_kind_t::run_reload:
			tResult.m_bRunReload = TryRunReload(pLocal, pWeapon);
			bHasJob = tResult.m_bRunReload;
			break;
		case job_kind_t::melee:
			bHasJob = TryMelee(pCmd, pLocal);
			break;
		case job_kind_t::get_ammo:
			bHasJob = TryGetAmmo(pCmd, pLocal);
			break;
		case job_kind_t::capture:
			bHasJob = TryCapture(pCmd, pLocal, pWeapon);
			break;
		case job_kind_t::snipe_sentry:
			bHasJob = TrySnipeSentry(pLocal);
			break;
		case job_kind_t::safe_reload:
			tResult.m_bRunSafeReload = TrySafeReload(pLocal, pWeapon);
			bHasJob = tResult.m_bRunSafeReload;
			break;
		case job_kind_t::stay_near:
			bHasJob = TryStayNear(pLocal, pWeapon);
			break;
		case job_kind_t::low_prio_health:
			bHasJob = TryGetHealth(pCmd, pLocal, true);
			break;
		case job_kind_t::group_with_others:
			bHasJob = TryGroupWithOthers(pLocal, pWeapon);
			break;
		case job_kind_t::roam:
			bHasJob = TryRoam(pLocal, pWeapon);
			break;
		}

		if (bHasJob)
		{
			tResult.m_bHasJob = true;
			return tResult;
		}

		pCandidate->m_flScore = 0.f;
	}

	return tResult;
}

void CNavBotJobSystem::Reset()
{
	F::NavBotStayNear.m_iStayNearTargetIdx = -1;
	F::NavBotReload.m_iLastReloadSlot = -1;
	F::NavBotSnipe.m_iTargetIdx = -1;
	F::NavBotSupplies.ResetTemp();
	F::NavBotEngineer.Reset();
	F::NavBotCapture.Reset();
	F::NavBotRoam.Reset();
	F::NavBotDanger.ResetSpawn();
	F::NavBotMVMSniper.Reset();
}

auto CNavBotJobSystem::TryEscapeSpawn(CTFPlayer* pLocal) -> bool
{
	return pLocal && F::NavBotDanger.EscapeSpawn(pLocal);
}

auto CNavBotJobSystem::TryEscapeProjectiles(CTFPlayer* pLocal) -> bool
{
	return pLocal && F::NavBotDanger.EscapeProjectiles(pLocal);
}

auto CNavBotJobSystem::TryEscapeDanger(CTFPlayer* pLocal) -> bool
{
	return pLocal && F::NavBotDanger.EscapeDanger(pLocal);
}

auto CNavBotJobSystem::TryGetHealth(CUserCmd* pCmd, CTFPlayer* pLocal, bool bLowPrio) -> bool
{
	if (!pCmd || !pLocal)
		return false;

	int iFlags = GetSupplyEnum::Health;
	if (bLowPrio)
		iFlags |= GetSupplyEnum::LowPrio;

	return F::NavBotSupplies.Run(pCmd, pLocal, iFlags);
}

auto CNavBotJobSystem::TryGetAmmo(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool
{
	return pCmd && pLocal && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo);
}

auto CNavBotJobSystem::TryEngineer(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool
{
	return pCmd && pLocal && F::NavBotEngineer.Run(pCmd, pLocal, F::BotUtils.m_tClosestEnemy);
}

auto CNavBotJobSystem::TryRunReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool
{
	return pLocal && pWeapon && F::NavBotReload.Run(pLocal, pWeapon);
}

auto CNavBotJobSystem::TrySafeReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool
{
	return pLocal && pWeapon && F::NavBotReload.RunSafe(pLocal, pWeapon);
}

auto CNavBotJobSystem::TryMelee(CUserCmd* pCmd, CTFPlayer* pLocal) -> bool
{
	return pCmd && pLocal && F::NavBotMelee.Run(pCmd, pLocal, F::BotUtils.m_iCurrentSlot, F::BotUtils.m_tClosestEnemy);
}

auto CNavBotJobSystem::TryCapture(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool
{
	return pCmd && pLocal && pWeapon && F::NavBotCapture.Run(pCmd, pLocal, pWeapon);
}

auto CNavBotJobSystem::TrySnipeSentry(CTFPlayer* pLocal) -> bool
{
	return pLocal && F::NavBotSnipe.Run(pLocal);
}

auto CNavBotJobSystem::TryStayNear(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool
{
	return pLocal && pWeapon && F::NavBotStayNear.Run(pLocal, pWeapon);
}

auto CNavBotJobSystem::TryGroupWithOthers(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool
{
	return pLocal && pWeapon && F::NavBotGroup.Run(pLocal, pWeapon);
}

auto CNavBotJobSystem::TryRoam(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> bool
{
	return pLocal && pWeapon && F::NavBotRoam.Run(pLocal, pWeapon);
}
