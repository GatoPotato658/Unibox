#include "NavBotJobs.h"
#include "../NavBotCore.h"
#include "../Hazards.h"
#include "../Objectives.h"

#include <algorithm>
#include <array>

using EPriority = PriorityListEnum::PriorityListEnum;

enum class EJobKind
{
	EscapeSpawn,
	EscapeProjectiles,
	EscapeDanger,
	GetHealth,
	Engineer,
	RunReload,
	Melee,
	GetAmmo,
	Capture,
	SnipeSentry,
	SafeReload,
	StayNear,
	LowPrioHealth,
	GroupWithOthers,
	Roam
};

struct JobCandidate_t
{
	EJobKind m_eKind = {};
	float m_flScore = 0.f;
};

constexpr float kActiveJobBonus = 140.f;
constexpr float kActiveJobCleanupScore = 320.f;

template <size_t nCount>
static auto FindBestCandidate(std::array<JobCandidate_t, nCount>& aCandidates) -> JobCandidate_t*
{
	JobCandidate_t* pBestCandidate = nullptr;
	for (auto& tCandidate : aCandidates)
	{
		if (tCandidate.m_flScore <= 0.f)
			continue;

		if (!pBestCandidate || tCandidate.m_flScore > pBestCandidate->m_flScore)
			pBestCandidate = &tCandidate;
	}

	return pBestCandidate;
}

static auto GetActivePriorityScore(EPriority ePriority, float flScore) -> float
{
	if (F::NavEngine.m_eCurrentPriority != ePriority)
		return flScore;

	return flScore > 0.f ? flScore + kActiveJobBonus : kActiveJobCleanupScore;
}

static auto GetProjectileEscapeScore(CTFPlayer* pLocal) -> float
{
	if (!pLocal || F::VSHController.IsBossLocal() || !NavJobUtils::HasBlacklist(Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies | Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
		return GetActivePriorityScore(PriorityListEnum::EscapeDanger, 0.f);

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger)
		return 0.f;

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	float flClosestThreat = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		float flThreatRadius = 0.f;
		if (!CNavBotDanger::GetProjectileThreatRange(pEntity, pLocal->m_iTeamNum(), flThreatRadius))
			continue;

		const float flDist = pEntity->m_vecOrigin().DistTo(vLocalOrigin);
		if (flDist < flThreatRadius)
			flClosestThreat = std::min(flClosestThreat, flDist);
	}

	float flScore = 0.f;
	if (flClosestThreat < FLT_MAX)
		flScore = 1800.f + (400.f - std::min(flClosestThreat, 400.f)) * 0.5f;

	return GetActivePriorityScore(PriorityListEnum::EscapeDanger, flScore);
}

auto CNavBotJobSystem::GetEscapeDangerScore(CTFPlayer* pLocal) -> float
{
	const auto Decline = [this]() -> float
		{
			m_bDangerLatch = false;
			return GetActivePriorityScore(PriorityListEnum::EscapeDanger, 0.f);
		};

	if (!pLocal || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::EscapeDanger))
		return Decline();

	if (NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::DontEscapeDangerIntel) &&
		F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_CTF)
	{
		const int iEnemyTeam = pLocal->m_iTeamNum() == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
		if (F::FlagController.GetCarrier(iEnemyTeam) == pLocal->entindex())
			return Decline();
	}

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack
		|| F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload)
		return Decline();

	auto pLocalArea = F::NavEngine.GetLocalNavArea();
	if (!pLocalArea || NavJobUtils::IsSpawnArea(pLocalArea))
		return Decline();

	const Hazard_t* pHazard = F::Hazards.GetHazard(pLocalArea);
	bool bAheadLow = false;
	if (!pHazard)
	{
		const bool bRespectAhead = F::NavEngine.m_eCurrentPriority != PriorityListEnum::Capture
			|| NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::SafeCapping);
		if (bRespectAhead)
		{
			if (const Hazard_t* pAhead = F::NavBotDanger.GetHazardAhead(pLocal))
			{
				if (pAhead->m_eKind != HazardKind::None && pAhead->m_eKind != HazardKind::EnemyDormant)
				{
					pHazard = pAhead;
					bAheadLow = pAhead->m_eKind == HazardKind::SentryLow;
				}
			}
		}
	}
	if (!pHazard)
	{
		if (!m_bDangerLatch || m_tDangerCommit.Check(1.f))
			m_bDangerLatch = false;
		return GetActivePriorityScore(PriorityListEnum::EscapeDanger, 0.f);
	}

	const float flHealth = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
	const int iPersonality = Vars::Misc::Movement::NavBot::Personality.Value;
	const bool bVSHBoss = F::VSHController.IsBossLocal();
	const float flMedHpThreshold = bVSHBoss ? 0.f : iPersonality == Vars::Misc::Movement::NavBot::PersonalityEnum::Yolo ? 0.25f
		: iPersonality == Vars::Misc::Movement::NavBot::PersonalityEnum::Cautious ? 0.8f : 0.5f;
	const float flHighHpThreshold = bVSHBoss ? 0.3f : iPersonality == Vars::Misc::Movement::NavBot::PersonalityEnum::Yolo ? 0.35f : 1.01f;
	float flScore = 0.f;
	switch (pHazard->m_eKind)
	{
	case HazardKind::Sentry:
	case HazardKind::Sticky:
	case HazardKind::EnemyInvuln:
		flScore = flHealth < flHighHpThreshold ? 1700.f : 0.f;
		break;
	case HazardKind::SentryMedium:
	case HazardKind::EnemyNormal:
		flScore = flHealth < flMedHpThreshold ? 1425.f : 0.f;
		break;
	case HazardKind::SentryLow:
		flScore = bAheadLow && flHealth < 0.5f ? 1425.f : 0.f;
		break;
	case HazardKind::Boss:
		flScore = 1700.f;
		break;
	default:
		break;
	}

	if (flScore > 0.f)
	{
		m_bDangerLatch = true;
		m_tDangerCommit.Update();
	}
	else if (m_bDangerLatch && F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger && !m_tDangerCommit.Check(1.f))
		flScore = 950.f;
	else
		m_bDangerLatch = false;

	if (flScore <= 0.f && F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger)
		flScore = 300.f;

	return GetActivePriorityScore(PriorityListEnum::EscapeDanger, flScore);
}

static auto GetEscapeSpawnScore(CTFPlayer* pLocal) -> float
{
	if (!Vars::Misc::Movement::NavBot::EscapeSpawn.Value)
		return 0.f;

	if (!pLocal || !NavJobUtils::IsSpawnArea(F::NavEngine.GetLocalNavArea()))
		return GetActivePriorityScore(PriorityListEnum::EscapeSpawn, 0.f);

	return GetActivePriorityScore(PriorityListEnum::EscapeSpawn, 2000.f);
}

static auto GetHealthScore(CTFPlayer* pLocal, bool bLowPrio) -> float
{
	const auto ePriority = bLowPrio ? PriorityListEnum::LowPrioGetHealth : PriorityListEnum::GetHealth;
	if (!pLocal || F::VSHController.IsBossLocal() || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::SearchHealth))
		return GetActivePriorityScore(ePriority, 0.f);

	const float flHealth = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
	const bool bActive = F::NavEngine.m_eCurrentPriority == ePriority;
	const bool bHealing = NavJobUtils::IsBeingHealed(pLocal);
	float flScore = 0.f;

	if (bLowPrio)
	{
		if (bActive)
			flScore = flHealth < NavJobTuning::HEALTH_RESUME_LOW_PRIO ? 340.f + (NavJobTuning::HEALTH_RESUME_LOW_PRIO - flHealth) * 400.f : 0.f;
		else if (!bHealing && F::NavEngine.m_eCurrentPriority <= PriorityListEnum::Patrol && flHealth <= NavJobTuning::HEALTH_START_LOW_PRIO)
			flScore = 260.f + (NavJobTuning::HEALTH_START_LOW_PRIO - flHealth) * 350.f;
	}
	else
	{
		if (bActive)
			flScore = flHealth < NavJobTuning::HEALTH_RESUME ? 900.f + (NavJobTuning::HEALTH_RESUME - flHealth) * 500.f : 0.f;
		else if (!bHealing)
		{
			if (flHealth < 0.25f)
				flScore = 1500.f;
			else if (flHealth < 0.40f)
				flScore = 1325.f;
			else if (flHealth < NavJobTuning::HEALTH_START)
				flScore = 1100.f;
		}

		if (flScore > 0.f && flHealth >= 0.5f && !bActive && F::MissionBoard.GetUrgency() >= 0.85f)
			flScore *= 0.5f;
	}

	return GetActivePriorityScore(ePriority, flScore);
}

static auto GetAmmoScore(CTFPlayer* pLocal) -> float
{
	if (!pLocal || F::VSHController.IsBossLocal() || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::SearchAmmo))
		return GetActivePriorityScore(PriorityListEnum::GetAmmo, 0.f);

	return GetActivePriorityScore(PriorityListEnum::GetAmmo, F::NavBotSupplies.GetAmmoNeed(F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetAmmo));
}

static auto GetEngineerScore(CTFPlayer* pLocal) -> float
{
	if (!pLocal || !F::NavBotEngineer.IsEngieMode(pLocal))
		return GetActivePriorityScore(PriorityListEnum::Engineer, 0.f);

	const auto pSentry = F::NavBotEngineer.m_pMySentryGun;
	const auto pDispenser = F::NavBotEngineer.m_pMyDispenser;
	float flScore = 0.f;
	if (!pSentry || pSentry->m_bPlacing())
		flScore = 960.f;
	else if (G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger)
		flScore = F::NavBotEngineer.m_flDistToSentry >= 1800.f ? 900.f : 0.f;
	else if (F::NavBotEngineer.BuildingNeedsToBeSmacked(pSentry))
		flScore = 1050.f;
	else if (!pDispenser || pDispenser->m_bPlacing())
		flScore = 860.f;
	else if (F::NavBotEngineer.BuildingNeedsToBeSmacked(pDispenser))
		flScore = 980.f;

	return GetActivePriorityScore(PriorityListEnum::Engineer, flScore);
}

static auto GetRunReloadScore(CTFPlayer* pLocal) -> float
{
	if (!pLocal || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies) || !F::NavBotReload.HasTask())
		return GetActivePriorityScore(PriorityListEnum::RunReload, 0.f);

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

	return GetActivePriorityScore(PriorityListEnum::RunReload, flScore);
}

static auto GetSafeReloadScore(CTFPlayer* pLocal) -> float
{
	if (!pLocal || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || !F::NavBotReload.HasTask())
		return GetActivePriorityScore(PriorityListEnum::RunSafeReload, 0.f);

	float flScore = 430.f;
	if (F::BotUtils.m_tClosestEnemy.m_pPlayer)
	{
		if (F::BotUtils.m_tClosestEnemy.m_flDist < 350.f)
			flScore += 140.f;
		else if (F::BotUtils.m_tClosestEnemy.m_flDist < 700.f)
			flScore += 70.f;
	}

	return GetActivePriorityScore(PriorityListEnum::RunSafeReload, flScore);
}

static auto GetMeleeScore(CTFPlayer* pLocal) -> float
{
	if (!pLocal || F::BotUtils.m_iCurrentSlot != SLOT_MELEE || F::NavBotReload.m_iLastReloadSlot != -1)
		return GetActivePriorityScore(PriorityListEnum::MeleeAttack, 0.f);

	const auto& tClosestEnemy = F::BotUtils.m_tClosestEnemy;
	const bool bVSHBoss = F::VSHController.IsBossLocal();
	const float flMeleeRange = bVSHBoss ? FLT_MAX : Vars::Misc::Movement::NavBot::MeleeTargetRange.Value;
	if (!tClosestEnemy.m_pPlayer || tClosestEnemy.m_flDist > flMeleeRange)
		return GetActivePriorityScore(PriorityListEnum::MeleeAttack, 0.f);

	if (bVSHBoss)
		return GetActivePriorityScore(PriorityListEnum::MeleeAttack, 900.f);

	float flScore = 700.f + (flMeleeRange - tClosestEnemy.m_flDist) * 0.6f;
	if (pLocal->m_iClass() == TF_CLASS_SPY)
		flScore += 80.f;

	return GetActivePriorityScore(PriorityListEnum::MeleeAttack, flScore);
}

static auto GetCaptureScore(CTFPlayer* pLocal) -> float
{
	if (!pLocal || !CNavBotCapture::CanCaptureObjective())
		return GetActivePriorityScore(PriorityListEnum::Capture, 0.f);

	const auto& tMission = F::MissionBoard.GetMission();
	const float flScore = tMission.m_bValid && tMission.m_flScore > 0.f ? tMission.m_flScore : 520.f;
	return GetActivePriorityScore(PriorityListEnum::Capture, flScore + F::MissionBoard.GetUrgency() * 150.f);
}

static auto HasTargetableBuilding(CTFPlayer* pLocal) -> bool
{
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (!pEntity || pEntity->IsDormant())
			continue;

		if (F::BotUtils.ShouldTargetBuilding(pLocal, pEntity->entindex()) == ShouldTargetEnum::Target)
			return true;
	}

	return false;
}

static auto GetSnipeSentryScore(CTFPlayer* pLocal) -> float
{
	if (!pLocal || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentries) ||
		(NavJobUtils::IsShortRangeClass(pLocal) && !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange)))
		return GetActivePriorityScore(PriorityListEnum::SnipeSentry, 0.f);

	float flScore = 0.f;
	if (F::NavBotSnipe.m_iTargetIdx > 0 &&
		F::BotUtils.ShouldTargetBuilding(pLocal, F::NavBotSnipe.m_iTargetIdx) == ShouldTargetEnum::Target)
		flScore = 540.f;
	else if (HasTargetableBuilding(pLocal))
		flScore = 500.f;

	return GetActivePriorityScore(PriorityListEnum::SnipeSentry, flScore);
}

static auto GetStayNearScore(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> float
{
	if (!pLocal || !pWeapon || !NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
		return GetActivePriorityScore(PriorityListEnum::StayNear, 0.f);

	const auto& tConfig = F::NavBotCore.m_tSelectedConfig;
	float flScore = 0.f;
	if (F::NavBotStayNear.m_iStayNearTargetIdx > 0 &&
		F::BotUtils.ShouldTarget(pLocal, pWeapon, F::NavBotStayNear.m_iStayNearTargetIdx) == ShouldTargetEnum::Target)
		flScore = 380.f;
	else if (F::BotUtils.m_tClosestEnemy.m_pPlayer)
	{
		flScore = 340.f;
		const float flDist = F::BotUtils.m_tClosestEnemy.m_flDist;
		if (flDist < tConfig.m_flMax)
			flScore += (tConfig.m_flMax - flDist) * 0.08f;
		if (tConfig.m_bPreferFar)
			flScore += 40.f;
	}

	if (flScore > 0.f)
	{
		const int iPersonality = Vars::Misc::Movement::NavBot::Personality.Value;
		if (iPersonality == Vars::Misc::Movement::NavBot::PersonalityEnum::Yolo)
			flScore += 120.f;
		else if (iPersonality == Vars::Misc::Movement::NavBot::PersonalityEnum::Cautious)
			flScore = std::max(flScore - 100.f, 0.f);
		flScore += SDK::RandomFloat(-15.f, 15.f);
	}

	return GetActivePriorityScore(PriorityListEnum::StayNear, flScore);
}

static auto GetGroupWithOthersScore() -> float
{
	if (!NavJobUtils::HasPreference(Vars::Misc::Movement::NavBot::PreferencesEnum::GroupWithOthers))
		return 0.f;

	return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol ? 210.f : 180.f;
}

static auto GetRoamScore() -> float
{
	return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol ? 140.f : 110.f;
}

void CNavBotJobSystem::RefreshSharedState(CTFPlayer* pLocal)
{
	if (!pLocal)
		return;

	F::NavBotGroup.UpdateLocalBots(pLocal);
	F::MissionBoard.Update(pLocal);
	F::NavBotEngineer.RefreshLocalBuildings(pLocal);
	F::NavBotEngineer.RefreshBuildingSpots(pLocal, F::BotUtils.m_tClosestEnemy);
}

auto CNavBotJobSystem::Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon) -> NavBotJobResult_t
{
	NavBotJobResult_t tResult{};
	if (!pCmd || !pLocal || !pWeapon)
		return tResult;

	std::array aCandidates =
	{
		JobCandidate_t{ EJobKind::EscapeSpawn, GetEscapeSpawnScore(pLocal) },
		JobCandidate_t{ EJobKind::EscapeProjectiles, GetProjectileEscapeScore(pLocal) },
		JobCandidate_t{ EJobKind::EscapeDanger, GetEscapeDangerScore(pLocal) },
		JobCandidate_t{ EJobKind::GetHealth, GetHealthScore(pLocal, false) },
		JobCandidate_t{ EJobKind::Engineer, GetEngineerScore(pLocal) },
		JobCandidate_t{ EJobKind::RunReload, GetRunReloadScore(pLocal) },
		JobCandidate_t{ EJobKind::Melee, GetMeleeScore(pLocal) },
		JobCandidate_t{ EJobKind::GetAmmo, GetAmmoScore(pLocal) },
		JobCandidate_t{ EJobKind::Capture, GetCaptureScore(pLocal) },
		JobCandidate_t{ EJobKind::SnipeSentry, GetSnipeSentryScore(pLocal) },
		JobCandidate_t{ EJobKind::SafeReload, GetSafeReloadScore(pLocal) },
		JobCandidate_t{ EJobKind::StayNear, GetStayNearScore(pLocal, pWeapon) },
		JobCandidate_t{ EJobKind::LowPrioHealth, GetHealthScore(pLocal, true) },
		JobCandidate_t{ EJobKind::GroupWithOthers, GetGroupWithOthersScore() },
		JobCandidate_t{ EJobKind::Roam, GetRoamScore() }
	};

	if (F::ZIController.IsZombie())
	{
		for (auto& tCandidate : aCandidates)
		{
			switch (tCandidate.m_eKind)
			{
			case EJobKind::GetHealth:
			case EJobKind::Engineer:
			case EJobKind::RunReload:
			case EJobKind::GetAmmo:
			case EJobKind::SnipeSentry:
			case EJobKind::SafeReload:
			case EJobKind::StayNear:
			case EJobKind::LowPrioHealth:
				tCandidate.m_flScore = 0.f;
				break;
			default:
				break;
			}
		}
	}

	while (auto pCandidate = FindBestCandidate(aCandidates))
	{
		bool bHasJob = false;
		switch (pCandidate->m_eKind)
		{
		case EJobKind::EscapeSpawn:
			bHasJob = F::NavBotDanger.EscapeSpawn(pLocal);
			break;
		case EJobKind::EscapeProjectiles:
			bHasJob = F::NavBotDanger.EscapeProjectiles(pLocal);
			break;
		case EJobKind::EscapeDanger:
			bHasJob = F::NavBotDanger.EscapeDanger(pLocal);
			break;
		case EJobKind::GetHealth:
			bHasJob = F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health);
			break;
		case EJobKind::Engineer:
			bHasJob = F::NavBotEngineer.Run(pCmd, pLocal, F::BotUtils.m_tClosestEnemy);
			break;
		case EJobKind::RunReload:
			tResult.m_bRunReload = bHasJob = F::NavBotReload.Run();
			break;
		case EJobKind::Melee:
			bHasJob = F::NavBotMelee.Run(pCmd, pLocal, F::BotUtils.m_iCurrentSlot, F::BotUtils.m_tClosestEnemy);
			break;
		case EJobKind::GetAmmo:
			bHasJob = F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo);
			break;
		case EJobKind::Capture:
			bHasJob = F::NavBotCapture.Run(pCmd, pLocal, pWeapon);
			break;
		case EJobKind::SnipeSentry:
			bHasJob = F::NavBotSnipe.Run(pLocal);
			break;
		case EJobKind::SafeReload:
			tResult.m_bRunSafeReload = bHasJob = F::NavBotReload.RunSafe();
			break;
		case EJobKind::StayNear:
			bHasJob = F::NavBotStayNear.Run(pLocal, pWeapon);
			break;
		case EJobKind::LowPrioHealth:
			bHasJob = F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::LowPrio);
			break;
		case EJobKind::GroupWithOthers:
			bHasJob = F::NavBotGroup.Run(pLocal);
			break;
		case EJobKind::Roam:
			bHasJob = F::NavBotRoam.Run(pLocal, pWeapon);
			break;
		}

		if (bHasJob)
		{
			if (pCandidate->m_eKind != EJobKind::Engineer)
				F::NavBotEngineer.m_eTaskStage = EngineerTaskStageEnum::None;

			tResult.m_bHasJob = true;
			return tResult;
		}

		pCandidate->m_flScore = 0.f;
	}

	F::NavBotEngineer.m_eTaskStage = EngineerTaskStageEnum::None;
	return tResult;
}

void CNavBotJobSystem::Reset()
{
	m_bDangerLatch = false;
	F::NavBotStayNear.Reset();
	F::NavBotReload.m_iLastReloadSlot = -1;
	F::NavBotSnipe.m_iTargetIdx = -1;
	F::NavBotSupplies.Reset();
	F::NavBotEngineer.Reset();
	F::NavBotCapture.Reset();
	F::NavBotRoam.Reset();
	F::NavBotGroup.Reset();
	F::NavBotMelee.Reset();
	F::NavBotDanger.ResetSpawn();
	F::NavBotMVMSniper.Reset();
	F::MissionBoard.Reset();
}
