#include "NavBotJobs.h"
#include "../../Players/PlayerUtils.h"
#include "../../Aimbot/AimbotProjectile/AimbotProjectile.h"
#include "../NavEngine/Controllers/CPController/CPController.h"
#include "../NavEngine/Controllers/FlagController/FlagController.h"
#include "../NavEngine/Controllers/PLController/PLController.h"
#include "../NavEngine/Controllers/HaarpController/HaarpController.h"
#include "../NavEngine/Controllers/DoomsdayController/DoomsdayController.h"
#include "../NavEngine/Controllers/PasstimeController/PasstimeController.h"
#include "../NavEngine/Controllers/Controller.h"
#include "../../Misc/NamedPipe/NamedPipe.h"

static CCaptureFlag* FindClosestWorldFlag(const Vector& vLocalOrigin)
{
	CCaptureFlag* pBestFlag = nullptr;
	float flBestDist = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (pEntity->GetClassID() != ETFClassID::CCaptureFlag)
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

bool CNavBotCapture::ShouldAvoidPlayer(int iIndex)
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
			auto pBestFlag = FindClosestWorldFlag(pLocal->GetAbsOrigin());
			if (pBestFlag)
			{
				m_sCaptureStatus = L"Flag";
				vOut = pBestFlag->GetAbsOrigin();
				return true;
			}
		}
		else
		{
			if (F::HaarpController.GetDefensePos(vOut))
			{
				m_sCaptureStatus = F::HaarpController.m_sHaarpStatus;
				return true;
			}
		}
		// If HaarpController didn't provide a goal (e.g. teammate has flag), fall through to standard CTF logic for assistance
	}

	Vector vPosition;
	if (!F::FlagController.GetPosition(iEnemyTeam, vPosition))
	{
		if (!F::FlagController.GetPosition(0, vPosition)) // Try neutral flag
			return false;
		iEnemyTeam = 0; // Use neutral team if found
	}

	// Get Flag related information
	auto iStatus = F::FlagController.GetStatus(iEnemyTeam);
	auto iCarrierIdx = F::FlagController.GetCarrier(iEnemyTeam);

	// CTF is the current capture type
	if (iStatus == TF_FLAGINFO_STOLEN)
	{
		if (iCarrierIdx == pLocal->entindex())
		{
			// Return our capture point location
			if (F::FlagController.GetSpawnPosition(iOurTeam, vPosition))
			{
				m_sCaptureStatus = L"CP";
				vOut = vPosition;
				return true;
			}
		}
		// Assist with capturing
		else if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpCaptureObjectives)
		{
			if (F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
			{
				m_sCaptureStatus = L"Assist";
				// Stay slightly behind and to the side to avoid blocking
				Vector vOffset(40.0f, 40.0f, 0.0f);
				vPosition -= vOffset;
				vOut = vPosition;
				return true;
			}
		}
		return false;
	}

	// Get the flag if not taken by us already
	m_sCaptureStatus = L"Flag";
	vOut = vPosition;

	return true;
}

bool CNavBotCapture::GetPayloadGoal(const CHandle<CTFPlayer> hLocal, const Vector vLocalOrigin, int iOurTeam, Vector& vOut)
{
	m_sCaptureStatus = L"Payload";

	auto pPayload = F::PLController.GetClosestPayload(vLocalOrigin, iOurTeam);
	if (!pPayload)
		return false;

	Vector vOrigin = pPayload->GetAbsOrigin();
	if (vLocalOrigin.DistTo(vOrigin) <= 200.f)
	{
		m_bOverwriteCapture = true;
		m_bWalkTo = !pPayload->m_hHealingTargets().HasElement(hLocal);
	}

	vOut = vOrigin;
	return true;
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

	Vector vPosition = tControlPointInfo.second;
	const int iControlPointIdx = tControlPointInfo.first;

	if (!m_vCurrentCaptureCenter.has_value() || m_vCurrentCaptureCenter->DistToSqr(vPosition) > 1.0f)
	{
		m_vCurrentCaptureCenter = vPosition;
		m_vCurrentCaptureSpot.reset();
	}

	constexpr float flCapRadius = 100.0f; // Approximate capture radius
	constexpr float flThreatRadius = 800.0f; // Distance to check for enemies
	constexpr float flOccupancyRadius = 28.0f;
	const float flOccupancyRadiusSq = flOccupancyRadius * flOccupancyRadius;
	const int iLocalIndex = I::EngineClient->GetLocalPlayer();

	std::vector<Vector> vTeammatePositions;
	vTeammatePositions.reserve(8);
	int iTeammatesOnPoint = 0;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		if (pEntity->IsDormant() || pEntity->entindex() == iLocalIndex)
			continue;

		auto pTeammate = pEntity->As<CTFPlayer>();
		if (!pTeammate || !pTeammate->IsAlive())
			continue;

		Vector vTeammateOrigin = pTeammate->GetAbsOrigin();
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
		if (!pEnemy->IsAlive() || !ShouldAvoidPlayer(pEnemy->entindex()))
			continue;

		if (pEnemy->GetAbsOrigin().DistTo(vPosition) <= flThreatRadius)
		{
			bEnemiesNear = true;
			break;
		}
	}

#ifdef TEXTMODE
	std::vector<Vector> vReservedSpots;
	if (iControlPointIdx != -1)
		vReservedSpots = F::NamedPipe.GetReservedCaptureSpots(SDK::GetLevelName(), iControlPointIdx, H::Entities.GetLocalAccountID());
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
			if (vTeammatePositions.empty())
				return FLT_MAX;

			float flBest = FLT_MAX;
			for (const auto& vTeammatePos : vTeammatePositions)
			{
				float flDist = vTeammatePos.DistTo2DSqr(vSpot);
				if (flDist < flBest)
					flBest = flDist;
			}
			return flBest;
		};

	Vector vAdjustedPos = vPosition;

	if (bEnemiesNear)
	{
		m_vCurrentCaptureSpot.reset();
		for (auto tArea : F::NavEngine.GetNavFile()->m_vAreas)
		{
			for (auto& tHidingSpot : tArea.m_vHidingSpots)
			{
				if (tHidingSpot.HasGoodCover() && tHidingSpot.m_vPos.DistTo(vPosition) <= flCapRadius)
				{
					vAdjustedPos = tHidingSpot.m_vPos;
					break;
				}
			}
		}
	}
	else
	{
		if (m_vCurrentCaptureSpot && SpotTakenByOther(m_vCurrentCaptureSpot.value()))
			m_vCurrentCaptureSpot.reset();

		if (!m_vCurrentCaptureSpot)
		{
			const int iSlots = std::clamp(iTeammatesOnPoint + 1, 1, 8);
			const float flBaseRadius = iSlots == 1 ? 0.0f : std::min(flCapRadius - 12.0f, 45.0f + 12.0f * static_cast<float>(iSlots - 1));
			const int iPreferredSlot = iSlots > 0 ? (iLocalIndex % iSlots) : 0;

			std::vector<Vector> vFallbackCandidates;
			vFallbackCandidates.reserve(iSlots + 12);

			for (int offset = 0; offset < iSlots; ++offset)
			{
				int slotIndex = (iPreferredSlot + offset) % iSlots;
				float t = static_cast<float>(slotIndex) / static_cast<float>(iSlots);
				float flAngle = t * Math::PI * 2.0f;

				Vector vCandidate = vPosition;
				if (flBaseRadius > 1.0f)
				{
					vCandidate.x += cos(flAngle) * flBaseRadius;
					vCandidate.y += sin(flAngle) * flBaseRadius;
				}

				vCandidate = AdjustCaptureCandidateToNav(vCandidate);
				vFallbackCandidates.push_back(vCandidate);

				if (!SpotTakenByOther(vCandidate))
				{
					m_vCurrentCaptureSpot = vCandidate;
					break;
				}
			}

			if (!m_vCurrentCaptureSpot)
			{
				for (int iRing = 1; iRing <= 2; ++iRing)
				{
					float flRingRadius = std::min(flCapRadius - 12.0f, flBaseRadius + 14.0f * static_cast<float>(iRing));
					int iMaxSegments = std::max(6, iSlots + iRing * 2);
					for (int iSeg = 0; iSeg < iMaxSegments; ++iSeg)
					{
						float flAngle = (static_cast<float>(iSeg) / iMaxSegments) * Math::PI * 2.0f;
						Vector vCandidate = vPosition;
						if (flRingRadius > 1.0f)
						{
							vCandidate.x += cos(flAngle) * flRingRadius;
							vCandidate.y += sin(flAngle) * flRingRadius;
						}

						vCandidate = AdjustCaptureCandidateToNav(vCandidate);
						vFallbackCandidates.push_back(vCandidate);

						if (!SpotTakenByOther(vCandidate))
						{
							m_vCurrentCaptureSpot = vCandidate;
							break;
						}
					}
					if (m_vCurrentCaptureSpot)
						break;
				}
			}

			if (!m_vCurrentCaptureSpot)
			{
				vFallbackCandidates.push_back(AdjustCaptureCandidateToNav(vPosition));

				Vector vBestCandidate = vPosition;
				float flBestScore = -1.0f;
				for (const auto& vCandidate : vFallbackCandidates)
				{
					float flScore = ClosestTeammateDistance(vCandidate);
					if (flScore > flBestScore)
					{
						flBestScore = flScore;
						vBestCandidate = vCandidate;
					}
				}

				m_vCurrentCaptureSpot = vBestCandidate;
			}
		}

		if (m_vCurrentCaptureSpot)
			vAdjustedPos = m_vCurrentCaptureSpot.value();
	}

	if (m_vCurrentCaptureSpot && iControlPointIdx != -1)
		ClaimCaptureSpot(*m_vCurrentCaptureSpot, iControlPointIdx);
	else
		ReleaseCaptureSpotClaim();

	if (vLocalOrigin.DistTo(vAdjustedPos) <= 150.0f)
		vAdjustedPos.z = vLocalOrigin.z;

	Vector2D vFlatDelta(vAdjustedPos.x - vLocalOrigin.x, vAdjustedPos.y - vLocalOrigin.y);
	if (vFlatDelta.LengthSqr() <= pow(45.0f, 2))
	{
		if (iControlPointIdx == -1)
		{
			m_vCurrentCaptureSpot.reset();
			ReleaseCaptureSpotClaim();
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", "Capture.GetControlPointGoal: nearby control point index invalid, searching other points", { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return false;
		}

		std::pair<int, Vector> tVerify;
		if (!F::CPController.GetClosestControlPointInfo(vLocalOrigin, iOurTeam, tVerify) || tVerify.first != iControlPointIdx)
		{
			m_vCurrentCaptureSpot.reset();
			ReleaseCaptureSpotClaim();
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", std::format("Capture.GetControlPointGoal: control point {} no longer valid (verified {}), searching other points", iControlPointIdx, tVerify.first).c_str(), { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return false;
		}

		m_bOverwriteCapture = true;
		return false;
	}

	vOut = vAdjustedPos;
	return true;
}

bool CNavBotCapture::GetDoomsdayGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut)
{
	if (F::DoomsdayController.GetGoal(vOut))
	{
		m_sCaptureStatus = F::DoomsdayController.m_sDoomsdayStatus;

		// If we are assisting, apply offset
		if (m_sCaptureStatus == L"Assist")
		{
			auto pFlag = F::DoomsdayController.GetFlag();
			if (pFlag)
			{
				int iCarrierIdx = F::FlagController.GetCarrier(pFlag);
				if (iCarrierIdx != -1 && F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
				{
					// Position to the side and slightly behind the carrier in the direction of the rocket
					auto pCarrier = I::ClientEntityList->GetClientEntity(iCarrierIdx);
					if (pCarrier && !pCarrier->IsDormant())
					{
						Vector vRocket;
						if (F::DoomsdayController.GetCapturePos(vRocket))
						{
							Vector vCarrierToRocket = vRocket - pCarrier->GetAbsOrigin();
							float len = vCarrierToRocket.Length();
							if (len > 0.001f)
							{
								vCarrierToRocket /= len;
							}

							Vector vCrossProduct = vCarrierToRocket.Cross(Vector(0, 0, 1));
							float crossLen = vCrossProduct.Length();
							if (crossLen > 0.001f)
							{
								vCrossProduct /= crossLen;
							}

							Vector vOffset = (vCarrierToRocket * -80.0f) - (vCrossProduct * 60.0f);
							vOut = pCarrier->GetAbsOrigin() + vOffset;
						}
						else
						{
							Vector vOffset(40.0f, 40.0f, 0.0f);
							vOut = pCarrier->GetAbsOrigin() - vOffset;
						}
						return true;
					}
				}
			}
			return false; // Don't assist if we shouldn't
		}

		return true;
	}

	return false;
}

bool CNavBotCapture::GetPasstimeGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut)
{
	m_sCaptureStatus = L"";

	auto pBall = F::PasstimeController.GetBall();
	static Timer tPasstimeLogTimer{};
	const bool bLog = Vars::Debug::Logging.Value && tPasstimeLogTimer.Run(0.5f);
	if (bLog)
	{
		SDK::Output("NavBotCapture", std::format(
			"GetPasstimeGoal: local={} ourTeam={} enemyTeam={} hasBall={} ballEnt={} gameMode={}",
			pLocal ? pLocal->entindex() : -1,
			iOurTeam,
			iEnemyTeam,
			pLocal && pLocal->m_bHasPasstimeBall() ? 1 : 0,
			pBall ? pBall->entindex() : -1,
			int(F::GameObjectiveController.m_eGameMode)).c_str(),
			{ 120, 200, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	}

	if (!pBall)
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("NavBotCapture", "GetPasstimeGoal: no ball entity from PasstimeController", { 255, 140, 140 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return false;
	}

	const int iLocalIndex = pLocal->entindex();
	const int iCarrierIdx = F::PasstimeController.GetCarrier();
	const bool bLocalCarrier = pLocal->m_bHasPasstimeBall() || iCarrierIdx == iLocalIndex;

	if (bLog)
	{
		SDK::Output("NavBotCapture", std::format(
			"GetPasstimeGoal: carrierIdx={} localCarrier={}",
			iCarrierIdx, bLocalCarrier ? 1 : 0).c_str(),
			{ 120, 200, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	}

	if (bLocalCarrier)
	{
		PasstimeGoalInfo tGoal = {};
		if (F::PasstimeController.GetGoalInfo(iOurTeam, pLocal->GetAbsOrigin(), tGoal))
		{
			if (!F::PasstimeController.IsEndzoneGoal(tGoal.m_iGoalType))
				return false;

			m_sCaptureStatus = L"Goal";
			vOut = tGoal.m_vOrigin;

			if (F::PasstimeController.IsPointInGoal(tGoal, pLocal->GetAbsOrigin()))
			{
				m_bOverwriteCapture = true;
				m_bWalkTo = true;
			}

			if (Vars::Debug::Logging.Value)
			{
				SDK::Output("NavBotCapture", std::format(
					"GetPasstimeGoal: local carrier target status={} goalType={} out=({:.0f},{:.0f},{:.0f})",
					std::string(m_sCaptureStatus.begin(), m_sCaptureStatus.end()),
					tGoal.m_iGoalType,
					vOut.x, vOut.y, vOut.z).c_str(),
					{ 120, 255, 120 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			}
			return true;
		}

		if (Vars::Debug::Logging.Value)
			SDK::Output("NavBotCapture", "GetPasstimeGoal: local carrier but no goal info for enemy team", { 255, 140, 140 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return false;
	}

	if (iCarrierIdx > 0)
	{
		auto pCarrierEnt = I::ClientEntityList->GetClientEntity(iCarrierIdx);
		auto pCarrier = pCarrierEnt ? pCarrierEnt->As<CTFPlayer>() : nullptr;
		if (!pCarrier || pCarrier->IsDormant() || !pCarrier->IsAlive())
			return false;

		if (pCarrier->m_iTeamNum() == iOurTeam)
		{
			if (!F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
				return false;

			Vector vCarrierPos = pCarrier->GetAbsOrigin();
			Vector vGoalPos = {};
			if (!F::PasstimeController.GetGoalPos(iOurTeam, vCarrierPos, vGoalPos))
			{
				m_sCaptureStatus = L"Assist";
				vOut = vCarrierPos;
				if (Vars::Debug::Logging.Value)
					SDK::Output("NavBotCapture", "GetPasstimeGoal: teammate carrier assist fallback, no goal pos", { 255, 210, 120 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
				return true;
			}

			Vector vDir = vGoalPos - vCarrierPos;
			if (vDir.Normalize() <= 0.01f)
			{
				m_sCaptureStatus = L"Assist";
				vOut = vCarrierPos;
				return true;
			}

			Vector vSide = vDir.Cross(Vector(0, 0, 1));
			vSide.Normalize();

			float flForward = pLocal->m_bIsTargetedForPasstimePass() ? 180.f : -70.f;
			float flSide = pLocal->m_bIsTargetedForPasstimePass() ? 60.f : 85.f;
			float flMaxPassRange = F::PasstimeController.GetMaxPassRange();
			if (flForward > 0.f && flMaxPassRange != FLT_MAX)
				flForward = std::min(flForward, flMaxPassRange * 0.65f);

			m_sCaptureStatus = pLocal->m_bIsTargetedForPasstimePass() ? L"Pass" : L"Assist";
			vOut = vCarrierPos + vDir * flForward + vSide * flSide;
			if (Vars::Debug::Logging.Value)
			{
				SDK::Output("NavBotCapture", std::format(
					"GetPasstimeGoal: teammate carrier status={} out=({:.0f},{:.0f},{:.0f})",
					std::string(m_sCaptureStatus.begin(), m_sCaptureStatus.end()),
					vOut.x, vOut.y, vOut.z).c_str(),
					{ 120, 255, 120 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			}
			return true;
		}

		Vector vEnemyGoal = {};
		if (F::PasstimeController.GetGoalPos(iEnemyTeam, pCarrier->GetAbsOrigin(), vEnemyGoal) &&
			pCarrier->GetAbsOrigin().DistTo(vEnemyGoal) <= 850.f)
		{
			m_sCaptureStatus = L"Defend";
			vOut = vEnemyGoal;
			return true;
		}

		m_sCaptureStatus = L"Carrier";
		vOut = pCarrier->GetAbsOrigin();
		return true;
	}

	if (F::PasstimeController.GetBallPos(vOut))
	{
		m_sCaptureStatus = L"Ball";
		if (Vars::Debug::Logging.Value)
		{
			SDK::Output("NavBotCapture", std::format(
				"GetPasstimeGoal: chasing loose ball at ({:.0f},{:.0f},{:.0f})",
				vOut.x, vOut.y, vOut.z).c_str(),
				{ 120, 255, 120 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}
		return true;
	}

	if (Vars::Debug::Logging.Value)
		SDK::Output("NavBotCapture", "GetPasstimeGoal: failed to resolve any passtime objective", { 255, 140, 140 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	return false;
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
#else
	(void)vSpot;
	(void)iPointIdx;
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

bool CNavBotCapture::Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tCaptureTimer;
	static Vector vPreviousTarget;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::CaptureObjectives))
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("NavBotCapture", "Capture.Run: CaptureObjectives preference disabled", { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return false;
	}

	if (const auto& pGameRules = I::TFGameRules())
	{
		if (!((pGameRules->m_iRoundState() == GR_STATE_RND_RUNNING || pGameRules->m_iRoundState() == GR_STATE_STALEMATE) && !pGameRules->m_bInWaitingForPlayers())
			|| pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN
			|| (pGameRules->m_bPlayingSpecialDeliveryMode() && !F::GameObjectiveController.m_bDoomsday))
		{
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", std::format("Capture.Run: game rules prevented capture (roundState={}, waiting={}, teamwin={}, specialDelivery={})",
					pGameRules->m_iRoundState(), pGameRules->m_bInWaitingForPlayers(), pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN, pGameRules->m_bPlayingSpecialDeliveryMode()).c_str(), { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return false;
		}
	}

	if (!tCaptureTimer.Check(2.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture;

	// Priority too high, don't try
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Capture)
		return false;

	int iOurTeam = pLocal->m_iTeamNum();
	int iEnemyTeam = iOurTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
	m_bOverwriteCapture = false;
	m_bWalkTo = false;

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	bool bGotTarget = false;

	// Where we want to go
	Vector vTarget;

	// Run logic
	switch (F::GameObjectiveController.m_eGameMode)
	{
	case TF_GAMETYPE_CTF:
		bGotTarget = GetCtfGoal(pLocal, iOurTeam, iEnemyTeam, vTarget);
		break;
	case TF_GAMETYPE_CP:
		bGotTarget = GetControlPointGoal(vLocalOrigin, iOurTeam, vTarget);
		break;
	case TF_GAMETYPE_ESCORT:
		bGotTarget = GetPayloadGoal(pLocal->GetRefEHandle(), vLocalOrigin, iOurTeam, vTarget);
		break;
	case TF_GAMETYPE_PASSTIME:
		bGotTarget = GetPasstimeGoal(pLocal, iOurTeam, iEnemyTeam, vTarget);
		break;
	default:
		if (F::GameObjectiveController.m_bDoomsday)
			bGotTarget = GetDoomsdayGoal(pLocal, iOurTeam, iEnemyTeam, vTarget);
		else if (F::GameObjectiveController.m_bHaarp)
			bGotTarget = GetCtfGoal(pLocal, iOurTeam, iEnemyTeam, vTarget);
		break;
	}

	// Overwritten, for example because we are currently on the payload, cancel any sort of pathing and return true
	if (m_bOverwriteCapture)
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("NavBotCapture", "Capture.Run: overwritten capture (player is on objective or close enough)", { 150, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);

		auto DoLook = [&](const Vec3& vTarget, bool bTargetValid) -> void
			{
				if (G::Attacking == 1)
				{
					F::BotUtils.InvalidateLLAP();
					return;
				}

				auto eLook = Vars::Misc::Movement::NavEngine::LookAtPath.Value;
				bool bSilent = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Silent || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;
				bool bLegit = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Legit || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;

				if (eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Off)
				{
					F::BotUtils.InvalidateLLAP();
					return;
				}

				if (bSilent && G::AntiAim)
				{
					F::BotUtils.InvalidateLLAP();
					return;
				}

				if (bLegit) F::BotUtils.LookLegit(pLocal, pCmd, bTargetValid ? vTarget : Vec3{}, bSilent);
				else if (bTargetValid)
				{
					F::BotUtils.InvalidateLLAP();
					F::BotUtils.LookAtPath(pCmd, Vec2(vTarget.x, vTarget.y), pLocal->GetEyePosition(), bSilent);
				}
				else F::BotUtils.InvalidateLLAP();
			};

		if (F::NavEngine.IsPathing()) F::NavEngine.CancelPath();
		if (m_bWalkTo)
		{
			DoLook(vTarget, true);
			SDK::WalkTo(pCmd, pLocal, vTarget);
		}
		else DoLook({}, false);

		return true;
	}

	// No target, bail and set on cooldown
	if (!bGotTarget)
	{
		tCaptureTimer.Update();
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture;
	}

	if (Vars::Debug::Info.Value)
	{
		G::BoxStorage.emplace_back(vTarget, Vec3(-16.0f, -16.0f, -16.0f), Vec3(16.0f, 16.0f, 16.0f), Vec3(), I::GlobalVars->curtime + 2.1f, Color_t(255, 255, 255, 180), Color_t(0, 0, 0, 0), true);
	}

	if (Vars::Debug::Logging.Value && F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_PASSTIME)
	{
		static Timer tRunLogTimer{};
		if (tRunLogTimer.Run(0.5f))
		{
			SDK::Output("NavBotCapture", std::format(
				"Run: passtime bGotTarget={} status={} overwrite={} walkTo={} target=({:.0f},{:.0f},{:.0f}) priority={}",
				bGotTarget ? 1 : 0,
				std::string(m_sCaptureStatus.begin(), m_sCaptureStatus.end()),
				m_bOverwriteCapture ? 1 : 0,
				m_bWalkTo ? 1 : 0,
				vTarget.x, vTarget.y, vTarget.z,
				int(F::NavEngine.m_eCurrentPriority)).c_str(),
				{ 180, 220, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}
	}

	if (F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_PASSTIME
		&& pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_PASSTIME_GUN
		&& pLocal->m_bHasPasstimeBall())
	{
		if (F::AimbotProjectile.AimPasstimePass(pLocal, pWeapon, pCmd))
		{
			if (F::NavEngine.IsPathing())
				F::NavEngine.CancelPath();
			return true;
		}
	}

	// If priority is not capturing, or we have a new target, try to path there
	const bool bPasstimeCarrier = F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_PASSTIME && pLocal->m_bHasPasstimeBall();
	const float flRetargetThresholdSq = bPasstimeCarrier ? pow(180.0f, 2) : 256.0f;
	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::Capture || vTarget.DistToSqr(vPreviousTarget) > flRetargetThresholdSq)
	{
		bool bNavOk = F::NavEngine.NavTo(vTarget, PriorityListEnum::Capture, true, !F::NavEngine.IsPathing());
		if (bNavOk)
		{
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", "Capture.Run: NavTo succeeded, started capture path", { 150, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			vPreviousTarget = vTarget;
			return true;
		}
		else
		{
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", "Capture.Run: NavTo failed for capture target", { 255, 100, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			tCaptureTimer.Update();
		}
	}

	return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture;
}

void CNavBotCapture::Reset()
{
	m_vCurrentCaptureSpot.reset();
	m_vCurrentCaptureCenter.reset();
	ReleaseCaptureSpotClaim();
}
