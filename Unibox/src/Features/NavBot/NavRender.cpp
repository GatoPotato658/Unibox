#include "NavRender.h"
#include "NavBotCore.h"
#include "NavEngine.h"
#include "Hazards.h"
#include "BotUtils.h"
#include "Objectives.h"
#include "Jobs/NavBotJobs.h"
#include "../FollowBot/FollowBot.h"
#include "../CritHack/CritHack.h"
#include "../ImGui/IndicatorPanel.h"

static constexpr Color_t kColorBlocked = { 255, 0, 0, 255 };
static constexpr Color_t kColorHazardSentry = { 255, 0, 0, 255 };
static constexpr Color_t kColorHazardEnemy = { 255, 128, 0, 255 };
static constexpr Color_t kColorHazardSticky = { 255, 255, 0, 255 };
static constexpr Color_t kColorEngieHelp = { 0, 200, 255, 255 };
static constexpr Color_t kColorEngieSpot = { 255, 255, 255, 100 };
static constexpr Color_t kColorEngieSpotCurrent = { 0, 255, 0, 255 };
static constexpr Color_t kColorEngieSpotFill = { 0, 255, 0, 50 };
static constexpr Color_t kColorEngieFocus = { 255, 255, 0, 255 };
static constexpr Color_t kColorHullClear = { 0, 255, 0, 255 };
static constexpr Color_t kColorHullBlocked = { 255, 0, 0, 255 };
static constexpr Color_t kColorHullGap = { 255, 128, 0, 255 };
static constexpr Color_t kColorHullSkipped = { 128, 128, 128, 100 };
static constexpr Color_t kColorHullTight = { 255, 255, 0, 255 };
static constexpr Color_t kColorHullSnug = { 200, 255, 120, 255 };
static constexpr Color_t kColorHullAdjusted = { 0, 200, 255, 255 };
static constexpr Color_t kColorDangerLow = { 255, 200, 0, 80 };
static constexpr Color_t kColorDangerNormal = { 255, 140, 0, 90 };
static constexpr Color_t kColorDangerSticky = { 255, 50, 50, 90 };
static constexpr Color_t kColorJumpLine = { 255, 255, 0, 255 };
static constexpr Color_t kColorJumpPeak = { 255, 0, 0, 255 };
static constexpr Color_t kColorJumpLanding = { 0, 0, 255, 255 };
static constexpr Color_t kColorCaptureTarget = { 255, 255, 255, 180 };
static constexpr Color_t kColorPasstimeGoal = { 255, 255, 255, 180 };
static constexpr Color_t kColorPasstimeGoalDisabled = { 255, 80, 80, 180 };
static constexpr Color_t kColorPasstimeGoalNeutral = { 80, 255, 120, 180 };
static constexpr Color_t kColorLookX = { 255, 0, 0, 255 };
static constexpr Color_t kColorLookY = { 0, 255, 0, 255 };
static constexpr Color_t kColorLookZ = { 0, 0, 255, 255 };
static constexpr Color_t kColorClear = { 0, 0, 0, 0 };

static void DrawMarker(const Vector& vPos, const Vector& vExtent, Color_t tColor, bool bWireframe = false)
{
	H::Draw.RenderBox(vPos, -vExtent, vExtent, Vector(), tColor, false);
	if (bWireframe)
		H::Draw.RenderWireframeBox(vPos, -vExtent, vExtent, Vector(), tColor, false);
}

static void DrawHull(const Vector& vPos, const Vector& vMins, const Vector& vMaxs, Color_t tColor)
{
	H::Draw.RenderWireframeBox(vPos, vMins, vMaxs, Vector(), tColor, false);
}

static void DrawAreaOutline(CNavArea* pArea, Color_t tColor)
{
	H::Draw.RenderLine(pArea->m_vNwCorner, pArea->GetNeCorner(), tColor, true);
	H::Draw.RenderLine(pArea->m_vNwCorner, pArea->GetSwCorner(), tColor, true);
	H::Draw.RenderLine(pArea->GetNeCorner(), pArea->m_vSeCorner, tColor, true);
	H::Draw.RenderLine(pArea->GetSwCorner(), pArea->m_vSeCorner, tColor, true);
}

static float OverlayExpireTime()
{
	return I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f;
}

void CNavRender::DrawPath()
{
	auto pCrumbs = F::NavEngine.GetCrumbs();
	if (pCrumbs->empty())
		return;

	const auto& tColor = Vars::Colors::NavbotPath.Value;
	for (size_t i = 0; i + 1 < pCrumbs->size(); ++i)
		H::Draw.RenderLine((*pCrumbs)[i].m_vPos, (*pCrumbs)[i + 1].m_vPos, tColor, false);
	for (const auto& tCrumb : *pCrumbs)
		DrawMarker(tCrumb.m_vPos, Vector(3.f, 3.f, 3.f), tColor);
}

void CNavRender::DrawLocalArea(CTFPlayer* pLocal)
{
	const Vector vOrigin = pLocal->GetAbsOrigin();
	auto pArea = F::NavEngine.GetLocalNavArea(vOrigin);
	if (!pArea)
		return;

	auto vEdge = pArea->GetNearestPoint(Vector2D(vOrigin.x, vOrigin.y));
	vEdge.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	DrawMarker(vEdge, Vector(4.f, 4.f, 1.f), kColorBlocked, true);
	DrawAreaOutline(pArea, Vars::Colors::NavbotArea.Value);
}

void CNavRender::DrawBlacklist()
{
	auto pMap = F::NavEngine.GetNavMap();
	if (!pMap)
		return;

	struct BlockedDraw_t
	{
		Vector m_vCurrent = {};
		Vector m_vNext = {};
		CNavArea* m_pSelfArea = nullptr;
	};

	std::vector<BlockedDraw_t> vBlocked;
	{
		std::lock_guard lock(pMap->m_mutex);
		vBlocked.reserve(pMap->m_mVischeckCache.size());
		const int iNow = I::GlobalVars->tickcount;
		for (auto& [tKey, tEntry] : pMap->m_mVischeckCache)
		{
			if (tEntry.m_eVischeckState != VischeckStateEnum::NotVisible) continue;
			if (tEntry.m_iExpireTick != 0 && tEntry.m_iExpireTick <= iNow) continue;

			BlockedDraw_t tDraw{};
			if (tEntry.m_bHasPoints)
			{
				tDraw.m_vCurrent = tEntry.m_tPoints.m_vCurrent;
				tDraw.m_vNext = tEntry.m_tPoints.m_vNext;
			}
			if (tKey.first == tKey.second)
				tDraw.m_pSelfArea = tKey.first;
			vBlocked.push_back(tDraw);
		}
	}

	for (const auto& tDraw : vBlocked)
	{
		if (tDraw.m_vCurrent.Length() > 0.f && tDraw.m_vNext.Length() > 0.f)
		{
			H::Draw.RenderLine(tDraw.m_vCurrent, tDraw.m_vNext, kColorBlocked, false);
			DrawMarker(tDraw.m_vCurrent, Vector(2.f, 2.f, 2.f), kColorBlocked);
			DrawMarker(tDraw.m_vNext, Vector(2.f, 2.f, 2.f), kColorBlocked);
		}

		if (tDraw.m_pSelfArea)
			DrawMarker(tDraw.m_pSelfArea->m_vCenter, Vector(6.f, 6.f, 2.f), kColorBlocked, true);
	}
}

void CNavRender::DrawHazards()
{
	auto pMap = F::NavEngine.GetNavMap();
	if (!pMap)
		return;

	for (const auto& [pArea, tHazard] : F::Hazards.GetHazardMap())
	{
		if (!pMap->IsAreaValid(pArea) || tHazard.m_flCost <= 0.f) continue;

		Color_t tColor;
		switch (tHazard.m_eKind)
		{
		case HazardKind::Sentry:
		case HazardKind::SentryMedium:
		case HazardKind::SentryLow:      tColor = kColorHazardSentry; break;
		case HazardKind::EnemyInvuln:
		case HazardKind::EnemyNormal:
		case HazardKind::EnemyDormant:
		case HazardKind::Boss:           tColor = kColorHazardEnemy; break;
		case HazardKind::Sticky:         tColor = kColorHazardSticky; break;
		default:                         tColor = Vars::Colors::NavbotBlacklist.Value; break;
		}

		DrawMarker(pArea->m_vCenter, Vector(6.f, 6.f, 6.f), tColor, tHazard.m_flCost >= HAZARD_COST_SENTRY * 0.9f);
	}
}

void CNavRender::DrawEngineer(CTFPlayer* pLocal)
{
	if (pLocal->m_iClass() != TF_CLASS_ENGINEER)
		return;

	if (F::NavBotEngineer.HasHelpTarget())
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(F::NavBotEngineer.GetHelpTarget());
		auto pBuilding = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
		if (pBuilding && pBuilding->IsBuilding() && !pBuilding->IsDormant())
			DrawHull(pBuilding->GetAbsOrigin(), pBuilding->m_vecMins(), pBuilding->m_vecMaxs(), kColorEngieHelp);
	}

	const auto& vSpots = F::NavBotEngineer.GetBuildingSpots();
	if (vSpots.empty())
		return;

	const Vector vSpotMins = { -30.f, -30.f, 0.f };
	const Vector vSpotMaxs = { 30.f, 30.f, 66.f };
	for (const auto& tSpot : vSpots)
	{
		const bool bIsCurrent = tSpot.m_vPos == F::NavBotEngineer.m_tCurrentBuildingSpot.m_vPos;
		DrawHull(tSpot.m_vPos, vSpotMins, vSpotMaxs, bIsCurrent ? kColorEngieSpotCurrent : kColorEngieSpot);
		if (bIsCurrent)
			H::Draw.RenderBox(tSpot.m_vPos, vSpotMins, vSpotMaxs, Vector(), kColorEngieSpotFill, false);
	}
	H::Draw.RenderWireframeSphere(F::NavBotEngineer.GetCurrentFocusPoint().m_vPos, 10.f, 36, 36, kColorEngieFocus);
}

void CNavRender::DrawFollowBotPath()
{
	const auto& vPath = F::FollowBot.GetCurrentPath();
	if (!Vars::Misc::Movement::FollowBot::DrawPath.Value || vPath.empty())
		return;

	for (size_t i = 0; i + 1 < vPath.size(); i++)
	{
		DrawMarker(vPath[i].m_vOrigin, Vector(1.f, 1.f, 1.f), Vars::Colors::FollowbotPathBox.Value);
		H::Draw.RenderLine(vPath[i].m_vOrigin, vPath[i + 1].m_vOrigin, Vars::Colors::FollowbotPathLine.Value, false);
	}
	DrawMarker(vPath.back().m_vOrigin, Vector(1.f, 1.f, 1.f), Vars::Colors::FollowbotPathBox.Value);
}

void CNavRender::DrawHullCheck()
{
	for (const auto& tSegment : F::NavEngine.GetHullDebug())
	{
		Color_t tColor = kColorHullClear;
		if (tSegment.m_bSkipped)
			tColor = kColorHullSkipped;
		else if (tSegment.m_bBlocked)
			tColor = kColorHullBlocked;
		else if (tSegment.m_bGap)
			tColor = kColorHullGap;
		else if (tSegment.m_bTight)
			tColor = kColorHullTight;
		else if (tSegment.m_bSnug)
			tColor = kColorHullSnug;
		else if (tSegment.m_bAdjusted)
			tColor = kColorHullAdjusted;

		H::Draw.RenderLine(tSegment.m_vStart, tSegment.m_vEnd, tColor, false);
		DrawHull(tSegment.m_vStart, tSegment.m_vMins, tSegment.m_vMaxs, tColor);
		DrawHull(tSegment.m_vEnd, tSegment.m_vMins, tSegment.m_vMaxs, tColor);

		if (tSegment.m_bBlocked)
		{
			DrawHull(tSegment.m_vHit, tSegment.m_vMins, tSegment.m_vMaxs, kColorHullBlocked);
			DrawMarker(tSegment.m_vHit, Vector(3.f, 3.f, 3.f), kColorHullBlocked, true);
		}
		else if (tSegment.m_bGap)
			DrawMarker(tSegment.m_vHit, Vector(3.f, 3.f, 3.f), kColorHullGap, true);
		else if (tSegment.m_bTight)
			DrawMarker(tSegment.m_vHit, Vector(3.f, 3.f, 3.f), kColorHullTight, true);
	}
}

void CNavRender::RenderWorld()
{
	DrawFollowBotPath();

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive() || !F::NavEngine.IsReady())
		return;

	if (Vars::Misc::Movement::NavEngine::HullCheck.Value)
		DrawHullCheck();

	const int iDraw = Vars::Misc::Movement::NavEngine::Draw.Value;
	if (!iDraw)
		return;

	if (iDraw & Vars::Misc::Movement::NavEngine::DrawEnum::Blacklist)
	{
		DrawHazards();
		DrawBlacklist();
	}
	if (iDraw & Vars::Misc::Movement::NavEngine::DrawEnum::Area)
		DrawLocalArea(pLocal);
	if (iDraw & Vars::Misc::Movement::NavEngine::DrawEnum::Path)
		DrawPath();
	DrawEngineer(pLocal);
}

void CNavRender::DrawDangerOverlay(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) || !pLocal->IsAlive() || !Vars::Misc::Movement::NavBot::DangerOverlay.Value)
		return;

	auto pMap = F::NavEngine.GetNavMap();
	if (!pMap)
		return;

	int iDrawn = 0;
	const float flMaxDist = Vars::Misc::Movement::NavBot::DangerOverlayMaxDist.Value;
	const float flMaxDistSqr = flMaxDist * flMaxDist;
	const float flExpire = OverlayExpireTime();
	for (const auto& [pArea, tData] : F::Hazards.GetHazardMap())
	{
		if (!pMap->IsAreaValid(pArea) || tData.m_flCost <= 0.f)
			continue;

		if (pArea->m_vCenter.DistToSqr(pLocal->GetAbsOrigin()) > flMaxDistSqr)
			continue;

		Color_t tColor = kColorDangerLow;
		if (tData.m_flCost >= HAZARD_COST_STICKY)
			tColor = kColorDangerSticky;
		else if (tData.m_flCost >= HAZARD_COST_ENEMY_NORMAL)
			tColor = kColorDangerNormal;

		G::SphereStorage.push_back({ pArea->m_vCenter, 24.f, 10, 10, flExpire, tColor, Color_t(), true });

		if (++iDrawn >= 64)
			break;
	}
}

void CNavRender::DrawJumpPrediction(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Movement::NavBot::Enabled.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value
		|| !pLocal->IsAlive() || F::FollowBot.m_bActive || !F::NavEngine.IsReady() || NavRuntime::IsMovementLocked(pLocal)
		|| F::BotUtils.m_vPredictedJumpPos.Length() <= 0.f)
		return;

	const float flExpire = OverlayExpireTime();
	G::LineStorage.push_back({ { pLocal->GetAbsOrigin(), F::BotUtils.m_vPredictedJumpPos }, flExpire, kColorJumpLine });
	G::SphereStorage.push_back({ F::BotUtils.m_vJumpPeakPos, 5.f, 10, 10, flExpire, kColorJumpPeak, kColorClear });
	G::SphereStorage.push_back({ F::BotUtils.m_vPredictedJumpPos, 5.f, 10, 10, flExpire, kColorJumpLanding, kColorClear });
}

void CNavRender::DrawCaptureTarget()
{
	if (I::GlobalVars->curtime >= F::NavBotCapture.m_flDebugTargetExpire)
		return;

	const Vector vExtent = { 16.f, 16.f, 16.f };
	G::BoxStorage.emplace_back(F::NavBotCapture.m_vDebugTarget, -vExtent, vExtent, Vec3(), OverlayExpireTime(), kColorCaptureTarget, kColorClear, true);
}

void CNavRender::DrawPasstimeGoals()
{
	if (F::GameObjectiveController.m_eGameMode != TF_GAMETYPE_PASSTIME)
		return;

	const float flExpire = OverlayExpireTime();
	for (const auto& tGoal : F::PasstimeController.GetGoalDebug())
	{
		Color_t tColor = kColorPasstimeGoal;
		if (tGoal.m_bDisabled)     tColor = kColorPasstimeGoalDisabled;
		else if (tGoal.m_bNeutral) tColor = kColorPasstimeGoalNeutral;

		G::BoxStorage.emplace_back(tGoal.m_vOrigin, tGoal.m_vMins - tGoal.m_vOrigin, tGoal.m_vMaxs - tGoal.m_vOrigin, Vec3(), flExpire, tColor, kColorClear, true);
	}
}

void CNavRender::DrawLookTarget()
{
	if (!Vars::Misc::Movement::BotUtils::LookAtPathDebug.Value || I::GlobalVars->curtime >= F::BotUtils.m_flDebugLookExpire)
		return;

	const Vector& vLook = F::BotUtils.m_vDebugLook;
	const float flExpire = OverlayExpireTime();
	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(10, 0, 0), vLook + Vec3(10, 0, 0)), flExpire, kColorLookX, false);
	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(0, 10, 0), vLook + Vec3(0, 10, 0)), flExpire, kColorLookY, false);
	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(0, 0, 10), vLook + Vec3(0, 0, 10)), flExpire, kColorLookZ, false);
}

void CNavRender::AddObjectiveMarker(const Vector& vPos, float flRadius, int iSegments, float flDuration, Color_t tColor)
{
	const float flExpire = I::GlobalVars->curtime + flDuration;
	for (auto& tMarker : m_vObjectiveMarkers)
	{
		if (tMarker.m_vPos.DistToSqr(vPos) <= 1.f && tMarker.m_flRadius == flRadius
			&& tMarker.m_tColor.r == tColor.r && tMarker.m_tColor.g == tColor.g && tMarker.m_tColor.b == tColor.b)
		{
			tMarker.m_flExpire = std::max(tMarker.m_flExpire, flExpire);
			return;
		}
	}
	m_vObjectiveMarkers.push_back({ vPos, flRadius, iSegments, flExpire, tColor });
}

void CNavRender::DrawObjectiveMarkers()
{
	const float flNow = I::GlobalVars->curtime;
	std::erase_if(m_vObjectiveMarkers, [flNow](const ObjectiveMarker_t& tMarker) { return tMarker.m_flExpire <= flNow || tMarker.m_flExpire > flNow + 10.f; });

	const float flExpire = OverlayExpireTime();
	for (const auto& tMarker : m_vObjectiveMarkers)
	{
		const Color_t tFace = { tMarker.m_tColor.r, tMarker.m_tColor.g, tMarker.m_tColor.b, 10 };
		const Color_t tEdge = { tMarker.m_tColor.r, tMarker.m_tColor.g, tMarker.m_tColor.b, 100 };
		G::SphereStorage.push_back({ tMarker.m_vPos, tMarker.m_flRadius, tMarker.m_iSegments, tMarker.m_iSegments, flExpire, tFace, tEdge });
	}
}

void CNavRender::DrawOverlay(CTFPlayer* pLocal)
{
	if (!pLocal)
		return;

	DrawLookTarget();
	if (!Vars::Debug::Info.Value)
	{
		m_vObjectiveMarkers.clear();
		return;
	}

	DrawDangerOverlay(pLocal);
	DrawJumpPrediction(pLocal);
	DrawCaptureTarget();
	DrawPasstimeGoals();
	DrawObjectiveMarkers();
}

static std::wstring BuildJobLabel()
{
	const auto AppendStatus = [](std::wstring& sLabel, const std::wstring& sStatus)
		{
			if (sStatus.empty())
				return;

			sLabel += L" (";
			sLabel += sStatus;
			sLabel += L')';
		};

	switch (F::NavEngine.m_eCurrentPriority)
	{
	case PriorityListEnum::Patrol:
	{
		if (!F::NavBotRoam.m_bDefending)
			return L"Patrol";

		std::wstring sLabel = L"Defend";
		AppendStatus(sLabel, F::NavBotCapture.m_sCaptureStatus);
		return sLabel;
	}
	case PriorityListEnum::LowPrioGetHealth:
		return L"Get health (Low-Prio)";
	case PriorityListEnum::StayNear:
		return std::format(L"Stalk enemy ({})", F::NavBotStayNear.m_sFollowTargetName);
	case PriorityListEnum::RunReload:
		return L"Run reload";
	case PriorityListEnum::RunSafeReload:
		return L"Run safe reload";
	case PriorityListEnum::SnipeSentry:
		return L"Snipe sentry";
	case PriorityListEnum::GetAmmo:
		return L"Get ammo";
	case PriorityListEnum::Capture:
	{
		std::wstring sLabel = L"Capture";
		AppendStatus(sLabel, F::NavBotCapture.m_sCaptureStatus);
		return sLabel;
	}
	case PriorityListEnum::MeleeAttack:
		return L"Melee";
	case PriorityListEnum::Engineer:
	{
		switch (F::NavBotEngineer.m_eTaskStage)
		{
		case EngineerTaskStageEnum::BuildSentry:
			return L"Engineer (Build sentry)";
		case EngineerTaskStageEnum::BuildDispenser:
			return L"Engineer (Build dispenser)";
		case EngineerTaskStageEnum::SmackSentry:
			return L"Engineer (Smack sentry)";
		case EngineerTaskStageEnum::SmackDispenser:
			return L"Engineer (Smack dispenser)";
		case EngineerTaskStageEnum::HelpBuilding:
			return L"Engineer (Help build)";
		default:
			return L"Engineer (None)";
		}
	}
	case PriorityListEnum::GetHealth:
		return L"Get health";
	case PriorityListEnum::EscapeSpawn:
		return L"Escape spawn";
	case PriorityListEnum::EscapeDanger:
	{
		std::wstring sLabel = L"Escape danger";
		AppendStatus(sLabel, F::NavBotDanger.m_sDangerStatus);
		return sLabel;
	}
	case PriorityListEnum::Followbot:
		return L"FollowBot";
	case PriorityListEnum::MVMTank:
		return L"MvM tank";
	case PriorityListEnum::MVMCombat:
		return L"MvM combat";
	case PriorityListEnum::MVMMoney:
		return L"MvM money";
	case PriorityListEnum::MVMFrontline:
		return L"MvM frontline";
	case PriorityListEnum::MVMSniper:
		return L"MvM sniper";
	case PriorityListEnum::BuyBot:
		return L"Buy bot";
	default:
		return L"None";
	}
}

void CNavRender::CacheIndicators(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot))
	{
		m_tDrawCache.Set({});
		return;
	}

	if (Vars::Misc::Movement::NavBot::Enabled.Value && !Vars::Misc::Movement::NavEngine::Enabled.Value && I::EngineClient->IsInGame())
	{
		std::vector<NavIndicatorLine_t> vDisabled = {};
		vDisabled.push_back({ "NAV ENGINE DISABLED", Vars::Menu::Theme::Active.Value });
		m_tDrawCache.Set(std::move(vDisabled));
		return;
	}

	const char* szLevelName = I::EngineClient->GetLevelName();
	if (Vars::Misc::Movement::NavEngine::Enabled.Value && I::EngineClient->IsInGame() && szLevelName && *szLevelName
		&& !F::NavEngine.IsNavMeshLoaded() && !F::FollowBot.m_bActive)
	{
		std::vector<NavIndicatorLine_t> vMissing = {};
		vMissing.push_back({ "INSTALL/GENERATE A NAVMESH FOR THIS", Vars::Menu::Theme::Active.Value });
		vMissing.push_back({ "MAP, INTO PATH:", Vars::Menu::Theme::Active.Value });

		const std::string sPath = F::NavEngine.GetNavInstallPath();
		if (sPath.empty())
			vMissing.push_back({ "tf/maps/<map>.nav", Vars::Menu::Theme::Active.Value });
		else
		{
			const size_t uSlash = sPath.find_last_of("/\\");
			if (uSlash == std::string::npos)
				vMissing.push_back({ sPath, Vars::Menu::Theme::Active.Value });
			else
			{
				vMissing.push_back({ sPath.substr(0, uSlash + 1), Vars::Menu::Theme::Active.Value });
				vMissing.push_back({ sPath.substr(uSlash + 1), Vars::Menu::Theme::Active.Value });
			}
		}

		m_tDrawCache.Set(std::move(vMissing));
		return;
	}

	if (!pLocal || !pLocal->IsAlive())
	{
		m_tDrawCache.Set({});
		return;
	}

	const bool bIsReady = F::NavEngine.IsReady();
	if (!Vars::Debug::Info.Value && !bIsReady)
	{
		m_tDrawCache.Set({});
		return;
	}

	const auto& tColor = F::NavEngine.IsPathing() ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	const auto& tReadyColor = bIsReady ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	int iInSpawn = -1;
	int iAreaFlags = -1;
	if (F::NavEngine.IsNavMeshLoaded())
	{
		if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
		{
			iAreaFlags = pLocalArea->m_iTFAttributeFlags;
			iInSpawn = iAreaFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED);
		}
	}

	std::vector<NavIndicatorLine_t> vCachedLines = {};
	vCachedLines.push_back({ std::format("Job: {} {}", SDK::ConvertWideToUTF8(BuildJobLabel()), F::CritHack.m_bForce ? "(Crithack on)" : ""), tColor });

	if (F::NavEngine.IsPathing())
	{
		const float flDist = pLocal->GetAbsOrigin().DistTo(F::NavEngine.m_vLastDestination);
		vCachedLines.push_back({ std::format("Nodes: {} (Dist: {:.0f})", F::NavEngine.GetCrumbs()->size(), flDist), tColor });
	}

	const float flIdleTime = SDK::PlatFloatTime() - F::NavBotCore.GetIdleLastUpdate();
	if (flIdleTime > 2.0f && F::NavEngine.IsPathing())
		vCachedLines.push_back({ std::format("Stuck: {:.1f}s", flIdleTime), Vars::Menu::Theme::Active.Value });

	if (!F::NavEngine.IsPathing() && !F::NavEngine.m_sLastFailureReason.empty())
		vCachedLines.push_back({ std::format("Failed: {}", F::NavEngine.m_sLastFailureReason), Vars::Menu::Theme::Active.Value });

	if (Vars::Debug::Info.Value)
	{
		vCachedLines.push_back({ std::format("Is ready: {}", static_cast<int>(bIsReady)), tReadyColor });
		vCachedLines.push_back({ std::format("Priority: {}", static_cast<int>(F::NavEngine.m_eCurrentPriority)), tReadyColor });
		vCachedLines.push_back({ std::format("In spawn: {}", iInSpawn), tReadyColor });
		vCachedLines.push_back({ std::format("Area flags: {}", iAreaFlags), tReadyColor });

		if (F::NavEngine.IsNavMeshLoaded())
		{
			vCachedLines.push_back({ std::format("Map: {}", F::NavEngine.GetNavFilePath()), tReadyColor });
			if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
				vCachedLines.push_back({ std::format("Area ID: {}", pLocalArea->m_uId), tReadyColor });
			vCachedLines.push_back({ std::format("Total areas: {}", F::NavEngine.GetNavFile()->m_vAreas.size()), tReadyColor });
		}

		if (F::NavEngine.IsPathing() || F::NavEngine.m_vLastDestination.Length() > 0.f)
		{
			const auto& vDest = F::NavEngine.m_vLastDestination;
			vCachedLines.push_back({ std::format("Dest: {:.0f}, {:.0f}, {:.0f}", vDest.x, vDest.y, vDest.z), tColor });
		}

		const bool bIsIdle = F::NavEngine.m_eCurrentPriority == PriorityListEnum::None || !F::NavEngine.IsPathing();
		vCachedLines.push_back({ std::format("Idle: {} ({:.1f}s)", bIsIdle ? "Yes" : "No", std::max(0.f, flIdleTime)), bIsIdle ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value });
	}

	m_tDrawCache.Set(std::move(vCachedLines));
}

void CNavRender::DrawIndicators()
{
	static IndicatorAnim_t tAnim;
	static std::vector<NavIndicatorLine_t> s_vLastValidLines;

	const std::vector<NavIndicatorLine_t> vCachedLines = m_tDrawCache.Get();
	if (!vCachedLines.empty())
		s_vLastValidLines = vCachedLines;

	const bool bEnabled = (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) != 0;
	const bool bVisible = bEnabled && I::EngineClient->IsInGame() && !vCachedLines.empty();

	bool bOpening = true;
	const float flAnim = tAnim.Update(bVisible, bOpening);
	if (!tAnim.ShouldDraw(bVisible))
		return;

	const auto& vDraw = !vCachedLines.empty() ? vCachedLines : s_vLastValidLines;

	int x = Vars::Menu::NavBotDisplay.Value.x;
	const int y = Vars::Menu::NavBotDisplay.Value.y + 8;
	const auto& tFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = tFont.m_nTall + H::Draw.IndicatorScale(1);
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	EAlign eAlign = ALIGN_TOP;
	if (x <= 100 + H::Draw.IndicatorScale(50, Scale_Round))
	{
		x -= H::Draw.IndicatorScale(42, Scale_Round);
		eAlign = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.IndicatorScale(50, Scale_Round))
	{
		x += H::Draw.IndicatorScale(42, Scale_Round);
		eAlign = ALIGN_TOPRIGHT;
	}

	const int iVtxStart = pDrawList->VtxBuffer.Size;
	const int iCmdStart = pDrawList->CmdBuffer.Size;

	for (size_t i = 0; i < vDraw.size(); i++)
	{
		const int iY = y + static_cast<int>(i) * nTall;
		DrawIndicatorText(pDrawList, x, iY, vDraw[i].m_tColor, Vars::Menu::Theme::Background.Value, eAlign, vDraw[i].m_sText);
	}

	TransformIndicatorVertices(pDrawList, iVtxStart, pDrawList->VtxBuffer.Size, iCmdStart, pDrawList->CmdBuffer.Size, flAnim, bOpening, ImGui::GetIO().DisplaySize);
}
