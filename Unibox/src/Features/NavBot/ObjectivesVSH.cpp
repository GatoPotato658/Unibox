#include "Objectives.h"
#include "NavEngine.h"
#include "BotUtils.h"
#include "Jobs/NavBotJobs.h"
#include "../FollowBot/FollowBot.h"
#include "../Misc/Misc.h"
#include "../Ticks/Ticks.h"

using namespace ObjectiveUtils;

namespace
{
	constexpr int kBossMinHealth = 1000;

	constexpr float kChargeSpeed = 1400.f;
	constexpr float kChargeMinDuration = 0.4f;
	constexpr float kChargeMaxDuration = 1.f;
	constexpr float kChargeMeterPerSecond = 1.66f;
	constexpr float kChargeMeterPerTick = 0.01f;
	constexpr float kChargeCooldown = 10.f;
	constexpr float kChargeLaneHalfWidth = 140.f;
	constexpr float kChargeLaneLength = 1400.f;
	constexpr float kChargeMinDistance = 400.f;
	constexpr float kChargeMaxDistance = 1150.f;

	constexpr float kSlamRadius = 500.f;
	constexpr float kSlamCooldown = 15.f;
	constexpr float kSlamMinHeight = 150.f;
	constexpr float kJumpRechargeTime = 3.5f;

	constexpr float kFailedAbilityCooldown = 4.f;
	constexpr float kAbilityTimeout = 4.f;
	constexpr float kSetupBossKeepOut = 600.f;
	constexpr float kPointBlankRadius = 150.f;
	constexpr float kPunchReadyRadius = 650.f;

	bool HasBossModel(CBaseEntity* pPlayer)
	{
		auto pModel = pPlayer->GetModel();
		const char* pszModelName = pModel ? I::ModelInfoClient->GetModelName(pModel) : nullptr;
		return pszModelName && std::string_view(pszModelName).find("vsh/player/") != std::string_view::npos;
	}

	float GetHeightAboveGround(const Vector& vOrigin, float flMaxHeight)
	{
		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnly filter = {};
		SDK::Trace(vOrigin, vOrigin - Vector(0.f, 0.f, flMaxHeight), MASK_PLAYERSOLID, &filter, &trace);
		return trace.fraction * flMaxHeight;
	}

	bool IsPathClear(const Vector& vFrom, const Vector& vTo, float flHalfExtent)
	{
		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnly filter = {};
		SDK::TraceHull(vFrom, vTo, Vector(-flHalfExtent, -flHalfExtent, -40.f), Vector(flHalfExtent, flHalfExtent, 40.f), MASK_PLAYERSOLID, &filter, &trace);
		return trace.fraction >= 1.f;
	}

	bool IsLaneSupported(const Vector& vFrom, const Vector& vTo)
	{
		if (!F::NavEngine.IsNavMeshLoaded())
			return true;

		Vector vDirection = vTo - vFrom;
		const float flLength = vDirection.Length();
		if (flLength < 1.f)
			return true;
		vDirection /= flLength;

		for (float flStep = 128.f; flStep < flLength; flStep += 128.f)
		{
			const Vector vPoint = vFrom + vDirection * flStep;
			auto pArea = F::NavEngine.FindClosestNavArea(vPoint, false);
			if (!pArea)
				return false;

			const Vector vNearest = pArea->GetNearestPoint(vPoint.Get2D());
			if (vNearest.DistTo2D(vPoint) > 96.f)
				return false;
		}
		return true;
	}

	Vector GetLeadPosition(CTFPlayer* pTarget, float flTime)
	{
		Vector vVelocity = pTarget->GetAbsVelocity();
		vVelocity.z = 0.f;
		return pTarget->GetCenter() + vVelocity * flTime;
	}

	void AimAt(CUserCmd* pCmd, const Vector& vEye, const Vector& vTarget)
	{
		Vec3 vAngles = Math::CalcAngle(vEye, vTarget);
		Math::ClampAngles(vAngles);
		pCmd->viewangles = vAngles;
	}
}

void CVSHController::Reset()
{
	m_tBoss = {};
	m_iMercTeam = TEAM_UNASSIGNED;
	m_bLocalBoss = false;
	m_bInSetup = false;
	m_iHuntIdx = -1;
	m_eAbility = VSHAbilityEnum::None;
	m_iPhase = 0;
	m_iPhaseTicks = 0;
	m_bSawDash = false;
	m_flChargeReady = m_flSlamReady = m_flLastJump = 0.f;
}

void CVSHController::Update()
{
	m_tBoss = {};
	m_iMercTeam = TEAM_UNASSIGNED;
	m_bLocalBoss = false;

	const auto pGameRules = I::TFGameRules();
	m_bInSetup = pGameRules && (pGameRules->m_bInSetup() || pGameRules->m_iRoundState() == GR_STATE_PREROUND);
	if (m_bInSetup)
		m_flChargeReady = m_flSlamReady = m_flLastJump = 0.f;

	auto pLocal = H::Entities.GetLocal();
	auto pResource = H::Entities.GetResource();
	if (!pLocal || !pResource)
		return;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		const int iIdx = pEntity->entindex();
		const int iTeam = pResource->m_iTeam(iIdx);
		if (!pResource->m_bAlive(iIdx) || (iTeam != TF_TEAM_RED && iTeam != TF_TEAM_BLUE)
			|| pResource->m_iPlayerClass(iIdx) != TF_CLASS_HEAVY
			|| (pResource->m_iMaxHealth(iIdx) < kBossMinHealth && !HasBossModel(pEntity)))
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		m_tBoss.m_pPlayer = pPlayer;
		m_tBoss.m_iIdx = iIdx;
		m_tBoss.m_iTeam = iTeam;
		m_tBoss.m_bDormant = pPlayer->IsDormant();
		m_tBoss.m_bHasPos = F::BotUtils.GetDormantOrigin(iIdx, &m_tBoss.m_vOrigin);
		if (!m_tBoss.m_bDormant)
		{
			m_tBoss.m_bWindup = pPlayer->InCond(TF_COND_AIMING);
			m_tBoss.m_bDashing = pPlayer->InCond(TF_COND_SHIELD_CHARGE);
			m_tBoss.m_bSlamming = !pPlayer->IsOnGround() && pPlayer->IsDucking() && pPlayer->GetAbsVelocity().z < -200.f;
			m_tBoss.m_bPunchReady = pPlayer->InCond(TF_COND_CRITBOOSTED);
		}
		break;
	}

	if (m_tBoss.m_iIdx == -1)
		return;

	m_bLocalBoss = m_tBoss.m_iIdx == pLocal->entindex();
	m_iMercTeam = m_tBoss.m_iTeam == TF_TEAM_RED ? TF_TEAM_BLUE : TF_TEAM_RED;
}

bool CVSHController::IsMovementHeld() const
{
	return m_bLocalBoss && (m_bInSetup || (m_eAbility != VSHAbilityEnum::None && I::GlobalVars->curtime - m_flAbilityStart <= kAbilityTimeout));
}

float CVSHController::GetStandoff(CTFPlayer* pLocal) const
{
	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT: return 500.f;
	case TF_CLASS_SOLDIER:
	case TF_CLASS_DEMOMAN: return 750.f;
	case TF_CLASS_MEDIC: return 700.f;
	case TF_CLASS_SNIPER: return 1000.f;
	case TF_CLASS_ENGINEER: return F::NavBotEngineer.IsEngieMode(pLocal) ? 0.f : 700.f;
	default: return 0.f;
	}
}

void CVSHController::GetBossZones(CTFPlayer* pLocal, std::vector<VSHZone_t>& vOut) const
{
	vOut.clear();
	if (!IsActive() || m_bLocalBoss || !m_tBoss.m_bHasPos || pLocal->m_iTeamNum() != m_iMercTeam)
		return;

	const Vector vBoss = m_tBoss.m_vOrigin;
	if (m_bInSetup)
	{
		vOut.push_back({ vBoss, vBoss, kSetupBossKeepOut, 0.f, false, true });
		return;
	}

	float flRadius = GetStandoff(pLocal);
	if (flRadius > 0.f)
	{
		if (m_tBoss.m_bPunchReady)
			flRadius = std::max(flRadius, kPunchReadyRadius);
		if (m_tBoss.m_bDormant)
			flRadius *= 0.7f;
		vOut.push_back({ vBoss, vBoss, flRadius, kPointBlankRadius, false, false });
	}

	if (m_tBoss.m_bDormant)
		return;

	if (m_tBoss.m_bWindup || m_tBoss.m_bDashing)
	{
		Vec3 vForward;
		Math::AngleVectors(m_tBoss.m_pPlayer->GetEyeAngles(), &vForward);
		vOut.push_back({ vBoss, vBoss + vForward * kChargeLaneLength, kChargeLaneHalfWidth, 0.f, true, true });
	}

	if (m_tBoss.m_bSlamming)
		vOut.push_back({ vBoss, vBoss, kSlamRadius + 60.f, 0.f, false, true });
}

float CVSHController::GetHuntScore(CTFPlayer* pTarget, float flDist)
{
	float flScore = flDist;
	switch (pTarget->m_iClass())
	{
	case TF_CLASS_MEDIC: flScore -= 450.f; break;
	case TF_CLASS_SPY: flScore -= 250.f; break;
	case TF_CLASS_SNIPER: flScore -= 200.f; break;
	case TF_CLASS_DEMOMAN:
	case TF_CLASS_ENGINEER: flScore -= 100.f; break;
	default: break;
	}

	if (pTarget->IsInvulnerable())
		flScore += 1500.f;
	if (pTarget->IsDormant())
		flScore += 500.f;

	const int iTargetIdx = pTarget->entindex();
	const Vector vTarget = pTarget->GetAbsOrigin();
	int iNeighbors = 0;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (pEntity->entindex() == iTargetIdx || pEntity->IsDormant() || !pEntity->As<CTFPlayer>()->IsAlive())
			continue;

		if (pEntity->GetAbsOrigin().DistTo(vTarget) <= 500.f)
			iNeighbors++;
	}
	flScore += std::min(iNeighbors, 4) * 90.f;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (!pEntity->IsDormant() && pEntity->GetClassID() == ETFClassID::CObjectSentrygun && pEntity->GetAbsOrigin().DistTo(vTarget) <= 900.f)
		{
			flScore += 600.f;
			break;
		}
	}

	flScore += static_cast<float>(pTarget->m_iHealth()) / std::max(1, pTarget->GetMaxHealth()) * 150.f;
	if (iTargetIdx == m_iHuntIdx)
		flScore -= 200.f;
	return flScore;
}

bool CVSHController::GetGatherPoint(CTFPlayer* pLocal, Vector& vOut) const
{
	if (!IsActive() || m_bLocalBoss)
		return false;

	Vector vSum = {};
	int iCount = 0;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		Vector vOrigin;
		if (pEntity->entindex() == pLocal->entindex() || !F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin)
			|| vOrigin.DistTo(pLocal->GetAbsOrigin()) > 3000.f)
			continue;

		vSum += vOrigin;
		iCount++;
	}
	if (!iCount)
		return false;

	vOut = AdjustPosToNav(vSum / static_cast<float>(iCount));
	return !m_tBoss.m_bHasPos || vOut.DistTo(m_tBoss.m_vOrigin) > 700.f;
}

bool CVSHController::GetPatrolAnchor(CTFPlayer* pLocal, Vector& vOut) const
{
	if (!m_bLocalBoss)
		return false;

	Vector vSum = {};
	int iCount = 0;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		Vector vOrigin;
		if (F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
		{
			vSum += vOrigin;
			iCount++;
		}
	}
	if (iCount)
	{
		vOut = AdjustPosToNav(vSum / static_cast<float>(iCount));
		return true;
	}

	auto pObjective = H::Entities.GetObjectiveResource();
	if (!pObjective || pObjective->m_iNumControlPoints() <= 0)
		return false;

	vOut = AdjustPosToNav(pObjective->m_vCPPositions(0));
	return true;
}

void CVSHController::EndAbility(bool bSuccess)
{
	const float flNow = I::GlobalVars->curtime;
	if (m_eAbility == VSHAbilityEnum::Charge)
		m_flChargeReady = flNow + (bSuccess ? kChargeCooldown : kFailedAbilityCooldown);
	else if (m_eAbility == VSHAbilityEnum::Slam)
	{
		m_flSlamReady = flNow + (bSuccess ? kSlamCooldown : kFailedAbilityCooldown);
		m_flLastJump = flNow;
	}

	m_eAbility = VSHAbilityEnum::None;
	m_iPhase = 0;
	m_iPhaseTicks = 0;
	m_bSawDash = false;
}

bool CVSHController::TryStartCharge(CTFPlayer* pLocal, CTFPlayer* pTarget, float flDist)
{
	if (I::GlobalVars->curtime < m_flChargeReady || !pLocal->IsOnGround() || pLocal->IsTaunting() || pTarget->IsInvulnerable()
		|| flDist < kChargeMinDistance || flDist > kChargeMaxDistance)
		return false;

	const Vector vFrom = pLocal->GetCenter();
	const Vector vTo = pTarget->GetCenter();
	if (std::fabs(vTo.z - vFrom.z) > 220.f || !IsPathClear(vFrom, vTo, 34.f) || !IsLaneSupported(vFrom, vTo))
		return false;

	const float flDuration = std::clamp((flDist + 60.f) / kChargeSpeed, kChargeMinDuration, kChargeMaxDuration);
	const float flMeter = std::clamp((flDuration - kChargeMinDuration) * kChargeMeterPerSecond, kChargeMeterPerTick, 1.f);
	m_iWindupTicks = std::max(2, static_cast<int>(std::ceil(flMeter / kChargeMeterPerTick)));
	m_eAbility = VSHAbilityEnum::Charge;
	m_iAbilityTarget = pTarget->entindex();
	m_flAbilityStart = I::GlobalVars->curtime;
	m_iPhase = 0;
	m_iPhaseTicks = 0;
	m_bSawDash = false;
	return true;
}

bool CVSHController::TryStartSlam(CTFPlayer* pLocal, CTFPlayer* pTarget)
{
	const float flNow = I::GlobalVars->curtime;
	if (flNow < m_flSlamReady || flNow - m_flLastJump < kJumpRechargeTime || !pLocal->IsOnGround() || pLocal->IsTaunting())
		return false;

	const Vector vFrom = pLocal->GetCenter();
	const Vector vTo = pTarget->GetCenter();
	const float flDist2D = vFrom.DistTo2D(vTo);
	if (flDist2D < 220.f || flDist2D > 650.f || std::fabs(vTo.z - vFrom.z) > 120.f || !IsPathClear(vFrom, vTo, 20.f))
		return false;

	CGameTrace tCeiling = {};
	CTraceFilterWorldAndPropsOnly filter = {};
	SDK::Trace(vFrom, vFrom + Vector(0.f, 0.f, 700.f), MASK_PLAYERSOLID, &filter, &tCeiling);
	if (tCeiling.fraction < 0.93f)
		return false;

	int iCluster = 0;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (!pEntity->IsDormant() && pEntity->As<CTFPlayer>()->IsAlive() && pEntity->GetAbsOrigin().DistTo(pTarget->GetAbsOrigin()) <= 450.f)
			iCluster++;
	}
	if (iCluster < 2)
		return false;

	m_eAbility = VSHAbilityEnum::Slam;
	m_iAbilityTarget = pTarget->entindex();
	m_flAbilityStart = flNow;
	m_iPhase = 0;
	m_iPhaseTicks = 0;
	return true;
}

void CVSHController::TickCharge(CTFPlayer* pLocal, CTFPlayer* pTarget, CUserCmd* pCmd)
{
	pCmd->forwardmove = pCmd->sidemove = pCmd->upmove = 0.f;
	pCmd->buttons &= ~(IN_ATTACK | IN_JUMP | IN_DUCK | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);

	const Vector vEye = pLocal->GetEyePosition();
	const float flTravel = vEye.DistTo(pTarget->GetCenter());
	const int iTicksLeft = std::max(0, m_iWindupTicks - m_iPhaseTicks);
	AimAt(pCmd, vEye, GetLeadPosition(pTarget, flTravel / kChargeSpeed + iTicksLeft * I::GlobalVars->interval_per_tick));

	m_iPhaseTicks++;
	if (m_iPhase == 0)
	{
		if (m_iPhaseTicks > m_iWindupTicks)
		{
			m_iPhase = 1;
			m_iPhaseTicks = 0;
			return;
		}

		pCmd->buttons |= IN_ATTACK3;
		if (m_iPhaseTicks > TIME_TO_TICKS(0.4f) && !pLocal->InCond(TF_COND_AIMING))
			EndAbility(false);
		return;
	}

	const bool bDashing = pLocal->InCond(TF_COND_SHIELD_CHARGE);
	m_bSawDash |= bDashing;
	if (m_bSawDash && !bDashing)
		EndAbility(true);
	else if (!m_bSawDash && m_iPhaseTicks > TIME_TO_TICKS(0.7f))
		EndAbility(false);
}

void CVSHController::TickSlam(CTFPlayer* pLocal, CTFPlayer* pTarget, CUserCmd* pCmd)
{
	const Vector vOrigin = pLocal->GetAbsOrigin();
	const Vector vTarget = pTarget->GetAbsOrigin();
	const bool bOnGround = pLocal->IsOnGround();

	Vec3 vAngles = Math::CalcAngle(pLocal->GetEyePosition(), vTarget);
	vAngles.x = pCmd->viewangles.x;
	pCmd->viewangles = vAngles;
	pCmd->sidemove = 0.f;
	pCmd->upmove = 0.f;
	pCmd->buttons &= ~(IN_ATTACK | IN_JUMP | IN_DUCK | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);

	m_iPhaseTicks++;
	switch (m_iPhase)
	{
	case 0:
		pCmd->forwardmove = 0.f;
		pCmd->buttons |= IN_JUMP;
		if (!bOnGround)
		{
			m_iPhase = 1;
			m_iPhaseTicks = 0;
		}
		else if (m_iPhaseTicks > TIME_TO_TICKS(0.5f))
			EndAbility(false);
		break;
	case 1:
		pCmd->forwardmove = 0.f;
		if (bOnGround)
		{
			EndAbility(false);
			break;
		}
		if (m_iPhaseTicks >= 3)
		{
			m_iPhase = 2;
			m_iPhaseTicks = 0;
		}
		break;
	case 2:
		pCmd->forwardmove = 450.f;
		pCmd->buttons |= IN_JUMP | IN_FORWARD;
		m_iPhase = 3;
		m_iPhaseTicks = 0;
		break;
	default:
	{
		pCmd->forwardmove = 450.f;
		pCmd->buttons |= IN_FORWARD;

		if (m_iPhase == 3)
		{
			if (bOnGround && m_iPhaseTicks > 6)
			{
				EndAbility(false);
				return;
			}

			const float flDist2D = vOrigin.DistTo2D(vTarget);
			const float flVelocityZ = pLocal->GetAbsVelocity().z;
			if (m_iPhaseTicks > 6 && flVelocityZ < 0.f && GetHeightAboveGround(vOrigin, 600.f) > kSlamMinHeight + 50.f
				&& (flDist2D <= 260.f || (flVelocityZ < -250.f && flDist2D <= 400.f)))
			{
				m_iPhase = 4;
				m_iPhaseTicks = 0;
			}
		}

		if (m_iPhase == 4)
		{
			pCmd->buttons |= IN_DUCK;
			if (bOnGround && m_iPhaseTicks > 3)
			{
				EndAbility(true);
				return;
			}
		}
		break;
	}
	}
}

void CVSHController::RunBoss(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	const bool bManualInput = pCmd && (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT)) && !F::Misc.m_bAntiAFK;
	if (!m_bLocalBoss || !pLocal || !pCmd || !pLocal->IsAlive() || m_bInSetup || bManualInput
		|| !Vars::Misc::Movement::NavBot::Enabled.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value
		|| F::FollowBot.m_bActive || F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
	{
		if (m_eAbility != VSHAbilityEnum::None)
			EndAbility(false);
		return;
	}

	const auto& tEnemy = F::BotUtils.m_tClosestEnemy;
	auto pTarget = tEnemy.m_pPlayer;
	if (m_eAbility != VSHAbilityEnum::None)
	{
		auto pAbilityTarget = I::ClientEntityList->GetClientEntity(m_iAbilityTarget);
		if (!pAbilityTarget || pAbilityTarget->IsDormant() || !pAbilityTarget->As<CTFPlayer>()->IsAlive()
			|| I::GlobalVars->curtime - m_flAbilityStart > kAbilityTimeout
			|| (pLocal->m_fFlags() & FL_FROZEN) || pLocal->IsTaunting() || pLocal->InCond(TF_COND_STUNNED))
		{
			EndAbility(false);
			return;
		}

		if (m_eAbility == VSHAbilityEnum::Charge)
			TickCharge(pLocal, pAbilityTarget->As<CTFPlayer>(), pCmd);
		else
			TickSlam(pLocal, pAbilityTarget->As<CTFPlayer>(), pCmd);
		return;
	}

	if (!pTarget || pTarget->IsDormant() || !m_tDecision.Run(0.1f))
		return;

	if (TryStartCharge(pLocal, pTarget, pLocal->GetCenter().DistTo(pTarget->GetCenter())))
		TickCharge(pLocal, pTarget, pCmd);
	else if (TryStartSlam(pLocal, pTarget))
		TickSlam(pLocal, pTarget, pCmd);
}
