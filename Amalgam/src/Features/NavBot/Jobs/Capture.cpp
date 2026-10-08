#include "NavBotJobs.h"
#include "../../Players/PlayerUtils.h"
#include "../../Aimbot/AimbotProjectile/AimbotProjectile.h"
#include "../Objectives.h"
#include "../../Misc/NamedPipe/NamedPipe.h"

static CCaptureFlag* FindClosestWorldFlag(const Vector& vLocalOrigin)
{
	CCaptureFlag* pBestFlag = nullptr;
	float flBestDist = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		const float flDist = vLocalOrigin.DistToSqr(pFlag->GetAbsOrigin());
		if (flDist >= flBestDist)
			continue;

		flBestDist = flDist;
		pBestFlag = pFlag;
	}

	return pBestFlag;
}

static Vector AdjustCaptureCandidateToNav(Vector vCandidate)
{
	if (!F::NavEngine.IsNavMeshLoaded())
		return vCandidate;

	if (auto pArea = F::NavEngine.FindClosestNavArea(vCandidate))
	{
		Vector vCorrected = pArea->GetNearestPoint(vCandidate.Get2D());
		vCorrected.z = pArea->m_vCenter.z;
		return vCorrected;
	}

	return vCandidate;
}

bool CNavBotCapture::CanCaptureObjective()
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::CaptureObjectives))
		return false;

	const auto pGameRules = I::TFGameRules();
	if (!pGameRules)
		return true;

	const int iRoundState = pGameRules->m_iRoundState();
	if ((iRoundState != GR_STATE_RND_RUNNING && iRoundState != GR_STATE_STALEMATE) || pGameRules->m_bInWaitingForPlayers())
		return false;

	return !pGameRules->m_bPlayingSpecialDeliveryMode() || F::GameObjectiveController.m_bDoomsday;
}

bool CNavBotCapture::IsThreatPlayer(int iIndex)
{
#ifdef TEXTMODE
	if (auto pResource = H::Entities.GetResource(); pResource && F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iIndex)))
		return false;
#endif

	return !F::PlayerUtils.IsIgnored(iIndex);
}

bool CNavBotCapture::GetCtfGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut)
{
	m_sCaptureStatus = L"";
	if (F::GameObjectiveController.m_bHaarp)
	{
		if (iOurTeam == TF_TEAM_BLUE)
		{
			if (F::HaarpController.GetCapturePos(vOut))
			{
				m_sCaptureStatus = F::HaarpController.m_sHaarpStatus;
				return true;
			}
			if (auto pBestFlag = FindClosestWorldFlag(pLocal->GetAbsOrigin()))
			{
				m_sCaptureStatus = L"Flag";
				vOut = pBestFlag->GetAbsOrigin();
				return true;
			}
		}
		else if (F::HaarpController.GetDefensePos(vOut))
		{
			m_sCaptureStatus = F::HaarpController.m_sHaarpStatus;
			return true;
		}
	}

	Vector vPosition;
	bool bNeutralFlag = false;
	if (!F::FlagController.GetPosition(iEnemyTeam, vPosition))
	{
		if (!F::FlagController.GetPosition(TEAM_UNASSIGNED, vPosition))
			return false;
		iEnemyTeam = TEAM_UNASSIGNED;
		bNeutralFlag = true;
	}

	const int iStatus = F::FlagController.GetStatus(iEnemyTeam);
	const int iCarrierIdx = F::FlagController.GetCarrier(iEnemyTeam);

	if (iStatus != TF_FLAGINFO_STOLEN)
	{
		m_sCaptureStatus = L"Flag";
		vOut = vPosition;
		return true;
	}

	if (iCarrierIdx == pLocal->entindex())
	{
		if (!F::FlagController.GetSpawnPosition(iOurTeam, vOut))
			return false;

		m_sCaptureStatus = L"CP";
		return true;
	}

	if (iCarrierIdx <= 0)
		return false;

	if (F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
	{
		if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpCaptureObjectives))
			return false;

		m_sCaptureStatus = L"Assist";
		vOut = vPosition - Vector(40.f, 40.f, 0.f);
		return true;
	}

	auto pCarrier = I::ClientEntityList->GetClientEntity(iCarrierIdx);
	if (bNeutralFlag && pCarrier && pCarrier->As<CBaseEntity>()->m_iTeamNum() != iOurTeam)
	{
		m_sCaptureStatus = L"Carrier";
		vOut = vPosition;
		return true;
	}

	return false;
}

bool CNavBotCapture::GetPayloadGoal(const CHandle<CTFPlayer> hLocal, const Vector vLocalOrigin, int iOurTeam, Vector& vOut)
{
	m_sCaptureStatus = L"Payload";

	auto pPayload = F::PLController.GetClosestPayload(vLocalOrigin, iOurTeam);
	if (!pPayload)
		return false;

	const Vector vOrigin = pPayload->GetAbsOrigin();
	if (vLocalOrigin.DistTo(vOrigin) <= 200.f)
	{
		m_bOverwriteCapture = true;
		m_bWalkTo = !pPayload->m_hHealingTargets().HasElement(hLocal);
	}

	vOut = vOrigin;
	return true;
}

bool CNavBotCapture::GetTugOfWarGoal(CTFPlayer* pLocal, int iOurTeam, Vector& vOut)
{
	constexpr float flBoardDistance = 200.f;
	constexpr float flOnCartDistance = 56.f;
	constexpr int iMaxPushers = 4;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	auto pCart = F::PLController.GetClosestPayload(vLocalOrigin, iOurTeam);
	if (!pCart)
		return false;

	const int iOwner = F::PLController.GetTugOwner();
	const Vector vOrigin = pCart->GetAbsOrigin();
	const float flDist2D = vLocalOrigin.DistTo2D(vOrigin);
	const bool bOnCart = flDist2D <= flOnCartDistance && fabsf(vLocalOrigin.z - vOrigin.z) <= PLAYER_JUMP_HEIGHT * 2.f;
	const bool bGuard = iOwner == iOurTeam && !bOnCart && F::PLController.GetTugPushers(iOurTeam) >= iMaxPushers;

	m_sCaptureStatus = bGuard ? L"Guard" : iOwner == iOurTeam ? L"Push" : iOwner == TEAM_UNASSIGNED ? L"Claim" : L"Contest";
	vOut = vOrigin;
	if (vLocalOrigin.DistTo(vOrigin) <= flBoardDistance)
	{
		m_bOverwriteCapture = true;
		m_bWalkTo = !bGuard && !bOnCart;
	}
	return true;
}

bool CNavBotCapture::FindCoverNearPoint(const Vector& vPoint, float flRadius, Vector& vOut) const
{
	auto pNavFile = F::NavEngine.GetNavFile();
	if (!pNavFile)
		return false;

	bool bFound = false;
	float flBestDistSqr = flRadius * flRadius;
	for (const auto& tArea : pNavFile->m_vAreas)
	{
		for (const auto& tHidingSpot : tArea.m_vHidingSpots)
		{
			if (!tHidingSpot.HasGoodCover())
				continue;

			const float flDistSqr = tHidingSpot.m_vPos.DistToSqr(vPoint);
			if (flDistSqr > flBestDistSqr)
				continue;

			flBestDistSqr = flDistSqr;
			vOut = tHidingSpot.m_vPos;
			bFound = true;
		}
	}
	return bFound;
}

bool CNavBotCapture::GetControlPointGoal(const Vector vLocalOrigin, int iOurTeam, Vector& vOut)
{
	m_sCaptureStatus = L"CP";
	std::pair<int, Vector> tControlPointInfo;
	if (!F::CPController.GetClosestControlPointInfo(vLocalOrigin, iOurTeam, tControlPointInfo))
	{
		m_vCurrentCaptureSpot.reset();
		m_vCurrentCaptureCenter.reset();
		ReleaseCaptureSpotClaim();
		return false;
	}

	const Vector vPosition = tControlPointInfo.second;
	const int iControlPointIdx = tControlPointInfo.first;

	if (!m_vCurrentCaptureCenter.has_value() || m_vCurrentCaptureCenter->DistToSqr(vPosition) > 1.0f)
	{
		m_vCurrentCaptureCenter = vPosition;
		m_vCurrentCaptureSpot.reset();
	}

	constexpr float flCapRadius = 100.0f;
	constexpr float flThreatRadius = 800.0f;
	constexpr float flOccupancyRadius = 28.0f;
	constexpr float flOccupancyRadiusSq = flOccupancyRadius * flOccupancyRadius;
	const int iLocalIndex = I::EngineClient->GetLocalPlayer();

	std::vector<Vector> vTeammatePositions;
	vTeammatePositions.reserve(8);
	int iTeammatesOnPoint = 0;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		if (pEntity->IsDormant() || pEntity->entindex() == iLocalIndex)
			continue;

		auto pTeammate = pEntity->As<CTFPlayer>();
		if (!pTeammate->IsAlive())
			continue;

		const Vector vTeammateOrigin = pTeammate->GetAbsOrigin();
		vTeammatePositions.push_back(vTeammateOrigin);

		if (vTeammateOrigin.DistTo(vPosition) <= flCapRadius)
			iTeammatesOnPoint++;
	}

	bool bEnemiesNear = false;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (pEntity->IsDormant())
			continue;

		auto pEnemy = pEntity->As<CTFPlayer>();
		if (!pEnemy->IsAlive() || !IsThreatPlayer(pEnemy->entindex()))
			continue;

		if (pEnemy->GetAbsOrigin().DistTo(vPosition) <= flThreatRadius)
		{
			bEnemiesNear = true;
			break;
		}
	}

#ifdef TEXTMODE
	const std::vector<Vector> vReservedSpots = F::NamedPipe.GetReservedCaptureSpots(SDK::GetLevelName(), iControlPointIdx, H::Entities.GetLocalAccountID());
#endif

	auto SpotTakenByOther = [&](const Vector& vSpot) -> bool
		{
#ifdef TEXTMODE
			for (const auto& vReserved : vReservedSpots)
			{
				if (vReserved.DistTo2DSqr(vSpot) <= flOccupancyRadiusSq)
					return true;
			}
#else
			for (const auto& vTeammatePos : vTeammatePositions)
			{
				if (vTeammatePos.DistTo2DSqr(vSpot) <= flOccupancyRadiusSq)
					return true;
			}
#endif
			return false;
		};

	auto ClosestTeammateDistance = [&](const Vector& vSpot) -> float
		{
			float flBest = FLT_MAX;
			for (const auto& vTeammatePos : vTeammatePositions)
				flBest = std::min(flBest, vTeammatePos.DistTo2DSqr(vSpot));
			return flBest;
		};

	Vector vAdjustedPos = vPosition;

	if (bEnemiesNear && !F::CPController.IsCapturing(iControlPointIdx, iOurTeam))
	{
		m_vCurrentCaptureSpot.reset();
		FindCoverNearPoint(vPosition, flCapRadius, vAdjustedPos);
	}
	else
	{
		if (m_vCurrentCaptureSpot && SpotTakenByOther(*m_vCurrentCaptureSpot))
			m_vCurrentCaptureSpot.reset();

		if (!m_vCurrentCaptureSpot)
		{
			const int iSlots = std::clamp(iTeammatesOnPoint + 1, 1, 8);
			const float flBaseRadius = iSlots == 1 ? 0.0f : std::min(flCapRadius - 12.0f, 45.0f + 12.0f * static_cast<float>(iSlots - 1));
			const int iPreferredSlot = iLocalIndex % iSlots;

			std::vector<Vector> vFallbackCandidates;
			vFallbackCandidates.reserve(iSlots + 12);

			auto TryCandidate = [&](float flAngle, float flRadius) -> bool
				{
					Vector vCandidate = vPosition;
					if (flRadius > 1.0f)
					{
						vCandidate.x += cos(flAngle) * flRadius;
						vCandidate.y += sin(flAngle) * flRadius;
					}

					vCandidate = AdjustCaptureCandidateToNav(vCandidate);
					vFallbackCandidates.push_back(vCandidate);
					if (SpotTakenByOther(vCandidate))
						return false;

					m_vCurrentCaptureSpot = vCandidate;
					return true;
				};

			for (int iOffset = 0; iOffset < iSlots && !m_vCurrentCaptureSpot; ++iOffset)
			{
				const int iSlotIndex = (iPreferredSlot + iOffset) % iSlots;
				TryCandidate(static_cast<float>(iSlotIndex) / static_cast<float>(iSlots) * Math::PI * 2.0f, flBaseRadius);
			}

			for (int iRing = 1; iRing <= 2 && !m_vCurrentCaptureSpot; ++iRing)
			{
				const float flRingRadius = std::min(flCapRadius - 12.0f, flBaseRadius + 14.0f * static_cast<float>(iRing));
				const int iMaxSegments = std::max(6, iSlots + iRing * 2);
				for (int iSeg = 0; iSeg < iMaxSegments && !m_vCurrentCaptureSpot; ++iSeg)
					TryCandidate(static_cast<float>(iSeg) / static_cast<float>(iMaxSegments) * Math::PI * 2.0f, flRingRadius);
			}

			if (!m_vCurrentCaptureSpot)
			{
				vFallbackCandidates.push_back(AdjustCaptureCandidateToNav(vPosition));

				Vector vBestCandidate = vPosition;
				float flBestScore = -1.0f;
				for (const auto& vCandidate : vFallbackCandidates)
				{
					const float flScore = ClosestTeammateDistance(vCandidate);
					if (flScore > flBestScore)
					{
						flBestScore = flScore;
						vBestCandidate = vCandidate;
					}
				}
				m_vCurrentCaptureSpot = vBestCandidate;
			}
		}

		vAdjustedPos = *m_vCurrentCaptureSpot;
	}

	if (m_vCurrentCaptureSpot)
		ClaimCaptureSpot(*m_vCurrentCaptureSpot, iControlPointIdx);
	else
		ReleaseCaptureSpotClaim();

	if (vLocalOrigin.DistTo(vAdjustedPos) <= 150.0f)
		vAdjustedPos.z = vLocalOrigin.z;

	constexpr float flArrivedDistSq = 45.0f * 45.0f;
	if (Vector2D(vAdjustedPos.x - vLocalOrigin.x, vAdjustedPos.y - vLocalOrigin.y).LengthSqr() <= flArrivedDistSq)
	{
		std::pair<int, Vector> tVerify;
		if (!F::CPController.GetClosestControlPointInfo(vLocalOrigin, iOurTeam, tVerify) || tVerify.first != iControlPointIdx)
		{
			m_vCurrentCaptureSpot.reset();
			ReleaseCaptureSpotClaim();
			return false;
		}

		m_bOverwriteCapture = true;
		return false;
	}

	vOut = vAdjustedPos;
	return true;
}

bool CNavBotCapture::GetStagedPointGoal(CTFPlayer* pLocal, int iOurTeam, Vector& vOut)
{
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	std::pair<int, Vector> tPoint;
	if (!F::CPController.GetClosestControlPointInfo(vLocalOrigin, iOurTeam, tPoint, true))
		return false;

	if (F::CPController.IsPointUseable(tPoint.first, iOurTeam))
		return GetControlPointGoal(vLocalOrigin, iOurTeam, vOut);

	m_sCaptureStatus = L"Wait";
	constexpr float flStageDistance = 350.f;
	Vector vStage = tPoint.second;
	Vector vSpawn;
	if (ObjectiveUtils::GetTeamSpawnCenter(iOurTeam, vSpawn))
	{
		Vector vDir = vSpawn - tPoint.second;
		vDir.z = 0.f;
		if (vDir.Normalize() > 1.f)
			vStage += vDir * flStageDistance;
	}
	vStage = ObjectiveUtils::AdjustPosToNav(vStage);

	if (vLocalOrigin.DistTo2D(vStage) <= 120.f)
	{
		m_bOverwriteCapture = true;
		m_bWalkTo = false;
		return false;
	}

	vOut = vStage;
	return true;
}

bool CNavBotCapture::GetPlayerDestructionGoal(CTFPlayer* pLocal, Vector& vOut)
{
	PDGoal_t tGoal;
	if (!F::PDController.GetGoal(pLocal, true, tGoal))
		return false;

	vOut = tGoal.m_vPos;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	switch (tGoal.m_eKind)
	{
	case PDGoalEnum::Deliver:
		m_sCaptureStatus = L"Deliver";
		if (F::PDController.GetZoneAt(vLocalOrigin, pLocal->m_iTeamNum()))
		{
			m_bOverwriteCapture = true;
			m_bWalkTo = true;
		}
		break;
	case PDGoalEnum::Collect:
		m_sCaptureStatus = L"Collect";
		break;
	case PDGoalEnum::Hunt:
		m_sCaptureStatus = L"Leader";
		break;
	default:
		m_sCaptureStatus = L"Escort";
		if (vLocalOrigin.DistTo(vOut) <= 250.f)
		{
			m_bOverwriteCapture = true;
			m_bWalkTo = false;
		}
		break;
	}
	return true;
}

bool CNavBotCapture::GetRobotDestructionGoal(CTFPlayer* pLocal, Vector& vOut)
{
	RDGoal_t tGoal;
	if (!F::RDController.GetGoal(pLocal, true, tGoal))
		return false;

	vOut = tGoal.m_vPos;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	switch (tGoal.m_eKind)
	{
	case RDGoalEnum::Deliver:
		m_sCaptureStatus = L"Deliver";
		if (F::RDController.GetZoneAt(vLocalOrigin, pLocal->m_iTeamNum()))
		{
			m_bOverwriteCapture = true;
			m_bWalkTo = true;
		}
		break;
	case RDGoalEnum::Collect:
		m_sCaptureStatus = L"Collect";
		break;
	default:
		m_sCaptureStatus = L"Robot";
		if (vLocalOrigin.DistTo(vOut) <= 300.f)
		{
			m_bOverwriteCapture = true;
			m_bWalkTo = false;
		}
		break;
	}
	return true;
}

bool CNavBotCapture::GetDoomsdayGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut)
{
	if (!F::DoomsdayController.GetGoal(vOut))
		return false;

	m_sCaptureStatus = F::DoomsdayController.m_sDoomsdayStatus;
	if (m_sCaptureStatus != L"Assist")
		return true;

	auto pFlag = F::DoomsdayController.GetFlag();
	const int iCarrierIdx = pFlag ? F::FlagController.GetCarrier(pFlag) : -1;
	if (iCarrierIdx == -1 || !F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
		return false;

	auto pCarrier = I::ClientEntityList->GetClientEntity(iCarrierIdx);
	if (!pCarrier || pCarrier->IsDormant())
		return false;

	const Vector vCarrierPos = pCarrier->GetAbsOrigin();
	Vector vRocket;
	if (!F::DoomsdayController.GetCapturePos(vRocket))
	{
		vOut = vCarrierPos - Vector(40.f, 40.f, 0.f);
		return true;
	}

	Vector vCarrierToRocket = vRocket - vCarrierPos;
	if (const float flLength = vCarrierToRocket.Length(); flLength > 0.001f)
		vCarrierToRocket /= flLength;

	Vector vSide = vCarrierToRocket.Cross(Vector(0, 0, 1));
	if (const float flLength = vSide.Length(); flLength > 0.001f)
		vSide /= flLength;

	vOut = vCarrierPos + vCarrierToRocket * -80.0f - vSide * 60.0f;
	return true;
}

bool CNavBotCapture::GetPasstimeGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut)
{
	m_sCaptureStatus = L"";

	if (!F::PasstimeController.GetBall())
		return false;

	const int iCarrierIdx = F::PasstimeController.GetCarrier();
	if (pLocal->m_bHasPasstimeBall() || iCarrierIdx == pLocal->entindex())
	{
		PasstimeGoalInfo tGoal = {};
		if (!F::PasstimeController.GetGoalInfo(iOurTeam, pLocal->GetAbsOrigin(), tGoal) || !F::PasstimeController.IsEndzoneGoal(tGoal.m_iGoalType))
			return false;

		m_sCaptureStatus = L"Goal";
		vOut = tGoal.m_vOrigin;
		if (F::PasstimeController.IsPointInGoal(tGoal, pLocal->GetAbsOrigin()))
		{
			m_bOverwriteCapture = true;
			m_bWalkTo = true;
		}
		return true;
	}

	if (iCarrierIdx > 0)
	{
		auto pCarrierEnt = I::ClientEntityList->GetClientEntity(iCarrierIdx);
		auto pCarrier = pCarrierEnt ? pCarrierEnt->As<CTFPlayer>() : nullptr;
		if (!pCarrier || pCarrier->IsDormant() || !pCarrier->IsAlive())
			return false;

		const Vector vCarrierPos = pCarrier->GetAbsOrigin();
		if (pCarrier->m_iTeamNum() == iOurTeam)
		{
			if (!F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
				return false;

			Vector vGoalPos = {};
			Vector vDir = {};
			if (!F::PasstimeController.GetGoalPos(iOurTeam, vCarrierPos, vGoalPos) || (vDir = vGoalPos - vCarrierPos).Normalize() <= 0.01f)
			{
				m_sCaptureStatus = L"Assist";
				vOut = vCarrierPos;
				return true;
			}

			Vector vSide = vDir.Cross(Vector(0, 0, 1));
			vSide.Normalize();

			const bool bTargetedForPass = pLocal->m_bIsTargetedForPasstimePass();
			float flForward = bTargetedForPass ? 180.f : -70.f;
			const float flSide = bTargetedForPass ? 60.f : 85.f;
			const float flMaxPassRange = F::PasstimeController.GetMaxPassRange();
			if (flForward > 0.f && flMaxPassRange != FLT_MAX)
				flForward = std::min(flForward, flMaxPassRange * 0.65f);

			m_sCaptureStatus = bTargetedForPass ? L"Pass" : L"Assist";
			vOut = vCarrierPos + vDir * flForward + vSide * flSide;
			return true;
		}

		Vector vEnemyGoal = {};
		if (F::PasstimeController.GetGoalPos(iEnemyTeam, vCarrierPos, vEnemyGoal) && vCarrierPos.DistTo(vEnemyGoal) <= 850.f)
		{
			m_sCaptureStatus = L"Defend";
			vOut = vEnemyGoal;
			return true;
		}

		m_sCaptureStatus = L"Carrier";
		vOut = vCarrierPos;
		return true;
	}

	if (!F::PasstimeController.GetBallPos(vOut))
		return false;

	m_sCaptureStatus = L"Ball";
	return true;
}

void CNavBotCapture::ClaimCaptureSpot(const Vector& vSpot, int iPointIdx)
{
#ifdef TEXTMODE
	const std::optional<int> oPreviousIndex = m_iCurrentCapturePointIdx;
	if (iPointIdx >= 0)
	{
		const bool bChangedPoint = !oPreviousIndex || *oPreviousIndex != iPointIdx;
		const bool bChangedSpot = !m_vLastClaimedCaptureSpot || m_vLastClaimedCaptureSpot->DistToSqr(vSpot) > 1.0f;
		if (bChangedPoint && oPreviousIndex)
			F::NamedPipe.AnnounceCaptureSpotRelease(SDK::GetLevelName(), *oPreviousIndex);
		if (bChangedPoint || bChangedSpot || m_tCaptureClaimRefresh.Run(0.6f))
		{
			F::NamedPipe.AnnounceCaptureSpotClaim(SDK::GetLevelName(), iPointIdx, vSpot, 1.5f);
			m_tCaptureClaimRefresh.Update();
		}
	}
#endif
	m_vLastClaimedCaptureSpot = vSpot;
	m_iCurrentCapturePointIdx = iPointIdx;
}

void CNavBotCapture::ReleaseCaptureSpotClaim()
{
	const bool bHadClaim = m_iCurrentCapturePointIdx.has_value() || m_vLastClaimedCaptureSpot.has_value();
#ifdef TEXTMODE
	if (m_iCurrentCapturePointIdx)
		F::NamedPipe.AnnounceCaptureSpotRelease(SDK::GetLevelName(), *m_iCurrentCapturePointIdx);
#endif
	m_vLastClaimedCaptureSpot.reset();
	m_iCurrentCapturePointIdx.reset();
	if (bHadClaim)
		m_tCaptureClaimRefresh -= 10.f;
}

bool CNavBotCapture::GetCachedGoal(CTFPlayer* pLocal, bool (CNavBotCapture::*pfnGoal)(CTFPlayer*, Vector&), Vector& vOut)
{
	if (m_tPlanRefresh.Run(0.5f))
	{
		Vector vTarget = {};
		m_tCachedPlan = {};
		m_tCachedPlan.m_bGotTarget = (this->*pfnGoal)(pLocal, vTarget);
		m_tCachedPlan.m_vTarget = vTarget;
		m_tCachedPlan.m_bOverwrite = m_bOverwriteCapture;
		m_tCachedPlan.m_bWalkTo = m_bWalkTo;
		m_tCachedPlan.m_sStatus = m_sCaptureStatus;
	}
	else
	{
		m_bOverwriteCapture = m_tCachedPlan.m_bOverwrite;
		m_bWalkTo = m_tCachedPlan.m_bWalkTo;
		m_sCaptureStatus = m_tCachedPlan.m_sStatus;
	}

	vOut = m_tCachedPlan.m_vTarget;
	return m_tCachedPlan.m_bGotTarget;
}

bool CNavBotCapture::GetObjectiveGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut)
{
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	const auto& tObjectives = F::GameObjectiveController;

	if (tObjectives.m_bZombieInfection)
		return GetZombieInfectionGoal(vOut);

	switch (tObjectives.m_eGameMode)
	{
	case TF_GAMETYPE_CTF:
		return GetCtfGoal(pLocal, iOurTeam, iEnemyTeam, vOut);
	case TF_GAMETYPE_CP:
	case TF_GAMETYPE_ARENA:
		return GetStagedPointGoal(pLocal, iOurTeam, vOut);
	case TF_GAMETYPE_ESCORT:
		if (tObjectives.m_bTugOfWar)
			return GetTugOfWarGoal(pLocal, iOurTeam, vOut);
		if (tObjectives.m_bPayloadHybrid)
		{
			if (GetStagedPointGoal(pLocal, iOurTeam, vOut) || m_bOverwriteCapture)
				return true;
		}
		return GetPayloadGoal(pLocal->GetRefEHandle(), vLocalOrigin, iOurTeam, vOut)
			|| (!F::PLController.HasPayloads() && GetControlPointGoal(vLocalOrigin, iOurTeam, vOut));
	case TF_GAMETYPE_PASSTIME:
		return GetPasstimeGoal(pLocal, iOurTeam, iEnemyTeam, vOut);
	case TF_GAMETYPE_PD:
		return GetCachedGoal(pLocal, &CNavBotCapture::GetPlayerDestructionGoal, vOut)
			|| GetControlPointGoal(vLocalOrigin, iOurTeam, vOut);
	case TF_GAMETYPE_RD:
		return GetCachedGoal(pLocal, &CNavBotCapture::GetRobotDestructionGoal, vOut);
	default:
		return tObjectives.m_bDoomsday ? GetDoomsdayGoal(pLocal, iOurTeam, iEnemyTeam, vOut)
			: GetControlPointGoal(vLocalOrigin, iOurTeam, vOut);
	}
}

void CNavBotCapture::LookAtObjective(CUserCmd* pCmd, CTFPlayer* pLocal, const Vec3& vTarget, bool bTargetValid)
{
	if (G::Attacking == 1)
	{
		F::BotUtils.InvalidateLLAP();
		return;
	}

	const auto eLook = Vars::Misc::Movement::NavEngine::LookAtPath.Value;
	const bool bSilent = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Silent || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;
	const bool bLegit = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Legit || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;

	if (eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Off || (bSilent && G::AntiAim))
	{
		F::BotUtils.InvalidateLLAP();
		return;
	}

	if (bLegit)
		F::BotUtils.LookLegit(pLocal, pCmd, bTargetValid ? vTarget : Vec3{}, bSilent);
	else if (bTargetValid)
	{
		F::BotUtils.InvalidateLLAP();
		F::BotUtils.LookAtPath(pCmd, Vec2(vTarget.x, vTarget.y), pLocal->GetEyePosition(), bSilent);
	}
	else
		F::BotUtils.InvalidateLLAP();
}

void CNavBotCapture::HoldObjective(CUserCmd* pCmd, CTFPlayer* pLocal, const Vector& vTarget)
{
	if (F::NavEngine.IsPathing())
		F::NavEngine.CancelPath();

	if (m_bWalkTo)
	{
		LookAtObjective(pCmd, pLocal, vTarget, true);
		SDK::WalkTo(pCmd, pLocal, vTarget);
	}
	else
		LookAtObjective(pCmd, pLocal, {}, false);
}

bool CNavBotCapture::Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!CanCaptureObjective() || !m_tCaptureTimer.Check(2.f) || F::NavEngine.m_eCurrentPriority > PriorityListEnum::Capture)
		return false;

	const int iOurTeam = pLocal->m_iTeamNum();
	const int iEnemyTeam = iOurTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
	m_bOverwriteCapture = false;
	m_bWalkTo = false;

	Vector vTarget = {};
	const bool bGotTarget = GetObjectiveGoal(pLocal, iOurTeam, iEnemyTeam, vTarget);

	if (m_bOverwriteCapture)
	{
		HoldObjective(pCmd, pLocal, vTarget);
		return true;
	}

	if (!bGotTarget)
	{
		m_tCaptureTimer.Update();
		return false;
	}

	if (Vars::Debug::Info.Value)
		G::BoxStorage.emplace_back(vTarget, Vec3(-16.0f, -16.0f, -16.0f), Vec3(16.0f, 16.0f, 16.0f), Vec3(), I::GlobalVars->curtime + 2.1f, Color_t(255, 255, 255, 180), Color_t(0, 0, 0, 0), true);

	const bool bPasstimeCarrier = F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_PASSTIME && pLocal->m_bHasPasstimeBall();
	if (bPasstimeCarrier && pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_PASSTIME_GUN
		&& F::AimbotProjectile.AimPasstimePass(pLocal, pWeapon, pCmd))
	{
		if (F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();
		return true;
	}

	const float flRetargetThresholdSq = bPasstimeCarrier ? 180.0f * 180.0f : 256.0f;
	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture && vTarget.DistToSqr(m_vPreviousTarget) <= flRetargetThresholdSq)
		return true;

	if (!F::NavEngine.NavTo(vTarget, PriorityListEnum::Capture))
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("NavBotCapture", std::format("NavTo failed for capture target ({})", SDK::ConvertWideToUTF8(m_sCaptureStatus)).c_str(), { 255, 100, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		m_tCaptureTimer.Update();
		return false;
	}

	m_vPreviousTarget = vTarget;
	return true;
}

void CNavBotCapture::Reset()
{
	m_vCurrentCaptureSpot.reset();
	m_vCurrentCaptureCenter.reset();
	ReleaseCaptureSpotClaim();
	m_tCachedPlan = {};
	m_vPreviousTarget = {};
	m_tPlanRefresh -= 10.f;
}
