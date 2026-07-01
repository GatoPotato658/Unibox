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
		vLook = tState.m_vLastPos;
		iTrackedTarget = iPreviousTarget;
		bEnemyLock = true;
	}
	else
	{
		const Vec3 vVelocity = pLocal->m_vecVelocity();
		const float flSpeed = vVelocity.Length2D();
		if (flSpeed > 25.f)
		{
			Vec3 vForward = vVelocity;
			vForward.Normalize();
			vLook = vEye + (vForward * 500.f);

			CGameTrace trace;
			CTraceFilterHitscan filter(pLocal);
			SDK::Trace(vEye, vLook, MASK_SHOT, &filter, &trace);

			if (trace.fraction < 0.25f)
			{
				float flBestDistInner = trace.fraction * 500.f;
				Vec3 vBestForward = vForward;

				for (float flOffset : { -15.f, 15.f, -30.f, 30.f, -45.f, 45.f, -60.f, 60.f, -75.f, 75.f })
				{
					Vec3 vTestAngles = Math::CalcAngle(vEye, vLook);
					vTestAngles.y += flOffset;
					vTestAngles.x = SDK::RandomFloat(-5.f, 10.f);
					Vec3 vTestForward;
					Math::AngleVectors(vTestAngles, &vTestForward);

					float flForwardDot = std::clamp(vForward.Dot(vTestForward), -1.f, 1.f);
					if (flForwardDot < 0.1f)
						continue;

					SDK::Trace(vEye, vEye + vTestForward * 500.f, MASK_SHOT, &filter, &trace);
					float flTraceScore = trace.fraction * 500.f * Math::RemapVal(flForwardDot, 0.1f, 1.f, 0.55f, 1.f);
					if (flTraceScore > flBestDistInner)
					{
						flBestDistInner = flTraceScore;
						vBestForward = vTestForward;
					}
				}
				vForward = vBestForward;
				vLook = vEye + (vForward * 500.f);
			}

			float flSweep = std::sin(I::GlobalVars->curtime * 1.5f) * 15.f;
			Vec3 vAngles = Math::CalcAngle(vEye, vLook);
			vAngles.y += flSweep;
			Math::AngleVectors(vAngles, &vForward);
			vLook = vEye + (vForward * 500.f);
		}
		else if (vLook.IsZero())
		{
			Vec3 vForward;
			Math::AngleVectors(I::EngineClient->GetViewAngles(), &vForward, nullptr, nullptr);
			vLook = vEye + vForward * 64.f;
		}
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
		tState.m_vGlanceCurrent = {};
		tState.m_vGlanceGoal = {};
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

	if (!bEnemyLock && !tState.m_bGlancing && flVelocity2D > 50.f && tState.m_tScanTimer.Run(tState.m_flNextScan))
	{
		tState.m_flNextScan = SDK::RandomFloat(0.5f, 1.5f);

		Vec3 vMoveDir = pLocal->m_vecVelocity();
		vMoveDir.Normalize();
		Vec3 vMoveAngles = Math::CalcAngle(Vec3(), vMoveDir);

		float flBestTraceScore = 0.f;
		float flBestTraceDist = 0.f;
		Vec3 vBestScanDir = {};

		for (float flYawOffset = -90.f; flYawOffset <= 90.f; flYawOffset += 15.f)
		{
			Vec3 vScanAngles = vMoveAngles;
			vScanAngles.y += flYawOffset;
			vScanAngles.x = SDK::RandomFloat(-5.f, 15.f);

			Vec3 vForward;
			Math::AngleVectors(vScanAngles, &vForward);

			float flForwardDot = std::clamp(vMoveDir.Dot(vForward), -1.f, 1.f);
			if (flForwardDot < 0.2f)
				continue;

			CGameTrace trace;
			CTraceFilterHitscan filter(pLocal);
			SDK::Trace(vEye, vEye + vForward * 1000.f, MASK_SHOT, &filter, &trace);

			if (Vars::Misc::Movement::BotUtils::LookAtPathDebug.Value)
				G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEye, trace.endpos), I::GlobalVars->curtime + 1.f, Color_t{ 255, 255, 255, 100 }, false);

			float flTraceDist = trace.fraction * 1000.f;
			float flTraceScore = flTraceDist * Math::RemapVal(flForwardDot, 0.2f, 1.f, 0.5f, 1.f);
			if (flTraceScore > flBestTraceScore)
			{
				flBestTraceScore = flTraceScore;
				flBestTraceDist = flTraceDist;
				vBestScanDir = vForward;
			}
		}

		if (Vars::Misc::Movement::BotUtils::LookAtPathDebug.Value && !vBestScanDir.IsZero())
			G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEye, vEye + vBestScanDir * flBestTraceDist), I::GlobalVars->curtime + 1.f, Color_t{ 0, 255, 0, 255 }, false);

		if (flBestTraceDist > 400.f)
		{
			tState.m_vGlanceGoal = Math::CalcAngle(vEye, vEye + vBestScanDir * 500.f);
			tState.m_bGlancing = true;
			tState.m_flGlanceDuration = SDK::RandomFloat(1.2f, 2.0f);
			tState.m_tGlanceTimer.Update();
		}
	}

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
	Vec3 vWish = vGoal;
	DoSlowAim(vWish, flSpeedVal, m_vLastAngles);

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
