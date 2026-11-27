#include "Resolver.h"

#include "../Backtrack/Backtrack.h"
#include "../Players/PlayerUtils.h"
#include "../Ticks/Ticks.h"
#include "../Output/Output.h"

static CTFPlayer* GetPlayerForUserID(int iUserID)
{
	int iEntIndex = I::EngineClient->GetPlayerForUserID(iUserID);
	if (iEntIndex <= 0)
		return nullptr;

	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIndex);
	return pEntity ? pEntity->As<CTFPlayer>() : nullptr;
}

ResolverTrack_t* CResolver::GetTrackForEntity(int iEntIndex, int iUserID)
{
	if (iEntIndex <= 0 || iEntIndex >= kMaxPlayers)
		return nullptr;

	auto& tTrack = m_aTracks[iEntIndex];
	if (tTrack.m_iUserID != iUserID)
		tTrack.Reset(iUserID);
	return &tTrack;
}

ResolverTrack_t* CResolver::FindTrackByUserID(int iUserID)
{
	int iEntIndex = I::EngineClient->GetPlayerForUserID(iUserID);
	if (iEntIndex > 0 && iEntIndex < kMaxPlayers)
		return GetTrackForEntity(iEntIndex, iUserID);

	for (int i = 1; i < kMaxPlayers; i++)
	{
		if (m_aTracks[i].m_iUserID == iUserID)
			return &m_aTracks[i];
	}
	return nullptr;
}

void CResolver::Reset()
{
	for (auto& tTrack : m_aTracks)
		tTrack.Reset();
	m_aSniperDots.fill(Vec3{});
	m_aHasSniperDot.fill(false);
	for (auto& tShot : m_aPendingShots)
		tShot = PendingShot_t{};
	m_iPendingHead = 0;
}

void CResolver::StoreSniperDots(CTFPlayerResource* pResource)
{
	(void)pResource;
	m_aHasSniperDot.fill(false);
	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::SniperDots))
	{
		if (auto pOwner = pEntity->m_hOwnerEntity().Get())
		{
			const int iEntIndex = pOwner->entindex();
			if (iEntIndex > 0 && iEntIndex < kMaxPlayers)
			{
				m_aSniperDots[iEntIndex] = pEntity->m_vecOrigin();
				m_aHasSniperDot[iEntIndex] = true;
			}
		}
	}
}

std::optional<float> CResolver::GetPitchForSniperDot(CTFPlayer* pEntity, int iEntIndex)
{
	if (iEntIndex > 0 && iEntIndex < kMaxPlayers && m_aHasSniperDot[iEntIndex])
	{
		Vec3 vOrigin = m_aSniperDots[iEntIndex];
		Vec3 vEyeOrigin = pEntity->m_vecOrigin() + pEntity->GetViewOffset();
		Vec3 vAngles = Math::VectorAngles(vOrigin - vEyeOrigin);
		return vAngles.x;
	}

	return std::nullopt;
}

void CResolver::FrameStageNotify()
{
	if (!Vars::Resolver::Enabled.Value || !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo())
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	auto pLocal = H::Entities.GetLocal();
	StoreSniperDots(pResource);

	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		const int iEntIndex = pPlayer->entindex();
		if (iEntIndex == I::EngineClient->GetLocalPlayer() ||
			pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
			continue;

		const float flDeltaTime = H::Entities.GetDeltaTime(iEntIndex);
		if (!flDeltaTime)
			continue;

		int iUserID = pResource->m_iUserID(iEntIndex);
		auto* pTrack = GetTrackForEntity(iEntIndex, iUserID);
		if (!pTrack)
			continue;

		if (auto flDotPitch = GetPitchForSniperDot(pPlayer, iEntIndex))
		{
			pTrack->m_bHasSniperDot = true;
			pTrack->m_flSniperDotPitch = *flDotPitch;
		}
		else
		{
			pTrack->m_bHasSniperDot = false;
		}

		ResolverSnapshot_t tSnap = {};
		tSnap.m_flSimTime = pPlayer->m_flSimulationTime();
		tSnap.m_flSentYaw = pPlayer->m_angEyeAnglesY();
		tSnap.m_flSentPitch = pPlayer->m_angEyeAnglesX();
		tSnap.m_flViewToLocalYaw = pLocal ? Math::CalcAngle(pPlayer->m_vecOrigin(), pLocal->m_vecOrigin()).y : tSnap.m_flSentYaw;

		const Vec3 vVel = pPlayer->m_vecVelocity();
		tSnap.m_flSpeed2D = vVel.Length2D();
		tSnap.m_flMoveYaw = (tSnap.m_flSpeed2D > 1.f) ? Math::VectorAngles(vVel).y : tSnap.m_flSentYaw;
		tSnap.m_iChokedTicks = std::max(0, TIME_TO_TICKS(flDeltaTime) - 1);
		tSnap.m_bOnGround = pPlayer->IsOnGround();

		pTrack->PushSnapshot(tSnap);
		if (pTrack->m_bHasSniperDot)
			pTrack->m_bPitch = true;
	}
}

void CResolver::CreateMove()
{
	const float flCurTime = I::GlobalVars->curtime;
	for (auto& tShot : m_aPendingShots)
	{
		if (!tShot.m_bActive || tShot.m_flExpireTime > flCurTime)
			continue;

		tShot.m_bActive = false;
		if (tShot.m_bImpactOccluded)
			continue;

		if (tShot.m_iEntIndex <= 0 || tShot.m_iEntIndex >= kMaxPlayers)
			continue;

		auto* pTrack = &m_aTracks[tShot.m_iEntIndex];
		if (pTrack->m_iUserID != tShot.m_iUserID)
			continue;

		const bool bAutoYaw = pTrack->m_bAutoSetYaw && Vars::Resolver::AutoResolveYawAmount.Value != 0.f;
		const bool bAutoPitch = pTrack->m_bAutoSetPitch && Vars::Resolver::AutoResolvePitchAmount.Value != 0.f;
		if (!bAutoYaw && !bAutoPitch)
			continue;

		const float flOldYaw = pTrack->m_flYaw;
		const float flOldPitch = pTrack->m_flPitch;
		const int iOldYawHyp = pTrack->m_iActiveYawHyp;
		const int iOldPitchHyp = pTrack->m_iActivePitchHyp;
		const bool bStepPitch = bAutoPitch && pTrack->m_bPitch && !pTrack->m_bHasSniperDot;

		pTrack->OnShotOutcome(
			false,
			tShot.m_bHeadshotWeapon,
			false,
			tShot.m_iYawHyp,
			tShot.m_iPitchHyp,
			bAutoYaw ? Vars::Resolver::AutoResolveYawAmount.Value : 0.f,
			bAutoPitch ? Vars::Resolver::AutoResolvePitchAmount.Value : 0.f,
			bStepPitch
		);

		auto pTarget = GetPlayerForUserID(tShot.m_iUserID);
		if (pTarget)
			F::Backtrack.ResolverUpdate(pTarget);

		if (pTrack->m_flYaw != flOldYaw || pTrack->m_iActiveYawHyp != iOldYawHyp)
			F::Output.ReportResolver(tShot.m_iEntIndex, "Cycling", "yaw", pTrack->m_flYaw);
		if (pTrack->m_flPitch != flOldPitch || pTrack->m_iActivePitchHyp != iOldPitchHyp)
		{
			const float flLoggedPitch = pTarget ? pTrack->EvaluatePitch(pTarget->m_angEyeAnglesX()) : pTrack->m_flPitch;
			F::Output.ReportResolver(tShot.m_iEntIndex, "Cycling", "pitch", flLoggedPitch);
		}
	}

	if (!Vars::Resolver::Enabled.Value)
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	auto fGetPlayerClosestToFOV = [&]()
	{
		CTFPlayer* pClosest = nullptr;
		float flMinFOV = std::numeric_limits<float>::max();

		const Vec3 vLocalPos = F::Ticks.GetShootPos();
		const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

		for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
		{
			if (pEntity->entindex() == I::EngineClient->GetLocalPlayer() || pEntity->IsDormant())
				continue;

			Vec3 vCurPos = pEntity->GetCenter();
			Vec3 vCurAngleTo = Math::CalcAngle(vLocalPos, vCurPos);
			float flCurFOVTo = Math::CalcFov(vLocalAngles, vCurAngleTo);

			if (flCurFOVTo < flMinFOV)
			{
				pClosest = pEntity->As<CTFPlayer>();
				flMinFOV = flCurFOVTo;
			}
		}

		return pClosest;
	};

	if (Vars::Resolver::CycleYaw.Value)
	{
		if (SDK::PlatFloatTime() > m_flLastYawCycle + 0.5f)
		{
			if (auto pTarget = fGetPlayerClosestToFOV())
			{
				int iUserID = pResource->m_iUserID(pTarget->entindex());
				if (auto* pTrack = GetTrackForEntity(pTarget->entindex(), iUserID))
				{
					pTrack->m_flYaw = Math::NormalizeAngle(pTrack->m_flYaw + Vars::Resolver::CycleYaw.Value);
					pTrack->m_bManualYaw = true;
					pTrack->m_bYaw = true;

					F::Backtrack.ResolverUpdate(pTarget);
					F::Output.ReportResolver(pTarget->entindex(), "Cycling", "yaw", pTrack->m_flYaw);
				}
			}

			m_flLastYawCycle = static_cast<float>(SDK::PlatFloatTime());
		}
	}
	else
		m_flLastYawCycle = 0.f;

	if (Vars::Resolver::CyclePitch.Value)
	{
		if (SDK::PlatFloatTime() > m_flLastPitchCycle + 0.5f)
		{
			if (auto pTarget = fGetPlayerClosestToFOV())
			{
				const int iEntIndex = pTarget->entindex();
				int iUserID = pResource->m_iUserID(iEntIndex);
				if (auto* pTrack = GetTrackForEntity(iEntIndex, iUserID))
				{
					if (!m_aHasSniperDot[iEntIndex])
					{
						if (fabsf(pTarget->m_angEyeAnglesX()) < 88.5f)
							F::Output.ReportResolver("Target not using out of bounds pitch");

						pTrack->m_flPitch = Math::ClampNormalizeAngle(pTrack->m_flPitch + Vars::Resolver::CyclePitch.Value, 90.f);
						pTrack->m_bManualPitch = true;
						pTrack->m_bPitch = true;

						F::Backtrack.ResolverUpdate(pTarget);
						F::Output.ReportResolver(iEntIndex, "Cycling", "pitch", pTrack->m_flPitch);
					}
					else
						F::Output.ReportResolver("Target pitch overridden by sniper dot");
				}
			}

			m_flLastPitchCycle = static_cast<float>(SDK::PlatFloatTime());
		}
	}
	else
		m_flLastPitchCycle = 0.f;

	if (Vars::Resolver::CycleMinwalk.Value)
	{
		if (SDK::PlatFloatTime() > m_flLastMinwalkCycle + 0.5f)
		{
			if (auto pTarget = fGetPlayerClosestToFOV())
			{
				int iUserID = pResource->m_iUserID(pTarget->entindex());
				if (auto* pTrack = GetTrackForEntity(pTarget->entindex(), iUserID))
				{
					pTrack->m_bMinwalk = !pTrack->m_bMinwalk;

					F::Backtrack.ResolverUpdate(pTarget);
					F::Output.ReportResolver(pTarget->entindex(), "Cycling", "minwalk", pTrack->m_bMinwalk);
				}
			}

			m_flLastMinwalkCycle = static_cast<float>(SDK::PlatFloatTime());
		}
	}
	else
		m_flLastMinwalkCycle = 0.f;

	if (Vars::Resolver::CycleView.Value)
	{
		if (SDK::PlatFloatTime() > m_flLastViewCycle + 0.5f)
		{
			if (auto pTarget = fGetPlayerClosestToFOV())
			{
				int iUserID = pResource->m_iUserID(pTarget->entindex());
				if (auto* pTrack = GetTrackForEntity(pTarget->entindex(), iUserID))
				{
					pTrack->m_bView = !pTrack->m_bView;

					F::Backtrack.ResolverUpdate(pTarget);
					F::Output.ReportResolver(pTarget->entindex(), "Cycling", "view", pTrack->m_bView ? "view to local" : "static");
				}
			}

			m_flLastViewCycle = static_cast<float>(SDK::PlatFloatTime());
		}
	}
	else
		m_flLastViewCycle = 0.f;
}

void CResolver::HitscanRan(CTFPlayer* pLocal, CTFPlayer* pTarget, CTFWeaponBase* pWeapon, int nHitbox, const Vec3& vShootPos, const Vec3& vTargetPos)
{
	if (!Vars::Resolver::Enabled.Value || !Vars::Resolver::AutoResolve.Value
		|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
		|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::SmoothVelocity
		|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::LegitForBots
		|| pLocal->m_iTeamNum() == pTarget->m_iTeamNum())
		return;

	if (Vars::Resolver::AutoResolveCheatersOnly.Value && !F::PlayerUtils.HasTag(pTarget->entindex(), CHEATER_TAG))
		return;

	if (Vars::Resolver::AutoResolveHeadshotOnly.Value && (nHitbox != HITBOX_HEAD || !G::CanHeadshot))
		return;

	if (pWeapon->GetWeaponSpread())
	{
		const float flTimeSinceLastShot = I::GlobalVars->curtime - pWeapon->m_flLastFireTime();
		if (flTimeSinceLastShot <= (pWeapon->GetBulletsPerShot() > 1 ? 0.25f : 1.25f))
			return;
	}

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	const int iEntIndex = pTarget->entindex();
	const int iUserID = pResource->m_iUserID(iEntIndex);
	auto* pTrack = GetTrackForEntity(iEntIndex, iUserID);
	if (!pTrack)
		return;

	PendingShot_t& tShot = m_aPendingShots[m_iPendingHead];
	m_iPendingHead = (m_iPendingHead + 1) % kMaxPendingShots;

	tShot = PendingShot_t{};
	tShot.m_iUserID = iUserID;
	tShot.m_iEntIndex = iEntIndex;
	tShot.m_flFireTime = I::GlobalVars->curtime;
	tShot.m_flExpireTime = I::GlobalVars->curtime + F::Backtrack.GetReal(MAX_FLOWS, false) * 1.5f + 0.1f;
	tShot.m_nHitbox = nHitbox;
	tShot.m_iYawHyp = pTrack->m_iActiveYawHyp;
	tShot.m_iPitchHyp = pTrack->m_iActivePitchHyp;
	tShot.m_vShootPos = vShootPos;
	tShot.m_vTargetPos = vTargetPos;
	tShot.m_bActive = true;

	if (nHitbox == HITBOX_HEAD && G::CanHeadshot)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			tShot.m_bHeadshotWeapon = true;
			break;
		}
	}
}

bool CResolver::HasPendingHitscan() const
{
	if (!Vars::Resolver::Enabled.Value)
		return false;

	for (const auto& tShot : m_aPendingShots)
	{
		if (tShot.m_bActive && !tShot.m_bImpactSeen && tShot.m_vTargetPos.LengthSqr() > 0.f)
			return true;
	}
	return false;
}

void CResolver::BulletImpact(const Vec3& vImpact)
{
	if (!Vars::Resolver::Enabled.Value)
		return;

	const float flCurTime = I::GlobalVars->curtime;

	PendingShot_t* pBestShot = nullptr;
	float flBestDelta = std::numeric_limits<float>::max();

	for (auto& tShot : m_aPendingShots)
	{
		if (!tShot.m_bActive || tShot.m_bImpactSeen)
			continue;

		if (tShot.m_vTargetPos.LengthSqr() <= 0.f)
			continue;

		const float flDelta = fabsf(flCurTime - tShot.m_flFireTime);
		if (flDelta < flBestDelta)
		{
			flBestDelta = flDelta;
			pBestShot = &tShot;
		}
	}

	if (!pBestShot)
		return;

	const float flImpactDist = pBestShot->m_vShootPos.DistTo(vImpact);
	const float flTargetDist = pBestShot->m_vShootPos.DistTo(pBestShot->m_vTargetPos);
	pBestShot->m_bImpactSeen = true;
	if (flImpactDist + 18.f < flTargetDist)
		pBestShot->m_bImpactOccluded = true;
}

void CResolver::PlayerHurt(IGameEvent* pEvent)
{
	if (!pEvent)
		return;

	if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker")) != I::EngineClient->GetLocalPlayer())
		return;

	const int iVictimUserID = pEvent->GetInt("userid");
	PendingShot_t* pBestShot = nullptr;
	float flBestTime = -1.f;

	for (auto& tShot : m_aPendingShots)
	{
		if (!tShot.m_bActive || tShot.m_iUserID != iVictimUserID)
			continue;

		if (tShot.m_flFireTime > flBestTime)
		{
			flBestTime = tShot.m_flFireTime;
			pBestShot = &tShot;
		}
	}

	if (!pBestShot)
		return;

	const int iDamageCustom = pEvent->GetInt("damagecustom");
	const bool bWasHeadshot = iDamageCustom == TF_DMG_CUSTOM_HEADSHOT
		|| iDamageCustom == TF_DMG_CUSTOM_HEADSHOT_DECAPITATION;

	if (pBestShot->m_iEntIndex > 0 && pBestShot->m_iEntIndex < kMaxPlayers && m_aTracks[pBestShot->m_iEntIndex].m_iUserID == pBestShot->m_iUserID)
	{
		auto* pTrack = &m_aTracks[pBestShot->m_iEntIndex];
		const bool bAutoYaw = pTrack->m_bAutoSetYaw && Vars::Resolver::AutoResolveYawAmount.Value != 0.f;
		const bool bAutoPitch = pTrack->m_bAutoSetPitch && Vars::Resolver::AutoResolvePitchAmount.Value != 0.f;
		const bool bStepPitch = bAutoPitch && pTrack->m_bPitch && !pTrack->m_bHasSniperDot;

		pTrack->OnShotOutcome(
			true,
			pBestShot->m_bHeadshotWeapon,
			bWasHeadshot,
			pBestShot->m_iYawHyp,
			pBestShot->m_iPitchHyp,
			bAutoYaw ? Vars::Resolver::AutoResolveYawAmount.Value : 0.f,
			bAutoPitch ? Vars::Resolver::AutoResolvePitchAmount.Value : 0.f,
			bStepPitch
		);

		if (pBestShot->m_bHeadshotWeapon && !bWasHeadshot)
		{
			if (auto pTarget = GetPlayerForUserID(pBestShot->m_iUserID))
				F::Backtrack.ResolverUpdate(pTarget);
		}
	}

	pBestShot->m_bActive = false;
}

void CResolver::SetYaw(int iUserID, float flValue, bool bAuto)
{
	auto* pTrack = FindTrackByUserID(iUserID);
	if (!pTrack)
		return;

	if (bAuto)
	{
		pTrack->m_bAutoSetYaw = true;
		pTrack->m_bManualYaw = false;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "yaw", "auto");
	}
	else
	{
		pTrack->m_flYaw = flValue;
		pTrack->m_bAutoSetYaw = false;
		pTrack->m_bManualYaw = true;
		pTrack->m_bYaw = true;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "yaw", flValue);
	}

	F::Backtrack.ResolverUpdate(GetPlayerForUserID(iUserID));
}

void CResolver::SetPitch(int iUserID, float flValue, bool bInverse, bool bAuto)
{
	auto* pTrack = FindTrackByUserID(iUserID);
	if (!pTrack)
		return;

	if (bAuto)
	{
		pTrack->m_bInversePitch = false;
		pTrack->m_bAutoSetPitch = true;
		pTrack->m_bManualPitch = false;
		pTrack->m_bAutoPitchStepped = false;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "pitch", "auto");
	}
	else if (bInverse)
	{
		pTrack->m_bInversePitch = true;
		pTrack->m_bAutoSetPitch = false;
		pTrack->m_bManualPitch = false;
		pTrack->m_bAutoPitchStepped = false;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "pitch", "inverse");
	}
	else
	{
		auto pPlayer = GetPlayerForUserID(iUserID);
		if (pPlayer && fabsf(pPlayer->m_angEyeAnglesX()) < 88.5f)
			F::Output.ReportResolver("Target not using out of bounds pitch");

		pTrack->m_flPitch = flValue;
		pTrack->m_bInversePitch = false;
		pTrack->m_bAutoSetPitch = false;
		pTrack->m_bManualPitch = true;
		pTrack->m_bAutoPitchStepped = false;
		pTrack->m_bPitch = true;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "pitch", flValue);
	}

	F::Backtrack.ResolverUpdate(GetPlayerForUserID(iUserID));
}

void CResolver::SetMinwalk(int iUserID, bool bValue)
{
	auto* pTrack = FindTrackByUserID(iUserID);
	if (!pTrack)
		return;

	pTrack->m_bMinwalk = bValue;

	F::Backtrack.ResolverUpdate(GetPlayerForUserID(iUserID));
	F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "minwalk", bValue);
}

void CResolver::SetView(int iUserID, bool bValue)
{
	auto* pTrack = FindTrackByUserID(iUserID);
	if (!pTrack)
		return;

	pTrack->m_bView = bValue;

	F::Backtrack.ResolverUpdate(GetPlayerForUserID(iUserID));
	F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "view", bValue ? "view to local" : "static");
}

bool CResolver::GetAngles(CTFPlayer* pPlayer, float* pYaw, float* pPitch, bool* pMinwalk, bool bFake)
{
	if (!Vars::Resolver::Enabled.Value)
		return false;

	if (!pPlayer || pPlayer->entindex() == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
		return false;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return false;

	const int iEntIndex = pPlayer->entindex();
	const int iUserID = pResource->m_iUserID(iEntIndex);
	auto* pTrack = GetTrackForEntity(iEntIndex, iUserID);
	if (!pTrack)
		return false;

	const bool bYaw = pTrack->m_bYaw;
	const bool bPitch = pTrack->m_bPitch;

	if (pYaw)
	{
		const float flSentYaw = pPlayer->m_angEyeAnglesY();
		*pYaw = flSentYaw;
		if (bYaw && !bFake)
		{
			auto pLocal = H::Entities.GetLocal();
			const float flViewToLocalYaw = pLocal ? Math::CalcAngle(pPlayer->m_vecOrigin(), pLocal->m_vecOrigin()).y : flSentYaw;
			const Vec3 vVel = pPlayer->m_vecVelocity();
			const float flSpeed2D = vVel.Length2D();
			const float flMoveYaw = (flSpeed2D > 1.f) ? Math::VectorAngles(vVel).y : flSentYaw;
			*pYaw = pTrack->EvaluateYaw(flSentYaw, flViewToLocalYaw, flMoveYaw, flSpeed2D, pPlayer->IsOnGround());
		}
	}

	if (pPitch)
	{
		const float flSentPitch = pPlayer->m_angEyeAnglesX();
		*pPitch = flSentPitch;
		if (bPitch)
			*pPitch = pTrack->EvaluatePitch(flSentPitch);
	}

	if (pMinwalk)
		*pMinwalk = pTrack->m_bMinwalk;

	return bYaw || bPitch;
}
