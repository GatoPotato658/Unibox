#include "NavBotJobs.h"
#include "../NavBotCore.h"
#include "../../Misc/NamedPipe/NamedPipe.h"
#include "../Hazards.h"
#include "../Objectives.h"

#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <unordered_set>

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

		if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger)
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
			const int iEnemyTeam = pLocal->m_iTeamNum() == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
			const int iFlagCarrierIdx = F::FlagController.GetCarrier(iEnemyTeam);
			if (iFlagCarrierIdx == pLocal->entindex())
				return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);
		}

		auto pLocalArea = F::NavEngine.GetLocalNavArea();
		if (!pLocalArea || is_spawn_area(pLocalArea))
			return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);

		const Hazard_t* pHazard = F::Hazards.GetHazard(pLocalArea);
		if (!pHazard)
			return get_active_priority_score(PriorityListEnum::EscapeDanger, 0.f);

		const float flHealth = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
		float flScore = 0.f;
		switch (pHazard->m_eKind)
		{
		case HazardKind::Sentry:
		case HazardKind::Sticky:
		case HazardKind::EnemyInvuln:
			flScore = 1700.f;
			break;
		case HazardKind::SentryMedium:
		case HazardKind::EnemyNormal:
			flScore = flHealth < 0.5f ? 1425.f : 0.f;
			break;
		case HazardKind::SentryLow:
		case HazardKind::EnemyDormant:
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

namespace NavJobUtils
{
	ClosestEnemy_t FindClosestTargetEnemy(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
	{
		if (!pLocal || !pWeapon)
			return {};

		ClosestEnemy_t tBestEnemy{};
		const Vector vLocalOrigin = pLocal->GetAbsOrigin();
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			const int iEntIndex = pEntity->entindex();
			if (F::BotUtils.ShouldTarget(pLocal, pWeapon, iEntIndex) != ShouldTargetEnum::Target)
				continue;

			const Vector vOrigin = pEntity->GetAbsOrigin();
			const float flDist = vLocalOrigin.DistTo(vOrigin);
			if (flDist >= tBestEnemy.m_flDist)
				continue;

			tBestEnemy.m_iEntIdx = iEntIndex;
			tBestEnemy.m_pPlayer = pEntity->As<CTFPlayer>();
			tBestEnemy.m_vOrigin = vOrigin;
			tBestEnemy.m_flDist = flDist;
		}

		return tBestEnemy;
	}

	bool TryNavToAreaScores(std::vector<NavAreaScore_t>& vAreaScores, PriorityListEnum::PriorityListEnum ePriority, bool bLowestScoreFirst, size_t nMaxAttempts)
	{
		if (vAreaScores.empty())
			return false;

		std::sort(vAreaScores.begin(), vAreaScores.end(), [bLowestScoreFirst](const NavAreaScore_t& a, const NavAreaScore_t& b)
			{
				return bLowestScoreFirst ? a.m_flScore < b.m_flScore : a.m_flScore > b.m_flScore;
			});

		size_t nAttempts = 0;
		for (const auto& tAreaScore : vAreaScores)
		{
			if (!tAreaScore.m_pArea)
				continue;

			if (nMaxAttempts && nAttempts++ >= nMaxAttempts)
				break;

			if (F::NavEngine.NavTo(tAreaScore.m_pArea->m_vCenter, ePriority))
				return true;
		}

		return false;
	}
}

static bool ApproachMeleeTarget(CUserCmd* pCmd, CTFPlayer* pLocal, const Vector& vTargetOrigin)
{

	auto pGroundEntity = pLocal->m_hGroundEntity().Get();
	if (pGroundEntity && pGroundEntity->IsPlayer())
		pCmd->buttons |= IN_DUCK;

	SDK::WalkTo(pCmd, pLocal, vTargetOrigin);
	F::NavEngine.CancelPath();
	F::NavEngine.m_eCurrentPriority = PriorityListEnum::MeleeAttack;
	return true;
}

static Vector GetSpyBackstabSpot(CTFPlayer* pLocal, CTFPlayer* pPlayer)
{
	Vec3 vForward;
	Math::AngleVectors(pPlayer->GetEyeAngles(), &vForward);
	vForward.z = 0.f;
	if (vForward.Normalize() <= 0.01f)
		return pPlayer->GetAbsOrigin();

	Vector vSide(-vForward.y, vForward.x, 0.f);
	const float flSideSign = (pLocal->entindex() + pPlayer->entindex()) % 2 ? 1.f : -1.f;
	Vector vBackstabSpot = pPlayer->GetAbsOrigin() - vForward * 68.f + vSide * (flSideSign * 28.f);

	CNavArea* pBackstabArea = F::NavEngine.FindClosestNavArea(vBackstabSpot);
	if (!pBackstabArea || pBackstabArea->IsBlocked(pLocal->m_iTeamNum()))
		vBackstabSpot = pPlayer->GetAbsOrigin() - vForward * 54.f;

	return vBackstabSpot;
}

bool CNavBotMelee::Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, ClosestEnemy_t tClosestEnemy)
{
	if (iSlot != SLOT_MELEE || F::NavBotReload.m_iLastReloadSlot != -1)
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack)
			F::NavEngine.CancelPath();
		return false;
	}

	auto pEntity = I::ClientEntityList->GetClientEntity(tClosestEnemy.m_iEntIdx);
	if (!pEntity || pEntity->IsDormant())
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch)
		return false;

	if (tClosestEnemy.m_flDist > Vars::Misc::Movement::NavBot::MeleeTargetRange.Value)
		return false;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::MeleeAttack)
		return false;

	static bool bIsVisible = false;
	static Timer tVischeckCooldown{};
	if (tVischeckCooldown.Run(0.2f))
	{
		CGameTrace trace;
		CTraceFilterHitscan filter(pLocal);
		SDK::TraceHull(pLocal->GetShootPos(), pPlayer->GetAbsOrigin(), pLocal->m_vecMins() * 0.3f, pLocal->m_vecMaxs() * 0.3f, MASK_PLAYERSOLID, &filter, &trace);
		bIsVisible = trace.DidHit() ? trace.m_pEnt && trace.m_pEnt == pPlayer : true;
	}

	Vector vTargetOrigin = pPlayer->GetAbsOrigin();
	Vector vLocalOrigin = pLocal->GetAbsOrigin();

	if (pLocal->m_iClass() == TF_CLASS_SPY)
	{
		auto pKnife = pLocal->GetWeaponFromSlot(SLOT_MELEE);
		const bool bReadyToBackstab = pKnife && pKnife->GetWeaponID() == TF_WEAPON_KNIFE && pKnife->As<CTFKnife>()->m_bReadyToBackstab();
		const bool bKnifeLethal = pKnife && pPlayer->m_iHealth() <= pKnife->GetDamage(false);
		const bool bCanSwing = bReadyToBackstab || bKnifeLethal;
		if (!bCanSwing)
			pCmd->buttons &= ~IN_ATTACK;

		Vector vBackstabSpot = GetSpyBackstabSpot(pLocal, pPlayer);
		if (vLocalOrigin.DistTo(vBackstabSpot) < 100.0f && bIsVisible)
			return ApproachMeleeTarget(pCmd, pLocal, bCanSwing ? vTargetOrigin : vBackstabSpot);

		static Timer tSpyMeleeCooldown{};
		float flDistToSpot = vLocalOrigin.DistTo(vBackstabSpot);
		if (!tSpyMeleeCooldown.Run(flDistToSpot < 200.f ? 0.1f : flDistToSpot < 1000.f ? 0.3f : 1.f) && F::NavEngine.IsPathing())
			return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

		if (F::NavEngine.NavTo(vBackstabSpot, PriorityListEnum::MeleeAttack))
			return true;

		return false;
	}

	if (tClosestEnemy.m_flDist < 100.0f && bIsVisible)
		return ApproachMeleeTarget(pCmd, pLocal, vTargetOrigin);

	static Timer tMeleeCooldown{};
	if (!tMeleeCooldown.Run(tClosestEnemy.m_flDist < 100.f ? 0.2f : tClosestEnemy.m_flDist < 1000.f ? 0.5f : 2.f) && F::NavEngine.IsPathing())
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

	if (F::NavEngine.NavTo(vTargetOrigin, PriorityListEnum::MeleeAttack))
		return true;
	return false;
}

inline bool HasReloadTask(int iReloadSlot)
{
	return G::Reloading || (iReloadSlot >= SLOT_PRIMARY && iReloadSlot <= SLOT_SECONDARY);
}

static bool TryNavToHiddenSpot(CNavArea* pLocalArea, const Vector& vVischeckPoint, PriorityListEnum::PriorityListEnum ePriority)
{
	if (!pLocalArea)
		return false;

	std::pair<CNavArea*, int> tBestSpot;
	if (!NavAreaUtils::FindClosestHidingSpot(pLocalArea, vVischeckPoint, 5, tBestSpot))
		return false;

	return F::NavEngine.NavTo(tBestSpot.first->m_vCenter, ePriority);
}

bool CNavBotReload::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tReloadrunCooldown{};

	if (!HasReloadTask(m_iLastReloadSlot))
		return false;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
		return false;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::RunReload)
		return false;

	if (!tReloadrunCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunReload;

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunReload && F::NavEngine.IsPathing())
		return true;

	const auto tClosestEnemy = NavJobUtils::FindClosestTargetEnemy(pLocal, pWeapon);
	if (!tClosestEnemy.m_pPlayer)
		return false;

	Vector vVischeckPoint = tClosestEnemy.m_vOrigin;
	vVischeckPoint.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	return TryNavToHiddenSpot(F::NavEngine.GetLocalNavArea(), vVischeckPoint, PriorityListEnum::RunReload);
}

bool CNavBotReload::RunSafe(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tReloadrunCooldown{};
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || !HasReloadTask(m_iLastReloadSlot))
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload)
			F::NavEngine.CancelPath();
		return false;
	}

	if (!tReloadrunCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload;

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload && F::NavEngine.IsPathing())
		return true;

	Vector vCurrentDestination;
	auto pCrumbs = F::NavEngine.GetCrumbs();
	bool bHasDestination = F::NavEngine.m_eCurrentPriority != PriorityListEnum::RunSafeReload && pCrumbs->size() > 4;
	if (bHasDestination)
		vCurrentDestination = pCrumbs->at(4).m_vPos;

	if (bHasDestination)
		vCurrentDestination.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	else
	{

		const auto tClosestEnemy = NavJobUtils::FindClosestTargetEnemy(pLocal, pWeapon);
		if (tClosestEnemy.m_pPlayer)
		{
			bHasDestination = true;
			vCurrentDestination = tClosestEnemy.m_vOrigin;
			vCurrentDestination.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		}
	}

	if (bHasDestination)
		return TryNavToHiddenSpot(F::NavEngine.GetLocalNavArea(), vCurrentDestination, PriorityListEnum::RunSafeReload);

	return false;
}

int CNavBotReload::GetReloadWeaponSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons))
		return -1;

	if (G::Reloading && F::BotUtils.m_iCurrentSlot >= SLOT_PRIMARY && F::BotUtils.m_iCurrentSlot <= SLOT_SECONDARY)
		return F::BotUtils.m_iCurrentSlot;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Capture)
		return -1;

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear && tClosestEnemy.m_flDist <= 500.f
		|| tClosestEnemy.m_flDist <= 250.f)
		return -1;

	auto pPrimaryWeapon = pLocal->GetWeaponFromSlot(SLOT_PRIMARY);
	auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
	bool bCheckPrimary = !SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[SLOT_PRIMARY], G::SavedDefIndexes[SLOT_PRIMARY], false);
	bool bCheckSecondary = !SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY], false);

	float flDivider = F::NavEngine.m_eCurrentPriority < PriorityListEnum::StayNear && tClosestEnemy.m_flDist > 500.f ? 1.f : 3.f;

	CTFWeaponInfo* pWeaponInfo = nullptr;
	bool bWeaponCantReload = false;
	if (bCheckPrimary && pPrimaryWeapon)
	{
		pWeaponInfo = pPrimaryWeapon->m_pWeaponInfo();
		bWeaponCantReload = (!pWeaponInfo || pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pPrimaryWeapon->m_iPrimaryAmmoType())) && G::SavedWepIds[SLOT_PRIMARY] != TF_WEAPON_PARTICLE_CANNON && G::SavedWepIds[SLOT_PRIMARY] != TF_WEAPON_DRG_POMSON;
		if (pWeaponInfo && !bWeaponCantReload && G::AmmoInSlot[SLOT_PRIMARY].m_iClip < (pWeaponInfo->iMaxClip1 / flDivider))
			return SLOT_PRIMARY;
	}

	bool bFoundPrimaryWepInfo = pWeaponInfo;
	if (bCheckSecondary && pSecondaryWeapon && (bFoundPrimaryWepInfo || !bCheckPrimary))
	{
		pWeaponInfo = pSecondaryWeapon->m_pWeaponInfo();
		bWeaponCantReload = (!pWeaponInfo || pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pSecondaryWeapon->m_iPrimaryAmmoType())) && G::SavedWepIds[SLOT_SECONDARY] != TF_WEAPON_RAYGUN;
		if (pWeaponInfo && !bWeaponCantReload && G::AmmoInSlot[SLOT_SECONDARY].m_iClip < (pWeaponInfo->iMaxClip1 / flDivider))
			return SLOT_SECONDARY;
	}

	return -1;
}

inline bool IsShortRangeSentryClass(CTFPlayer* pLocal)
{
	return pLocal->m_iClass() == TF_CLASS_SCOUT || pLocal->m_iClass() == TF_CLASS_PYRO;
}

bool CNavBotSnipe::IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bShortRangeClass, bool bFixSentryZ)
{
	if (bFixSentryZ)
		vEntOrigin.z += 40.0f;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	float flMinDist = (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange && bShortRangeClass) ? 0.f : 1100.f + HALF_PLAYER_WIDTH;
	if (vEntOrigin.DistTo(vAreaOrigin) <= flMinDist)
		return false;

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

	bool bShortRangeClass = IsShortRangeSentryClass(pLocal);
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange) &&
		bShortRangeClass)
	{
		m_iTargetIdx = -1;
		return false;
	}

	if (!tSentrySnipeCooldown.Run(2.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	int iPreviousTargetValid = F::BotUtils.ShouldTargetBuilding(pLocal, m_iTargetIdx);
	if (iPreviousTargetValid == ShouldTargetEnum::Target)
	{
		tInvalidTargetTimer.Update();

		Vector vOrigin;
		if (F::BotUtils.GetDormantOrigin(m_iTargetIdx, &vOrigin))
		{

			if (F::NavEngine.m_tLastCrumb.m_pNavArea)
			{

				if (IsAreaValidForSnipe(vOrigin, F::NavEngine.m_tLastCrumb.m_pNavArea->m_vCenter, bShortRangeClass))
					return true;
			}
			if (TryToSnipe(m_iTargetIdx, bShortRangeClass))
				return true;
		}
	}

	else if (iPreviousTargetValid == ShouldTargetEnum::Invalid && !tInvalidTargetTimer.Check(0.1f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	tInvalidTargetTimer.Update();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (pEntity->IsDormant())
			continue;

		int iEntIdx = pEntity->entindex();

		if (F::BotUtils.ShouldTargetBuilding(pLocal, iEntIdx) != ShouldTargetEnum::Target)
			continue;

		if (TryToSnipe(iEntIdx, bShortRangeClass))
		{
			m_iTargetIdx = iEntIdx;
			return true;
		}
	}

	m_iTargetIdx = -1;
	return false;
}

static Vector NormalizePlanar(Vector vDirection)
{
	vDirection.z = 0.f;
	const float flLength = vDirection.Length();
	if (flLength <= 0.001f)
		return {};

	return vDirection / flLength;
}

bool CNavBotGroup::GetFormationOffset(CTFPlayer* pLocal, int iPositionIndex, Vector& vOut)
{
	if (iPositionIndex <= 0)
		return false;

	Vector vLeaderVelocity(0, 0, 0);

	if (!m_vLocalBotPositions.empty())
		vLeaderVelocity = m_vLocalBotPositions[0].second;
	else
	{

		vLeaderVelocity = pLocal->m_vecVelocity();
	}

	Vector vDirection = vLeaderVelocity;
	if (vDirection.Length() < 10.0f)
	{
		QAngle viewAngles;
		I::EngineClient->GetViewAngles(viewAngles);
		Math::AngleVectors(viewAngles, &vDirection);
	}

	vDirection = NormalizePlanar(vDirection);

	vOut = (vDirection * -m_flFormationDistance * iPositionIndex);
	return true;
}

void CNavBotGroup::UpdateLocalBotPositions(CTFPlayer* pLocal)
{
	if (!m_tUpdateFormationTimer.Run(0.5f))
		return;

	m_vLocalBotPositions.clear();

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	int iLocalIdx = pLocal->entindex();
	uint32 uLocalUserID = pResource->m_iUserID(iLocalIdx);
	int iLocalTeam = pLocal->m_iTeamNum();

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
		if (!pEntity || pEntity->IsDormant() ||
			!pEntity->IsPlayer() || pEntity->m_iTeamNum() != iLocalTeam)
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive())
			continue;

		m_vLocalBotPositions.push_back({ pResource->m_iUserID(i), pPlayer->m_vecVelocity() });
	}

	std::sort(m_vLocalBotPositions.begin(), m_vLocalBotPositions.end(),
		[](const std::pair<uint32_t, Vector>& a, const std::pair<uint32_t, Vector>& b)
		{
			return a.first < b.first;
		});

	m_iPositionInFormation = -1;

	std::vector<uint32_t> vAllBotsInOrder;
	vAllBotsInOrder.push_back(uLocalUserID);

	for (const auto& bot : m_vLocalBotPositions)
		vAllBotsInOrder.push_back(bot.first);

	std::sort(vAllBotsInOrder.begin(), vAllBotsInOrder.end());

	for (size_t i = 0; i < vAllBotsInOrder.size(); i++)
	{
		if (vAllBotsInOrder[i] == uLocalUserID)
		{
			m_iPositionInFormation = static_cast<int>(i);
			break;
		}
	}
}

bool CNavBotGroup::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::GroupWithOthers))
		return false;

	static int iConsecutiveFailures = 0;
	static Vector vLastTargetPos;

	if (m_iPositionInFormation < 0 || m_vLocalBotPositions.empty())
		return false;

	if (m_iPositionInFormation == 0)
		return false;

	Vector vOffset;
	if (!GetFormationOffset(pLocal, m_iPositionInFormation, vOffset))
		return false;

	Vector vLeaderPos;
	CTFPlayer* pLeaderPlayer = nullptr;

	if (!m_vLocalBotPositions.empty())
	{

		auto pClientEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(m_vLocalBotPositions[0].first));
		auto pLeader = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
		if (pLeader && pLeader->IsPlayer())
		{
			pLeaderPlayer = pLeader->As<CTFPlayer>();
			vLeaderPos = pLeaderPlayer->GetAbsOrigin();
		}
	}
	if (!pLeaderPlayer)
		return false;

	if (pLeaderPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
		return false;

	Vector vTargetPos = vLeaderPos + vOffset;

	float flDistToTarget = pLocal->GetAbsOrigin().DistTo(vTargetPos);
	if (flDistToTarget <= 30.f)
	{

		if (F::NavEngine.IsPathing() && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol && vLastTargetPos.DistTo(vTargetPos) <= 50.f)
			F::NavEngine.CancelPath();

		iConsecutiveFailures = 0;
		vLastTargetPos = vTargetPos;
		return false;
	}

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Patrol)
		return false;

	if (vLastTargetPos.DistTo(vTargetPos) <= 50.f && !F::NavEngine.IsPathing())
	{
		iConsecutiveFailures++;

		if (iConsecutiveFailures >= 3)
		{
			iConsecutiveFailures = 0;

			m_flFormationDistance += 50.0f;
			if (m_flFormationDistance > 300.0f)
				m_flFormationDistance = 120.0f;

			return true;
		}
	}
	else if (vLastTargetPos.DistTo(vTargetPos) > 50.f)
		iConsecutiveFailures = 0;
	vLastTargetPos = vTargetPos;

	if (!m_tFormationNavTimer.Run(0.5f) && F::NavEngine.IsPathing())
		return true;

	if (F::NavEngine.NavTo(vTargetPos, PriorityListEnum::Patrol))
		return true;

	return false;
}

namespace NavAreaUtils
{
	bool FindClosestHidingSpot(
		CNavArea* pArea,
		const Vector& vVischeckPoint,
		int iMaxDepth,
		std::pair<CNavArea*, int>& tOut,
		bool bVischeck,
		int iStartDepth)
	{
		tOut = {};
		if (!pArea || iMaxDepth <= iStartDepth)
			return false;
		if (!bVischeck)
		{
			tOut = { pArea, iStartDepth };
			return true;
		}

		std::queue<std::pair<CNavArea*, int>> vAreas;
		std::unordered_set<CNavArea*> vVisited;
		vAreas.push({ pArea, iStartDepth });
		vVisited.insert(pArea);

		while (!vAreas.empty())
		{
			auto [pCurrentArea, iDepth] = vAreas.front();
			vAreas.pop();

			Vector vAreaOrigin = pCurrentArea->m_vCenter;
			vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
			if (!F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vVischeckPoint))
			{
				tOut = { pCurrentArea, iDepth };
				return true;
			}

			if (iDepth + 1 >= iMaxDepth)
				continue;
			for (const auto& tConnection : pCurrentArea->m_vConnections)
				if (tConnection.m_pArea && vVisited.insert(tConnection.m_pArea).second)
					vAreas.push({ tConnection.m_pArea, iDepth + 1 });
		}
		return false;
	}
}
