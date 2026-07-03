#include "BotUtils.h"
#include "../Aimbot/AimbotGlobal/AimbotGlobal.h"

static bool SmoothAimHasPriority()
{
	const auto iAimType = Vars::Aimbot::General::AimType.Value;
	if (iAimType != Vars::Aimbot::General::AimTypeEnum::Smooth
		&& iAimType != Vars::Aimbot::General::AimTypeEnum::SmoothVelocity
		&& iAimType != Vars::Aimbot::General::AimTypeEnum::Assistive)
		return false;
	return G::AimbotSteering;
}

struct LegitPathCandidate_t
{
	Vec3 m_vPosition = {};
	float m_flScore = -FLT_MAX;
	bool m_bValid = false;
};

static Vec3 GetFlatDirection(Vec3 vDirection, const Vec3& vFallback)
{
	vDirection.z = 0.f;
	if (vDirection.Normalize() > 24.f)
		return vDirection;

	Vec3 vFlatFallback = vFallback;
	vFlatFallback.z = 0.f;
	if (vFlatFallback.Normalize() > 0.01f)
		return vFlatFallback;

	return { 1.f, 0.f, 0.f };
}

static bool IsPathTargetForward(const Vec3& vOrigin, const Vec3& vTarget, const Vec3& vPathDirection, float flMinDot)
{
	Vec3 vToTarget = vTarget - vOrigin;
	vToTarget.z = 0.f;
	if (vToTarget.Normalize() < 24.f)
		return true;
	return vPathDirection.Dot(vToTarget) >= flMinDot;
}

static Vec3 MakePositionFromYaw(const Vec3& vOrigin, const Vec3& vDirection, float flYawOffset, float flDistance, float flHeight)
{
	Vec3 vAngles = Math::CalcAngle(Vec3(), vDirection);
	vAngles.y += flYawOffset;

	Vec3 vForward = {};
	Math::AngleVectors(vAngles, &vForward);
	vForward.z = 0.f;
	if (vForward.Normalize() <= 0.01f)
		vForward = vDirection;

	return vOrigin + vForward * flDistance + Vec3(0.f, 0.f, flHeight);
}

static bool TracePathTargetVisible(CTFPlayer* pLocal, const Vec3& vEye, const Vec3& vTarget, float* pFraction)
{
	CGameTrace tTrace = {};
	CTraceFilterWorldAndPropsOnly tFilter(pLocal);
	SDK::Trace(vEye, vTarget, MASK_SHOT | CONTENTS_GRATE, &tFilter, &tTrace);
	if (pFraction)
		*pFraction = tTrace.fraction;
	return tTrace.fraction >= 0.98f || !tTrace.DidHit();
}

static float TracePathTargetOpen(CTFPlayer* pLocal, const Vec3& vFrom, const Vec3& vTarget)
{
	CGameTrace tTrace = {};
	CTraceFilterNavigation tFilter(pLocal);
	Vec3 vStart = vFrom + Vec3(0.f, 0.f, 18.f);
	Vec3 vEnd = vTarget;
	vEnd.z = vStart.z;
	SDK::TraceHull(vStart, vEnd, pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &tFilter, &tTrace);
	return tTrace.fraction;
}

static LegitPathCandidate_t ScorePathCandidate(CTFPlayer* pLocal, const Vec3& vEye, const Vec3& vOrigin, const Vec3& vTarget, const Vec3& vPathDirection, const Vec3& vCurrentTarget, bool bAllowSteep)
{
	LegitPathCandidate_t tResult = {};
	if (vTarget.IsZero())
		return tResult;

	Vec3 vToTarget = vTarget - vOrigin;
	Vec3 vFlatToTarget = vToTarget;
	vFlatToTarget.z = 0.f;
	const float flFlatDistance = vFlatToTarget.Normalize();
	if (flFlatDistance < 24.f)
		return tResult;

	const float flPitch = Math::CalcAngle(vEye, vTarget).x;
	if (!bAllowSteep && (flPitch < -24.f || flPitch > 20.f))
		return tResult;

	float flSightFraction = 0.f;
	const bool bVisible = TracePathTargetVisible(pLocal, vEye, vTarget, &flSightFraction);
	const float flOpenFraction = TracePathTargetOpen(pLocal, vOrigin, vTarget);
	if (!bVisible && flSightFraction < 0.55f)
		return tResult;
	if (flOpenFraction < 0.2f)
		return tResult;

	const float flAlignment = std::clamp(vPathDirection.Dot(vFlatToTarget), -1.f, 1.f);
	if (flAlignment < 0.45f)
		return tResult;

	const float flDistanceScore = 1.f - std::fabs(std::clamp(flFlatDistance, 120.f, 520.f) - 330.f) / 330.f;
	const float flHeightPenalty = std::min(std::fabs(vTarget.z - vEye.z) / (bAllowSteep ? 220.f : 110.f), 1.f);
	const float flContinuity = vCurrentTarget.IsZero() ? 0.5f : 1.f - std::min(vCurrentTarget.DistTo(vTarget) / 480.f, 1.f);

	tResult.m_vPosition = vTarget;
	tResult.m_flScore = flAlignment * 2.5f
		+ flDistanceScore * 1.25f
		+ flSightFraction * 1.75f
		+ flOpenFraction * 0.9f
		+ flContinuity * 1.4f
		- flHeightPenalty * 1.2f;
	tResult.m_bValid = true;
	return tResult;
}

static Vec3 ClampAngleSpeed(const Vec3& vFrom, const Vec3& vTarget, float& flPitchVelocity, float& flYawVelocity, float flSpeed, bool bEnemyLock)
{
	Vec3 vDelta = vTarget.DeltaAngle(vFrom);
	const float flTick = std::max(I::GlobalVars->interval_per_tick, 0.001f);
	const float flPitchLimit = (bEnemyLock ? 125.f : 55.f) * std::clamp(flSpeed / 25.f, 0.45f, 2.2f);
	const float flYawLimit = (bEnemyLock ? 180.f : 80.f) * std::clamp(flSpeed / 25.f, 0.45f, 2.2f);
	const float flPitchAccel = (bEnemyLock ? 520.f : 220.f) * flTick;
	const float flYawAccel = (bEnemyLock ? 720.f : 320.f) * flTick;
	const float flDesiredPitchVelocity = std::clamp(vDelta.x / flTick, -flPitchLimit, flPitchLimit);
	const float flDesiredYawVelocity = std::clamp(vDelta.y / flTick, -flYawLimit, flYawLimit);

	flPitchVelocity += std::clamp(flDesiredPitchVelocity - flPitchVelocity, -flPitchAccel, flPitchAccel);
	flYawVelocity += std::clamp(flDesiredYawVelocity - flYawVelocity, -flYawAccel, flYawAccel);
	flPitchVelocity = std::clamp(flPitchVelocity, -flPitchLimit, flPitchLimit);
	flYawVelocity = std::clamp(flYawVelocity, -flYawLimit, flYawLimit);

	Vec3 vResult = vFrom;
	vResult.x += flPitchVelocity * flTick;
	vResult.y += flYawVelocity * flTick;
	Math::ClampAngles(vResult);
	return vResult;
}

void CBotUtils::DoSlowAim(Vec3& vWishAngles, float flSpeed, Vec3 vPreviousAngles)
{
	float flAimSpeed = std::max(flSpeed, 1.f);
	Vec3 vSlowDelta = vWishAngles.DeltaAngle(vPreviousAngles);

	const float flPitchStep = std::clamp(std::fabs(vSlowDelta.x) / (flAimSpeed * 1.35f), 0.1f, std::max(0.35f, 18.f / std::sqrt(flAimSpeed)));
	const float flYawStep = std::clamp(std::fabs(vSlowDelta.y) / flAimSpeed, 0.15f, std::max(0.5f, 32.f / std::sqrt(flAimSpeed)));

	vPreviousAngles.x += std::clamp(vSlowDelta.x, -flPitchStep, flPitchStep);
	vPreviousAngles.y += std::clamp(vSlowDelta.y, -flYawStep, flYawStep);
	vWishAngles = vPreviousAngles;
	Math::ClampAngles(vWishAngles);
}

void CBotUtils::LookAtPath(CUserCmd* pCmd, Vec2 vDest, Vec3 vLocalEyePos, bool bSilent)
{
	if (SmoothAimHasPriority())
	{
		m_vLastAngles = I::EngineClient->GetViewAngles();
		return;
	}

	Vec3 vWishAng{ vDest.x, vDest.y, vLocalEyePos.z };
	vWishAng = Math::CalcAngle(vLocalEyePos, vWishAng);

	DoSlowAim(vWishAng, static_cast<float>(Vars::Misc::Movement::BotUtils::LookAtPathSpeed.Value), m_vLastAngles);
	if (bSilent)
		pCmd->viewangles = vWishAng;
	else
		I::EngineClient->SetViewAngles(vWishAng);
	m_vLastAngles = vWishAng;
}

void CBotUtils::LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth)
{
	if (SmoothAimHasPriority())
	{
		m_vLastAngles = I::EngineClient->GetViewAngles();
		return;
	}

	if (bSmooth)
		DoSlowAim(vWishAngles, 25.f, m_vLastAngles);

	if (bSilent)
		pCmd->viewangles = vWishAngles;
	else
		I::EngineClient->SetViewAngles(vWishAngles);
	m_vLastAngles = vWishAngles;
}

void CBotUtils::LookLegit(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vDest, bool bSilent)
{
	if (!pLocal)
		return;

	auto& tState = m_tLLAP;

	if (SmoothAimHasPriority())
	{
		Vec3 vCurrent = I::EngineClient->GetViewAngles();
		m_vLastAngles = vCurrent;
		if (tState.m_bInitialized)
			tState.m_vAnchor = vCurrent;
		return;
	}

	Vec3 vEye = pLocal->GetEyePosition();
	Vec3 vLook = vDest;
	bool bEnemyLock = false;
	const int iPreviousTarget = tState.m_iLastTarget;
	int iTrackedTarget = -1;
	const Vec3 vVelocity = pLocal->m_vecVelocity();
	const float flSpeed = vVelocity.Length2D();
	const Vec3 vOrigin = pLocal->GetAbsOrigin();
	Vec3 vPathDelta = vDest.IsZero() ? Vec3{} : vDest - vOrigin;
	const bool bHasPathTarget = !vDest.IsZero() && vPathDelta.Length2D() > 24.f;
	const Vec3 vPathDirection = GetFlatDirection(vPathDelta, bHasPathTarget ? Vec3{} : vVelocity);

	CBaseEntity* pBestEnemy = nullptr;
	float flBestDist = FLT_MAX;
	auto pWeapon = pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>();

	if (G::AimTarget.m_iEntIndex)
	{
		if (auto pTarget = I::ClientEntityList->GetClientEntity(G::AimTarget.m_iEntIndex)->As<CBaseEntity>())
		{
			if (pTarget->IsPlayer() ? pTarget->As<CTFPlayer>()->IsAlive() : (pTarget->IsBuilding() ? pTarget->As<CBaseObject>()->m_iHealth() > 0 : false))
			{
				Vec3 vTargetPos = pTarget->IsPlayer() ? pTarget->As<CTFPlayer>()->GetEyePosition() : pTarget->GetCenter();
				if (SDK::VisPos(pLocal, pTarget, vEye, vTargetPos))
				{
					pBestEnemy = pTarget;
					flBestDist = -1.f;
				}
			}
		}
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pEnemy = pEntity->As<CTFPlayer>();
		if (!pEnemy || !pEnemy->IsAlive() || pEnemy->IsDormant())
			continue;

		if (ShouldTarget(pLocal, pWeapon, pEnemy->entindex()) == ShouldTargetEnum::DontTarget)
			continue;

		Vec3 vEnemyEye = pEnemy->GetEyePosition();
		if (SDK::VisPos(pLocal, pEnemy, vEye, vEnemyEye))
		{
			float flDist = vEye.DistTo(vEnemyEye);
			if (flDist < flBestDist)
			{
				flBestDist = flDist;
				pBestEnemy = pEnemy;
			}
		}
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (!pBuilding || pBuilding->m_iHealth() <= 0 || pBuilding->IsDormant())
			continue;

		if (ShouldTargetBuilding(pLocal, pBuilding->entindex()) == ShouldTargetEnum::DontTarget)
			continue;

		Vec3 vBuildingCenter = pBuilding->GetCenter();
		if (SDK::VisPos(pLocal, pBuilding, vEye, vBuildingCenter))
		{
			float flDist = vEye.DistTo(vBuildingCenter);
			if (flDist < flBestDist)
			{
				flBestDist = flDist;
				pBestEnemy = pBuilding;
			}
		}
	}

	if (pBestEnemy)
	{
		if (pBestEnemy->IsPlayer())
		{
			vLook = pBestEnemy->As<CTFPlayer>()->GetEyePosition();
			vLook.z -= 10.f;
			Vec3 vTargetVelocity = pBestEnemy->GetAbsVelocity();
			const float flLeadTime = std::clamp(vEye.DistTo(vLook) / 2800.f, 0.015f, 0.08f);
			vLook += vTargetVelocity * flLeadTime;
		}
		else
			vLook = pBestEnemy->GetCenter();

		iTrackedTarget = pBestEnemy->entindex();
		tState.m_flLastSeen = I::GlobalVars->curtime;
		tState.m_vLastPos = vLook;
		bEnemyLock = true;
	}
	else if ((I::GlobalVars->curtime - tState.m_flLastSeen) < 1.2f && !tState.m_vLastPos.IsZero())
	{
		if (!bHasPathTarget || IsPathTargetForward(vOrigin, tState.m_vLastPos, vPathDirection, 0.f))
		{
			vLook = tState.m_vLastPos;
			iTrackedTarget = iPreviousTarget;
			bEnemyLock = true;
		}
	}

	if (!bEnemyLock)
	{
		const bool bAllowSteep = !pLocal->OnSolid() || (!vDest.IsZero() && std::fabs(vDest.z - vOrigin.z) > 48.f) || std::fabs(vVelocity.z) > 120.f;
		const float flBaseDistance = std::clamp(std::max(vPathDelta.Length2D(), flSpeed * 0.7f), 150.f, 520.f);
		const float flTargetHeight = !vDest.IsZero() ? std::clamp(vDest.z - vOrigin.z, -96.f, 112.f) : 0.f;
		const float flEyeHeight = vEye.z - vOrigin.z;
		std::array<Vec3, 7> vCandidates = {
			vDest.IsZero() ? Vec3{} : Vec3(vDest.x, vDest.y, vEye.z + flTargetHeight * 0.35f),
			vOrigin + vPathDirection * std::min(flBaseDistance + 120.f, 620.f) + Vec3(0.f, 0.f, flEyeHeight + flTargetHeight * 0.3f),
			vOrigin + vPathDirection * std::min(flBaseDistance + 240.f, 760.f) + Vec3(0.f, 0.f, flEyeHeight + flTargetHeight * 0.2f),
			!bHasPathTarget && flSpeed > 25.f ? vOrigin + GetFlatDirection(vVelocity, vPathDirection) * std::min(flSpeed * 1.25f, 540.f) + Vec3(0.f, 0.f, flEyeHeight) : Vec3{},
			MakePositionFromYaw(vOrigin, vPathDirection, -12.f, std::min(flBaseDistance + 80.f, 560.f), flEyeHeight + flTargetHeight * 0.15f),
			MakePositionFromYaw(vOrigin, vPathDirection, 12.f, std::min(flBaseDistance + 80.f, 560.f), flEyeHeight + flTargetHeight * 0.15f),
			vOrigin + vPathDirection * 260.f + Vec3(0.f, 0.f, flEyeHeight)
		};

		LegitPathCandidate_t tBestCandidate = {};
		for (const Vec3& vCandidate : vCandidates)
		{
			LegitPathCandidate_t tScored = ScorePathCandidate(pLocal, vEye, vOrigin, vCandidate, vPathDirection, tState.m_vPathTargetGoal, bAllowSteep);
			if (tScored.m_bValid && tScored.m_flScore > tBestCandidate.m_flScore)
				tBestCandidate = tScored;
		}

		if (tBestCandidate.m_bValid)
		{
			const float flTimeSinceSwitch = I::GlobalVars->curtime - tState.m_flPathTargetTime;
			const bool bNoTarget = tState.m_vPathTargetGoal.IsZero();
			const bool bMuchBetter = tBestCandidate.m_flScore > tState.m_flPathTargetScore + 0.85f;
			const bool bStaleTarget = flTimeSinceSwitch > 1.35f && tBestCandidate.m_vPosition.DistToSqr(tState.m_vPathTargetGoal) > 180.f * 180.f;
			const bool bInvalidOld = !bNoTarget && !TracePathTargetVisible(pLocal, vEye, tState.m_vPathTargetGoal, nullptr);
			const bool bOldBehind = !bNoTarget && !IsPathTargetForward(vOrigin, tState.m_vPathTargetGoal, vPathDirection, 0.2f);

			if (bNoTarget || bInvalidOld || bOldBehind || (flTimeSinceSwitch > 0.45f && (bMuchBetter || bStaleTarget)))
			{
				tState.m_vPathTargetGoal = tBestCandidate.m_vPosition;
				tState.m_flPathTargetScore = tBestCandidate.m_flScore;
				tState.m_flPathTargetTime = I::GlobalVars->curtime;
			}
			else
				tState.m_flPathTargetScore = Math::Lerp(tState.m_flPathTargetScore, tBestCandidate.m_flScore, 0.08f);

			const float flTargetBlend = std::clamp(I::GlobalVars->interval_per_tick * (flSpeed > 120.f ? 4.6f : 3.2f), 0.02f, 0.16f);
			if (tState.m_vPathTarget.IsZero())
				tState.m_vPathTarget = tState.m_vPathTargetGoal;
			else if (!IsPathTargetForward(vOrigin, tState.m_vPathTarget, vPathDirection, 0.05f))
				tState.m_vPathTarget = tState.m_vPathTargetGoal;
			else
				tState.m_vPathTarget = tState.m_vPathTarget.Lerp(tState.m_vPathTargetGoal, flTargetBlend);
			vLook = tState.m_vPathTarget;
		}
		else
			vLook = vOrigin + vPathDirection * std::max(flBaseDistance, 260.f) + Vec3(0.f, 0.f, flEyeHeight + flTargetHeight * 0.25f);
	}

	tState.m_iLastTarget = iTrackedTarget;

	Vec3 vFocus;
	if (bEnemyLock)
	{
		vFocus = vLook;
	}
	else
	{
		const float flHeightDelta = std::clamp(vLook.z - vEye.z, -72.f, 96.f);
		const float flPitchFactor = flHeightDelta >= 0.f ? 0.55f : 0.22f;
		vFocus = { vLook.x, vLook.y, vEye.z + flHeightDelta * flPitchFactor + 6.f };
	}

	Vec3 vDesired = Math::CalcAngle(vEye, vFocus);
	Math::ClampAngles(vDesired);

	const float flTargetDelta = tState.m_vLastTarget.IsZero() ? FLT_MAX : tState.m_vLastTarget.DistToSqr(vFocus);
	const float flDesiredDelta = tState.m_bInitialized ? Math::CalcFov(tState.m_vAnchor, vDesired) : FLT_MAX;
	if (!tState.m_bInitialized || !std::isfinite(flTargetDelta) || !std::isfinite(flDesiredDelta) || flDesiredDelta > 120.f || (!bEnemyLock && flTargetDelta > powf(1200.f, 2)))
	{
		tState.m_bInitialized = true;
		tState.m_vAnchor = vDesired;
		tState.m_vOffset = {};
		tState.m_vOffsetGoal = {};
		tState.m_vLastTarget = vFocus;
		tState.m_vPathTarget = bEnemyLock ? Vec3{} : vLook;
		tState.m_vPathTargetGoal = bEnemyLock ? Vec3{} : vLook;
		tState.m_vGlanceCurrent = {};
		tState.m_vGlanceGoal = {};
		tState.m_flPathTargetTime = I::GlobalVars->curtime;
		tState.m_flPathTargetScore = 0.f;
		tState.m_flNextOffset = SDK::RandomFloat(0.6f, 1.8f);
		tState.m_flAcquireDuration = SDK::RandomFloat(0.09f, 0.18f);
		tState.m_flEnemyBlend = bEnemyLock ? 1.f : 0.f;
		tState.m_flPhase = SDK::RandomFloat(0.f, 6.2831853f);
		tState.m_flNextGlance = SDK::RandomFloat(1.4f, 3.0f);
		tState.m_flGlanceDuration = SDK::RandomFloat(0.3f, 0.55f);
		tState.m_bGlancing = false;
		tState.m_tOffsetTimer.Update();
		tState.m_tAcquireTimer.Update();
		tState.m_tGlanceTimer.Update();
		tState.m_tGlanceCooldown.Update();

		tState.m_flNextScan = SDK::RandomFloat(0.5f, 1.5f);
		tState.m_tScanTimer.Update();
		tState.m_flPitchVelocity = 0.f;
		tState.m_flYawVelocity = 0.f;
	}
	else
		tState.m_vLastTarget = vFocus;

	if (bEnemyLock && iTrackedTarget != -1 && iTrackedTarget != iPreviousTarget)
	{
		tState.m_flAcquireDuration = SDK::RandomFloat(0.09f, 0.18f);
		tState.m_tAcquireTimer.Update();
		tState.m_flEnemyBlend = std::min(tState.m_flEnemyBlend, 0.2f);
	}

	const bool bAcquireComplete = !bEnemyLock || tState.m_tAcquireTimer.Run(tState.m_flAcquireDuration);
	const float flEnemyBlendGoal = bEnemyLock ? (bAcquireComplete ? 1.f : 0.42f) : 0.f;
	tState.m_flEnemyBlend = Math::Lerp(tState.m_flEnemyBlend, flEnemyBlendGoal, bEnemyLock ? 0.18f : 0.08f);

	float flAnchorDelta = Math::CalcFov(tState.m_vAnchor, vDesired);
	if (!std::isfinite(flAnchorDelta) || flAnchorDelta > 120.f)
		tState.m_vAnchor = vDesired;
	else
	{
		float flAnchorBlend = std::clamp(flAnchorDelta / 90.f, 0.05f, 0.3f);
		if (bEnemyLock)
		{
			float flProgressive = std::pow(std::clamp(flAnchorDelta / 30.f, 0.f, 1.f), 1.5f);
			flAnchorBlend = std::clamp((0.08f + flProgressive * 0.42f) * std::clamp(tState.m_flEnemyBlend, 0.2f, 1.f), 0.04f, 0.5f);
		}
		tState.m_vAnchor = tState.m_vAnchor.LerpAngle(vDesired, flAnchorBlend);
	}

	const float flVelocity2D = pLocal->m_vecVelocity().Length2D();
	if (tState.m_tOffsetTimer.Run(tState.m_flNextOffset))
	{
		float flYawScale = std::clamp(flVelocity2D / 220.f, 0.3f, 0.95f);
		float flPitchScale = std::clamp(flVelocity2D / 320.f, 0.18f, 0.75f);
		if (!bEnemyLock)
		{
			tState.m_vOffsetGoal.y = SDK::RandomFloat(-28.f, 28.f) * flYawScale;
			tState.m_vOffsetGoal.x = SDK::RandomFloat(-3.f, 4.f) * flPitchScale;
		}
		else
			tState.m_vOffsetGoal = {};

		tState.m_flNextOffset = SDK::RandomFloat(0.65f, 1.95f);
	}

	float flOffsetBlend = bEnemyLock ? Math::Lerp(0.16f, 0.08f, std::clamp(tState.m_flEnemyBlend, 0.f, 1.f)) : 0.1f;
	tState.m_vOffset = tState.m_vOffset.LerpAngle(tState.m_vOffsetGoal, flOffsetBlend);

	if (tState.m_bGlancing)
	{
		if (bEnemyLock || tState.m_tGlanceTimer.Run(tState.m_flGlanceDuration))
		{
			tState.m_bGlancing = false;
			tState.m_vGlanceGoal = {};
			tState.m_flNextGlance = SDK::RandomFloat(1.6f, 3.4f);
			tState.m_tGlanceCooldown.Update();
		}
	}
	else if (!bEnemyLock && tState.m_tGlanceCooldown.Run(tState.m_flNextGlance))
	{
		tState.m_bGlancing = true;
		tState.m_flGlanceDuration = SDK::RandomFloat(0.28f, 0.52f);
		float flYawGlance = SDK::RandomFloat(10.f, 24.f) * (SDK::RandomInt(0, 1) == 0 ? -1.f : 1.f);
		tState.m_vGlanceGoal = { SDK::RandomFloat(-3.5f, 4.5f), flYawGlance, 0.f };
		tState.m_tGlanceTimer.Update();
	}

	tState.m_vGlanceCurrent = tState.m_vGlanceCurrent.LerpAngle(tState.m_vGlanceGoal, tState.m_bGlancing ? 0.12f : 0.08f);

	float flPhaseSpeed = std::clamp(flVelocity2D / 240.f, 0.25f, 1.0f);
	tState.m_flPhase += I::GlobalVars->interval_per_tick * (0.9f + flPhaseSpeed);
	if (tState.m_flPhase > 8192.f)
		tState.m_flPhase = std::fmod(tState.m_flPhase, 8192.f);

	float flMicroScale = std::clamp(flVelocity2D / 320.f, 0.12f, 0.4f);
	Vec3 vMicro = {
		std::sin(tState.m_flPhase * 0.92f) * 0.6f * flMicroScale,
		std::sin(tState.m_flPhase * 0.55f + 1.4f) * 0.8f * flMicroScale,
		0.f
	};

	if (bEnemyLock)
	{
		float flErrorScale = Math::Lerp(0.55f, 0.25f, std::clamp(tState.m_flEnemyBlend, 0.f, 1.f));
		float flDeltaX = SDK::RandomFloat(-1.f, 1.f) * flErrorScale;
		float flDeltaY = SDK::RandomFloat(-1.f, 1.f) * flErrorScale;
		tState.m_flErrorVelocityX += (flDeltaX - tState.m_flErrorX) * 0.12f;
		tState.m_flErrorVelocityY += (flDeltaY - tState.m_flErrorY) * 0.12f;
		tState.m_flErrorVelocityX *= 0.82f;
		tState.m_flErrorVelocityY *= 0.82f;
		tState.m_flErrorX += tState.m_flErrorVelocityX;
		tState.m_flErrorY += tState.m_flErrorVelocityY;
	}
	else
	{
		tState.m_flErrorX = Math::Lerp(tState.m_flErrorX, 0.f, 0.1f);
		tState.m_flErrorY = Math::Lerp(tState.m_flErrorY, 0.f, 0.1f);
	}

	Vec3 vGoal = tState.m_vAnchor + tState.m_vOffset + tState.m_vGlanceCurrent + vMicro + Vec3(tState.m_flErrorX, tState.m_flErrorY, 0.f);
	Math::ClampAngles(vGoal);
	if (bEnemyLock)
		vGoal.x = std::clamp(vGoal.x, -89.f, 89.f);
	else
		vGoal.x = std::clamp(vGoal.x, -15.f, 25.f);

	float flSpeedVal = std::max(1.f, static_cast<float>(Vars::Misc::Movement::BotUtils::LookAtPathSpeed.Value));
	Vec3 vWish = ClampAngleSpeed(m_vLastAngles, vGoal, tState.m_flPitchVelocity, tState.m_flYawVelocity, flSpeedVal, bEnemyLock);

	if (Vars::Misc::Movement::BotUtils::LookAtPathDebug.Value)
	{
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(10, 0, 0), vLook + Vec3(10, 0, 0)), I::GlobalVars->curtime + 0.1f, Color_t{ 255, 0, 0, 255 }, false);
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(0, 10, 0), vLook + Vec3(0, 10, 0)), I::GlobalVars->curtime + 0.1f, Color_t{ 0, 255, 0, 255 }, false);
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(0, 0, 10), vLook + Vec3(0, 0, 10)), I::GlobalVars->curtime + 0.1f, Color_t{ 0, 0, 255, 255 }, false);
	}

	pCmd->viewangles = vWish;
	if (!bSilent)
		I::EngineClient->SetViewAngles(vWish);

	m_vLastAngles = vWish;
}

void CBotUtils::InvalidateLLAP()
{
	m_tLLAP = {};
}
