#include "NavEngine.h"
#include "Hazards.h"
#include "BotUtils.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>

static std::atomic<float> s_flNavTickInterval{ 1.0f / 66.0f };

static float GetAreaVerticalOutside(const CNavArea& tArea, const Vector& vPos)
{
	const float flBelow = std::max(tArea.m_flMinZ - vPos.z, 0.0f);
	const float flAbove = std::max(vPos.z - tArea.m_flMaxZ, 0.0f);
	return flBelow + flAbove;
}

static Vector GetSafePointOnArea(CNavArea* pArea, const Vector& vPos)
{
	if (!pArea) return vPos;

	constexpr float flMargin = HALF_PLAYER_WIDTH + 2.0f;
	auto Inset = [flMargin](float flValue, float flMin, float flMax)
		{
			if (flMax - flMin <= flMargin * 2.0f)
				return (flMin + flMax) * 0.5f;
			return std::clamp(flValue, flMin + flMargin, flMax - flMargin);
		};

	const float flX = Inset(vPos.x, pArea->m_vNwCorner.x, pArea->m_vSeCorner.x);
	const float flY = Inset(vPos.y, pArea->m_vNwCorner.y, pArea->m_vSeCorner.y);
	return { flX, flY, pArea->GetZ(flX, flY) };
}

bool CMap::CanFallToNavArea(const Vector& vPos, const CNavArea& tArea)
{
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	const float flNearestX = std::clamp(vPos.x, tArea.m_vNwCorner.x, tArea.m_vSeCorner.x);
	const float flNearestY = std::clamp(vPos.y, tArea.m_vNwCorner.y, tArea.m_vSeCorner.y);
	const float flAreaZ = tArea.GetZ(flNearestX, flNearestY);
	const float flDelta = vPos.z - flAreaZ;
	if (flDelta < -18.0f)
		return false;

	Vector vStart = vPos;
	vStart.z += 2.0f;
	const Vector vEnd(flNearestX, flNearestY, flAreaZ + 2.0f);
	CGameTrace trace{};
	CTraceFilterNavigation filter(pLocal);
	SDK::TraceHull(vStart, vEnd, pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &filter, &trace);
	return !trace.startsolid && !trace.allsolid
		&& (trace.fraction >= 1.0f || (std::fabs(trace.endpos.z - vEnd.z) <= 18.0f
			&& trace.endpos.DistTo2DSqr(vEnd) <= HALF_PLAYER_WIDTH * HALF_PLAYER_WIDTH));
}

static float GetNearestAreaScore(const CNavArea& tArea, const Vector& vPos, bool bLocalOrigin)
{
	const float flNearestX = std::clamp(vPos.x, tArea.m_vNwCorner.x, tArea.m_vSeCorner.x);
	const float flNearestY = std::clamp(vPos.y, tArea.m_vNwCorner.y, tArea.m_vSeCorner.y);
	const float flNearestZ = tArea.GetZ(flNearestX, flNearestY);
	const float flVerticalToSurface = std::fabs(flNearestZ - vPos.z);
	const float flVerticalOutside = GetAreaVerticalOutside(tArea, vPos);

	const float flDx = flNearestX - vPos.x;
	const float flDy = flNearestY - vPos.y;
	const float flPlanarDistSqr = flDx * flDx + flDy * flDy;

	const bool bOverlappingStrict = tArea.IsOverlapping(vPos);
	const bool bOverlapping = bLocalOrigin ? tArea.IsOverlapping(vPos, HALF_PLAYER_WIDTH) : bOverlappingStrict;
	const bool bTightOverlap = bOverlappingStrict && flVerticalOutside <= 24.0f && flVerticalToSurface <= 45.0f;

	float flScore = flPlanarDistSqr + (flVerticalToSurface * flVerticalToSurface * 6.0f) + (flVerticalOutside * flVerticalOutside * (bLocalOrigin ? 18.0f : 10.0f));
	if (bOverlapping) flScore *= bLocalOrigin ? 0.45f : 0.7f;
	if (bTightOverlap) flScore *= 0.15f;
	else if (bLocalOrigin && bOverlapping && flVerticalOutside > PLAYER_JUMP_HEIGHT)
		flScore += flVerticalOutside * flVerticalOutside * 8.0f;

	if (bLocalOrigin)
	{
		const float flDelta = vPos.z - flNearestZ;
		if (flDelta < -18.0f)
			flScore += flDelta * flDelta * 28.0f;
		else if (flDelta < -6.0f)
			flScore += flDelta * flDelta * 10.0f;
	}

	return flScore;
}

int CMap::Solve(CNavArea* pStart, CNavArea* pEnd, const SolveContext& tCtx, std::vector<CNavArea*>& vOutPath, float* pflCost)
{
	vOutPath.clear();

	if (!pStart || !pEnd || m_navfile.m_vAreas.empty()) return 2;

	if (pStart == pEnd)
	{
		vOutPath.push_back(pStart);
		if (pflCost) *pflCost = 0.f;
		return 3;
	}

	if (m_vPathNodes.size() != m_navfile.m_vAreas.size())
		m_vPathNodes.assign(m_navfile.m_vAreas.size(), {});

	m_iQueryId++;

	const size_t uStartIdx = pStart - &m_navfile.m_vAreas[0];
	const size_t uEndIdx = pEnd - &m_navfile.m_vAreas[0];
	if (uStartIdx >= m_vPathNodes.size() || uEndIdx >= m_vPathNodes.size())
		return 2;

	m_bSkipSpawn = !(pStart->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE))
		&& !(pEnd->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE));

	PathNode_t& tStart = m_vPathNodes[uStartIdx];
	tStart.m_g = 0.f;
	tStart.m_f = pStart->m_vCenter.DistTo(pEnd->m_vCenter);
	tStart.m_pParent = nullptr;
	tStart.m_iQueryId = m_iQueryId;

	using NodePair = std::pair<float, size_t>;
	std::priority_queue<NodePair, std::vector<NodePair>, std::greater<NodePair>> openSet;
	openSet.push({ tStart.m_f, uStartIdx });

	std::vector<AdjacentEntry> vNeighbors;
	vNeighbors.reserve(8);

	while (!openSet.empty())
	{
		const auto [flCurrentF, uCurrentIdx] = openSet.top();
		openSet.pop();

		if (tCtx.m_pCancel && tCtx.m_pCancel->load(std::memory_order_relaxed))
			return 2;

		PathNode_t& tCurrent = m_vPathNodes[uCurrentIdx];
		if (flCurrentF > tCurrent.m_f) continue;

		if (uCurrentIdx == uEndIdx)
		{
			if (pflCost) *pflCost = tCurrent.m_g;
			CNavArea* p = pEnd;
			while (p)
			{
				vOutPath.push_back(p);
				size_t i = p - &m_navfile.m_vAreas[0];
				p = m_vPathNodes[i].m_pParent;
			}
			std::reverse(vOutPath.begin(), vOutPath.end());
			return 0;
		}

		CNavArea* pCurrentArea = &m_navfile.m_vAreas[uCurrentIdx];
		vNeighbors.clear();
		GetAdjacent(pCurrentArea, tCtx, vNeighbors);

		for (const auto& tEdge : vNeighbors)
		{
			CNavArea* pNextArea = tEdge.m_pArea;
			const size_t uNextIdx = pNextArea - &m_navfile.m_vAreas[0];
			PathNode_t& tNext = m_vPathNodes[uNextIdx];

			if (tNext.m_iQueryId != m_iQueryId)
			{
				tNext.m_g = std::numeric_limits<float>::max();
				tNext.m_f = std::numeric_limits<float>::max();
				tNext.m_pParent = nullptr;
				tNext.m_iQueryId = m_iQueryId;
			}

			const float flTentativeG = tCurrent.m_g + tEdge.m_flCost;
			if (flTentativeG < tNext.m_g)
			{
				tNext.m_pParent = pCurrentArea;
				tNext.m_g = flTentativeG;
				tNext.m_f = flTentativeG + pNextArea->m_vCenter.DistTo(pEnd->m_vCenter);
				openSet.push({ tNext.m_f, uNextIdx });
			}
		}
	}

	return 1;
}

SolveContext CMap::BuildSolveContext()
{
	SolveContext tCtx{};
	auto pLocal = H::Entities.GetLocal();
	tCtx.m_iTeam = pLocal ? pLocal->m_iTeamNum() : 0;
	tCtx.m_iTickcount = I::GlobalVars ? I::GlobalVars->tickcount : 0;
	s_flNavTickInterval.store(I::GlobalVars ? I::GlobalVars->interval_per_tick : (1.0f / 66.0f), std::memory_order_relaxed);
	tCtx.m_iVischeckCacheSeconds = std::min(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value, 45);
	tCtx.m_bIgnoreTraces = F::NavEngine.m_bIgnoreTraces;
	if (pLocal)
	{
		auto pWeaponEntity = pLocal->m_hActiveWeapon().Get();
		tCtx.m_bCanJump = NavRuntime::CanUseNavJump(pLocal, pWeaponEntity ? pWeaponEntity->As<CTFWeaponBase>() : nullptr);
	}
	tCtx.m_tPolicy = NavPolicy::Snapshot(tCtx.m_iTeam);
	F::Hazards.SnapshotCosts(tCtx.m_mHazardCosts);
	return tCtx;
}

int CMap::SolveCrumbs(const Vector& vStart, CNavArea* pStartArea, const Vector& vEnd, CNavArea* pEndArea,
	const SolveContext& tCtx, std::vector<CachedPathCrumb_t>& vOutPath, float* pflCost)
{
	vOutPath.clear();
	if (!pStartArea || !pEndArea || !IsAreaValid(pStartArea) || !IsAreaValid(pEndArea)) return 2;

	std::vector<CNavArea*> vAreas;
	float flAreaCost = 0.f;
	const int iResult = Solve(pStartArea, pEndArea, tCtx, vAreas, &flAreaCost);
	if ((iResult != 0 && iResult != 3) || vAreas.empty()) return iResult == 3 ? 3 : 1;
	if (pflCost) *pflCost = flAreaCost;
	constexpr float flCrumbSpacing = 150.0f;

	auto AppendCrumb = [&vOutPath](CachedPathCrumb_t tCrumb)
		{
			if (!vOutPath.empty() && vOutPath.back().m_vPos.DistToSqr(tCrumb.m_vPos) < 1.f)
			{
				if (tCrumb.m_pNavArea)
					vOutPath.back().m_pNavArea = tCrumb.m_pNavArea;
				if (tCrumb.m_bRequiresDrop)
				{
					vOutPath.back().m_bRequiresDrop = true;
					vOutPath.back().m_flDropHeight = tCrumb.m_flDropHeight;
					vOutPath.back().m_flApproachDistance = tCrumb.m_flApproachDistance;
					vOutPath.back().m_vApproachDir = tCrumb.m_vApproachDir;
				}
				return;
			}
			vOutPath.push_back(std::move(tCrumb));
		};
	auto AppendAreaSegment = [&](const Vector& vFrom, const Vector& vTo, CNavArea* pArea, const DropdownHint_t* pDrop = nullptr)
		{
			const Vector vSegmentStart = GetSafePointOnArea(pArea, vFrom);
			const Vector vSegmentEnd = pDrop ? vTo : GetSafePointOnArea(pArea, vTo);
			const Vector vDelta = vSegmentEnd - vSegmentStart;
			const int iSteps = std::max(static_cast<int>(std::ceil(vDelta.Length() / flCrumbSpacing)), 1);
			Vector vApproachDir = vDelta;
			vApproachDir.z = 0.f;
			if (vApproachDir.Normalize() <= 0.01f)
				vApproachDir = {};

			for (int iStep = 1; iStep <= iSteps; ++iStep)
			{
				CachedPathCrumb_t tCrumb{};
				tCrumb.m_pNavArea = pArea;
				tCrumb.m_vPos = vSegmentStart + vDelta * (static_cast<float>(iStep) / iSteps);
				tCrumb.m_vApproachDir = vApproachDir;
				if (pDrop && iStep == iSteps)
				{
					tCrumb.m_vPos = vSegmentEnd;
					tCrumb.m_bRequiresDrop = true;
					tCrumb.m_flDropHeight = pDrop->m_flDropHeight;
					tCrumb.m_flApproachDistance = pDrop->m_flApproachDistance;
					if (!pDrop->m_vApproachDir.IsZero())
						tCrumb.m_vApproachDir = pDrop->m_vApproachDir;
				}
				else
					tCrumb.m_vPos.z = pArea->GetZ(tCrumb.m_vPos.x, tCrumb.m_vPos.y);
				AppendCrumb(std::move(tCrumb));
			}
		};

	CachedPathCrumb_t tStart{};
	tStart.m_pNavArea = pStartArea;
	tStart.m_vPos = GetSafePointOnArea(pStartArea, vStart);
	AppendCrumb(tStart);
	Vector vAreaEntry = tStart.m_vPos;

	for (size_t i = 0; i + 1 < vAreas.size(); ++i)
	{
		CNavArea* pFrom = vAreas[i];
		CNavArea* pTo = vAreas[i + 1];
		const auto tKey = std::pair<CNavArea*, CNavArea*>(pFrom, pTo);
		const auto it = m_mVischeckCache.find(tKey);
		const NavPoints_t tPoints = it != m_mVischeckCache.end() ? it->second.m_tPoints : DeterminePoints(pFrom, pTo);
		const DropdownHint_t tDropdown = it != m_mVischeckCache.end() ? it->second.m_tDropdown : HandleDropdown(tPoints);

		AppendAreaSegment(vAreaEntry, tDropdown.m_vAdjustedPos, pFrom, tDropdown.m_bRequiresDrop ? &tDropdown : nullptr);

		CachedPathCrumb_t tAreaEntry{};
		tAreaEntry.m_pNavArea = pTo;
		tAreaEntry.m_vPos = GetSafePointOnArea(pTo, tPoints.m_vCenterNext);
		AppendCrumb(tAreaEntry);
		vAreaEntry = tAreaEntry.m_vPos;
	}

	AppendAreaSegment(vAreaEntry, vEnd, pEndArea);

	return pStartArea == pEndArea ? 3 : 0;
}

void CMap::GetAdjacent(CNavArea* pCurrentArea, const SolveContext& tCtx, std::vector<AdjacentEntry>& vOut)
{
	if (!pCurrentArea) return;

	const int iTeam = tCtx.m_iTeam;
	const int iNow = tCtx.m_iTickcount;
	const float flTickInterval = s_flNavTickInterval.load(std::memory_order_relaxed);
	const int iCacheExpiry = iNow + static_cast<int>(static_cast<float>(tCtx.m_iVischeckCacheSeconds) / flTickInterval);
	const int iUnreachableCacheExpiry = iNow + static_cast<int>(90.f / flTickInterval);

	auto LookupHazard = [&](CNavArea* pArea) -> float
		{
			auto it = tCtx.m_mHazardCosts.find(pArea);
			return it == tCtx.m_mHazardCosts.end() ? 0.f : it->second;
		};

	for (NavConnect_t& tConnection : pCurrentArea->m_vConnections)
	{
		CNavArea* pNextArea = tConnection.m_pArea;
		if (!pNextArea || pNextArea == pCurrentArea || !IsAreaValid(pNextArea))
			continue;

		if (!HasDirectConnection(pCurrentArea, pNextArea)) continue;
		if (!NavPolicy::IsAreaTraversable(*pNextArea, tCtx.m_tPolicy)) continue;

		const bool bTouchesSpawn = pCurrentArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)
			|| pNextArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE);

		if (!std::isfinite(LookupHazard(pNextArea)))
			continue;

		const auto tAreaBlockKey = std::pair<CNavArea*, CNavArea*>(pNextArea, pNextArea);
		if (auto itBlocked = m_mVischeckCache.find(tAreaBlockKey); itBlocked != m_mVischeckCache.end())
		{
			const auto& tEnt = itBlocked->second;
			if (tEnt.m_eVischeckState == VischeckStateEnum::NotVisible
				&& (tEnt.m_iExpireTick == 0 || tEnt.m_iExpireTick > iNow)
				&& tEnt.m_bStuckBlacklist)
				continue;
		}

		const auto tKey = std::pair<CNavArea*, CNavArea*>(pCurrentArea, pNextArea);
		CachedConnection_t& tEntry = m_mVischeckCache[tKey];
		const size_t uNavMeshHash = GetConnectionNavMeshHash(pCurrentArea, pNextArea);
		if (tEntry.m_uNavMeshHash != uNavMeshHash)
		{
			tEntry = {};
			tEntry.m_uNavMeshHash = uNavMeshHash;
		}
		const bool bValidCache = (tEntry.m_iExpireTick == 0 || tEntry.m_iExpireTick > iNow);

		NavPoints_t tPoints{};
		DropdownHint_t tDropdown{};
		float flBaseCost = std::numeric_limits<float>::max();
		bool bPassable = false;

		if (bValidCache && tEntry.m_eVischeckState == VischeckStateEnum::Visible && tEntry.m_bPassable
			&& std::isfinite(tEntry.m_flCachedCost) && tEntry.m_flCachedCost < std::numeric_limits<float>::max())
		{
			tPoints = tEntry.m_tPoints;
			tDropdown = tEntry.m_tDropdown;
			flBaseCost = tEntry.m_flCachedCost;
			bPassable = true;
		}
		else if (bValidCache && tEntry.m_eVischeckState == VischeckStateEnum::NotVisible && !tEntry.m_bPassable)
		{
			continue;
		}
		else
		{
			tPoints = DeterminePoints(pCurrentArea, pNextArea);
			tDropdown = HandleDropdown(tPoints);

			const float flUpDelta = tPoints.m_vCenterNext.z - tPoints.m_vCenter.z;

			if (!tCtx.m_bIgnoreTraces && (flUpDelta > PLAYER_JUMP_HEIGHT || flUpDelta < -PLAYER_DEATH_DROP_HEIGHT))
			{
				tEntry.m_iExpireTick = iUnreachableCacheExpiry;
				tEntry.m_eVischeckState = VischeckStateEnum::NotVisible;
				tEntry.m_bPassable = false;
				tEntry.m_bStuckBlacklist = false;
				tEntry.m_flCachedCost = std::numeric_limits<float>::max();
				tEntry.m_tPoints = tPoints;
				tEntry.m_tDropdown = tDropdown;
				continue;
			}

			NavPoints_t tCostPoints = tPoints;
			tCostPoints.m_vCenter = tDropdown.m_vAdjustedPos;
			bPassable = true;
			flBaseCost = EvaluateConnectionCost(pCurrentArea, pNextArea, tCostPoints, tDropdown, iTeam);

			tEntry.m_iExpireTick = iCacheExpiry;
			tEntry.m_eVischeckState = VischeckStateEnum::Visible;
			tEntry.m_bPassable = true;
			tEntry.m_tPoints = tPoints;
			tEntry.m_tDropdown = tDropdown;
			tEntry.m_flCachedCost = flBaseCost;
		}

		if (!bPassable || !std::isfinite(flBaseCost) || flBaseCost <= 0.f)
			continue;

		float flFinalCost = std::max(flBaseCost, 1.f);
		if (m_bSkipSpawn && bTouchesSpawn)
			flFinalCost += 5000.f;

		if (!tCtx.m_bCanJump && tPoints.m_vCenterNext.z - tPoints.m_vCenter.z > 18.0f)
			flFinalCost += 1200.f;

		if (!tCtx.m_bIgnoreTraces)
		{
			const float flHazardCost = LookupHazard(pNextArea);
			if (std::isfinite(flHazardCost))
				flFinalCost += std::clamp(flHazardCost * 0.28f, 0.f, 650.f);
			else
				continue;

			if (auto itStuck = m_mConnectionStuckTime.find(tKey); itStuck != m_mConnectionStuckTime.end())
			{
				if (itStuck->second.m_iExpireTick == 0 || itStuck->second.m_iExpireTick > iNow)
					flFinalCost += std::clamp(static_cast<float>(itStuck->second.m_iTimeStuck) * 120.f, 80.f, 800.f);
				else
					m_mConnectionStuckTime.erase(itStuck);
			}
		}
		else
		{
			flFinalCost *= 1.2f;
		}

		if (!std::isfinite(flFinalCost) || flFinalCost <= 0.f)
			continue;

		vOut.push_back({ pNextArea, flFinalCost });
	}
}

size_t CMap::GetConnectionNavMeshHash(CNavArea* pFrom, CNavArea* pTo) const
{
	size_t uHash = 0;
	auto HashArea = [&uHash](const CNavArea* pArea)
		{
			boost::hash_combine(uHash, pArea->m_uId);
			boost::hash_combine(uHash, pArea->m_iAttributeFlags);
			boost::hash_combine(uHash, pArea->m_iTFAttributeFlags);
			boost::hash_combine(uHash, pArea->m_vNwCorner.x);
			boost::hash_combine(uHash, pArea->m_vNwCorner.y);
			boost::hash_combine(uHash, pArea->m_vSeCorner.x);
			boost::hash_combine(uHash, pArea->m_vSeCorner.y);
			boost::hash_combine(uHash, pArea->m_vCenter.z);
			boost::hash_combine(uHash, pArea->m_flNeZ);
			boost::hash_combine(uHash, pArea->m_flSwZ);
			boost::hash_combine(uHash, pArea->m_flMinZ);
			boost::hash_combine(uHash, pArea->m_flMaxZ);
			boost::hash_combine(uHash, pArea->m_vConnections.size());
			for (const auto& tConnection : pArea->m_vConnections)
				boost::hash_combine(uHash, tConnection.m_pArea ? tConnection.m_pArea->m_uId : 0u);
		};

	HashArea(pFrom);
	HashArea(pTo);
	return uHash;
}

NavPoints_t CMap::DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea)
{
	const Vector vCurrentCenter = pCurrentArea->m_vCenter;
	const Vector vNextCenter = pNextArea->m_vCenter;
	Vector vCenter = pCurrentArea->GetNearestPoint(Vector2D(vNextCenter.x, vNextCenter.y));
	if (vCenter.x != vCurrentCenter.x && vCenter.y != vCurrentCenter.y
		&& vCenter.x != vNextCenter.x && vCenter.y != vNextCenter.y)
	{
		const Vector vNextClosest = pNextArea->GetNearestPoint(Vector2D(vCurrentCenter.x, vCurrentCenter.y));
		vCenter = vNextClosest;
		vCenter.z = pCurrentArea->GetNearestPoint(Vector2D(vNextClosest.x, vNextClosest.y)).z;
	}

	return {
		vCurrentCenter,
		vCenter,
		pNextArea->GetNearestPoint(Vector2D(vCenter.x, vCenter.y)),
		vNextCenter
	};
}

DropdownHint_t CMap::HandleDropdown(const NavPoints_t& tPoints)
{
	DropdownHint_t tHint{};
	tHint.m_vAdjustedPos = tPoints.m_vCenter;

	Vector vToTarget = tPoints.m_vNext - tPoints.m_vCenter;
	if (-vToTarget.z <= PLAYER_JUMP_HEIGHT)
		return tHint;

	vToTarget.z = 0.f;
	if (vToTarget.LengthSqr() <= 1.f)
	{
		vToTarget = tPoints.m_vCenter - tPoints.m_vCurrent;
		vToTarget.z = 0.f;
	}
	if (vToTarget.LengthSqr() <= 1.f)
		return tHint;

	vToTarget.Normalize();
	tHint.m_bRequiresDrop = true;
	tHint.m_flDropHeight = tPoints.m_vCenter.z - tPoints.m_vNext.z;
	tHint.m_vApproachDir = vToTarget;
	tHint.m_flApproachDistance = PLAYER_WIDTH * 2.0f;
	tHint.m_vAdjustedPos = tPoints.m_vCenter + vToTarget * tHint.m_flApproachDistance;
	tHint.m_vAdjustedPos.z = tPoints.m_vCenter.z;
	return tHint;
}

bool CMap::HasDirectConnection(CNavArea* pFrom, CNavArea* pTo) const
{
	if (!pFrom || !pTo) return false;
	if (pFrom == pTo) return true;
	for (const auto& tConnection : pFrom->m_vConnections)
	{
		if (tConnection.m_pArea != pTo)
			continue;
		if (pTo->m_flMinZ - pFrom->m_flMaxZ > PLAYER_JUMP_HEIGHT)
			return false;
		if (pFrom->m_flMinZ - pTo->m_flMaxZ > PLAYER_DEATH_DROP_HEIGHT)
			return false;
		return true;
	}
	return false;
}

float CMap::EvaluateConnectionCost(CNavArea* pCurrentArea, CNavArea* pNextArea, const NavPoints_t& tPoints, const DropdownHint_t& tDropdown, int iTeam) const
{
	auto HorizontalDistance = [](const Vector& a, const Vector& b)
		{
			Vector d = b - a; d.z = 0.f; return d.Length();
		};

	const float flForward = std::max(HorizontalDistance(tPoints.m_vCurrent, tPoints.m_vNext), 1.f);
	const float flDeviationStart = HorizontalDistance(tPoints.m_vCurrent, tPoints.m_vCenter);
	const float flDeviationEnd = HorizontalDistance(tPoints.m_vCenter, tPoints.m_vNext);
	const float flHeightDiff = tPoints.m_vNext.z - tPoints.m_vCurrent.z;

	float flCost = flForward + flDeviationStart * 0.55f + flDeviationEnd * 0.35f;

	if (flHeightDiff > 0.f)              flCost += flHeightDiff * 2.6f;
	else if (flHeightDiff < -8.f)        flCost += std::abs(flHeightDiff) * 1.15f;

	if (tDropdown.m_bRequiresDrop)
		flCost += tDropdown.m_flDropHeight * 3.25f + tDropdown.m_flApproachDistance * 0.7f;
	else if (tDropdown.m_flApproachDistance > 0.f)
		flCost += tDropdown.m_flApproachDistance * 0.5f;

	Vector vIn = tPoints.m_vCenter - tPoints.m_vCurrent;  vIn.z = 0.f;
	Vector vOut = tPoints.m_vNext - tPoints.m_vCenter;    vOut.z = 0.f;
	const float flLenIn = vIn.Length();
	const float flLenOut = vOut.Length();
	if (flLenIn > 1.f && flLenOut > 1.f)
	{
		vIn /= flLenIn;
		vOut /= flLenOut;
		const float flDot = std::clamp(vIn.Dot(vOut), -1.f, 1.f);
		flCost += (1.f - flDot) * 65.f;
	}

	Vector vAreaExtent = pNextArea->m_vSeCorner - pNextArea->m_vNwCorner;
	vAreaExtent.z = 0.f;
	const float flNextAreaSize = vAreaExtent.Length();
	flCost -= std::clamp(flNextAreaSize * 0.008f, 0.f, 8.f);
	if (flNextAreaSize < PLAYER_WIDTH * 1.6f)
		flCost += 75.f;

	const bool bRedSpawn = pNextArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED;
	const bool bBlueSpawn = pNextArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE;
	if (bRedSpawn || bBlueSpawn)
	{
		if (iTeam == TF_TEAM_RED && bBlueSpawn && !bRedSpawn)       flCost += 220.f;
		else if (iTeam == TF_TEAM_BLUE && bRedSpawn && !bBlueSpawn) flCost += 220.f;
		else if (bRedSpawn && bBlueSpawn)                            flCost += 60.f;
		else                                                          flCost += 40.f;
	}

	if (pNextArea->m_iAttributeFlags & NAV_MESH_AVOID)  flCost += 100000.f;
	if (pNextArea->m_iAttributeFlags & NAV_MESH_CROUCH) flCost += flForward * 7.f + 90.f;
	if (pNextArea->m_iAttributeFlags & NAV_MESH_NO_JUMP) flCost += flHeightDiff > 8.f ? 420.f : 80.f;
	if (pNextArea->m_iAttributeFlags & NAV_MESH_STAIRS) flCost += std::max(flHeightDiff, 0.f) * 0.6f;

	const bool bHasReturnPath = HasDirectConnection(pNextArea, pCurrentArea);
	int iForwardExitCount = 0;
	for (const auto& tExit : pNextArea->m_vConnections)
	{
		auto* pExitArea = tExit.m_pArea;
		if (pExitArea && pExitArea != pNextArea && pExitArea != pCurrentArea && IsAreaValid(pExitArea))
			iForwardExitCount++;
	}

	if (iForwardExitCount == 0)
		flCost += bHasReturnPath ? 340.f : 1300.f;
	else if (iForwardExitCount == 1)
		flCost += 150.f;

	if (!bHasReturnPath)
	{
		flCost += 260.f;
		if (tDropdown.m_bRequiresDrop)
			flCost += std::clamp(tDropdown.m_flDropHeight * 4.5f, 180.f, 720.f);
		if (pNextArea->m_iAttributeFlags & NAV_MESH_NO_JUMP)
			flCost += 320.f;
	}

	return std::max(flCost, 1.f);
}

void CMap::CollectAreasAround(const Vector& vOrigin, float flRadius, std::vector<CNavArea*>& vOutAreas)
{
	vOutAreas.clear();

	CNavArea* pSeedArea = FindClosestNavArea(vOrigin, false);
	if (!pSeedArea) return;

	const float flRadiusSqr = flRadius * flRadius;
	const float flExpansionLimit = flRadiusSqr * 4.f;

	std::queue<std::pair<CNavArea*, float>> qAreas;
	std::unordered_set<CNavArea*> setVisited;

	qAreas.emplace(pSeedArea, (pSeedArea->m_vCenter - vOrigin).LengthSqr());
	setVisited.insert(pSeedArea);

	int iLoopLimit = 2048;
	while (!qAreas.empty() && iLoopLimit-- > 0)
	{
		auto [pArea, flDist] = qAreas.front();
		qAreas.pop();

		if (flDist <= flRadiusSqr) vOutAreas.push_back(pArea);
		if (flDist > flExpansionLimit) continue;

		for (auto& tConnection : pArea->m_vConnections)
		{
			CNavArea* pNextArea = tConnection.m_pArea;
			if (!pNextArea) continue;
			const float flNextDist = (pNextArea->m_vCenter - vOrigin).LengthSqr();
			if (flNextDist > flExpansionLimit) continue;
			if (setVisited.insert(pNextArea).second)
				qAreas.emplace(pNextArea, flNextDist);
		}
	}

	if (vOutAreas.empty())
		vOutAreas.push_back(pSeedArea);
}

CNavArea* CMap::FindClosestNavArea(const Vector& vPos, bool bLocalOrigin)
{
	std::lock_guard lock(m_mutex);
	if (m_navfile.m_vAreas.empty())
		return nullptr;

	float flBestScore = FLT_MAX;
	CNavArea* pBest = nullptr;
	float flBestReachableScore = FLT_MAX;
	CNavArea* pBestReachable = nullptr;

	auto Consider = [&](CNavArea& tArea)
	{
		const float flScore = GetNearestAreaScore(tArea, vPos, bLocalOrigin);
		if (flScore < flBestScore)
		{
			flBestScore = flScore;
			pBest = &tArea;
		}

		if (bLocalOrigin && tArea.IsOverlapping(vPos, HALF_PLAYER_WIDTH)
			&& flScore < flBestReachableScore && CanFallToNavArea(vPos, tArea))
		{
			flBestReachableScore = flScore;
			pBestReachable = &tArea;
		}
	};

	std::vector<CNavArea*> vCandidates;
	for (const float flRadius : { 256.f, 768.f, 2048.f })
	{
		m_navfile.QueryOverlapping(vPos, flRadius, vCandidates);
		for (CNavArea* pArea : vCandidates)
			Consider(*pArea);

		if (pBestReachable)
			return pBestReachable;
		if (!bLocalOrigin && pBest)
			return pBest;
	}

	for (auto& tArea : m_navfile.m_vAreas)
		Consider(tArea);

	if (pBestReachable)
		return pBestReachable;
	if (!bLocalOrigin || !pBest || CanFallToNavArea(vPos, *pBest))
		return pBest;

	for (auto& tArea : m_navfile.m_vAreas)
	{
		const float flScore = GetNearestAreaScore(tArea, vPos, true);
		if (flScore < flBestReachableScore && CanFallToNavArea(vPos, tArea))
		{
			flBestReachableScore = flScore;
			pBestReachable = &tArea;
		}
	}

	return pBestReachable;
}

CNavFile::CNavFile(const char* szLevelname)
{
	Load(szLevelname);
}

void CNavFile::Clear()
{
	m_bOK = false;
	m_vPlaces.clear();
	m_vAreas.clear();
	m_vGrid.clear();
	m_nGridW = 0;
	m_nGridH = 0;
}

bool CNavFile::Load(const char* szLevelname)
{
	Clear();
	if (!szLevelname)
		return false;

	m_sMapName = szLevelname;
	std::ifstream file(m_sMapName, std::ios::binary);
	if (!file.is_open())
		return false;

	std::error_code tError;
	const uintmax_t uFileSize = std::filesystem::file_size(m_sMapName, tError);
	if (tError || uFileSize < 16 || uFileSize > 512ull * 1024ull * 1024ull)
		return false;

	auto CanRead = [&](uintmax_t uSize)
	{
		const std::streampos tPosition = file.tellg();
		return tPosition >= 0 && static_cast<uintmax_t>(tPosition) <= uFileSize && uSize <= uFileSize - static_cast<uintmax_t>(tPosition);
	};
	auto Read = [&](auto& tValue)
	{
		if (!CanRead(sizeof(tValue)))
			return false;
		file.read(reinterpret_cast<char*>(&tValue), sizeof(tValue));
		return static_cast<bool>(file);
	};
	auto ReadBytes = [&](char* pData, size_t uSize)
	{
		if (!CanRead(uSize))
			return false;
		file.read(pData, static_cast<std::streamsize>(uSize));
		return static_cast<bool>(file);
	};
	auto HasItems = [&](uint64_t uCount, uint64_t uMinimumSize)
	{
		const std::streampos tPosition = file.tellg();
		if (tPosition < 0 || static_cast<uintmax_t>(tPosition) > uFileSize || !uMinimumSize)
			return false;
		return uCount <= (uFileSize - static_cast<uintmax_t>(tPosition)) / uMinimumSize;
	};

	uint32_t uMagic = 0;
	if (!Read(uMagic) || uMagic != 0xFEEDFACE)
		return false;

	uint32_t uVersion = 0;
	if (!Read(uVersion) || uVersion < 16)
		return false;

	uint32_t uSubVersion = 0;
	if (!Read(uSubVersion) || uSubVersion != 2)
		return false;

	uint32_t uBspSize = 0;
	unsigned char bAnalyzed = 0;
	if (!Read(uBspSize) || !Read(bAnalyzed))
		return false;

	uint16_t uPlacesCount = 0;
	if (!Read(uPlacesCount) || !HasItems(uPlacesCount, sizeof(uint16_t)))
		return false;
	std::vector<NavPlace_t> vPlaces;
	vPlaces.reserve(uPlacesCount);
	for (uint16_t i = 0; i < uPlacesCount; ++i)
	{
		NavPlace_t tPlace{};
		if (!Read(tPlace.m_uLen) || tPlace.m_uLen > sizeof(tPlace.m_sName) || !ReadBytes(tPlace.m_sName, tPlace.m_uLen))
			return false;
		vPlaces.push_back(tPlace);
	}

	unsigned char bHasUnnamedAreas = 0;
	if (!Read(bHasUnnamedAreas))
		return false;

	uint32_t uAreaCount = 0;
	if (!Read(uAreaCount) || !HasItems(uAreaCount, 107))
		return false;
	std::vector<CNavArea> vAreas;
	vAreas.reserve(uAreaCount);
	for (uint32_t i = 0; i < uAreaCount; ++i)
	{
		CNavArea tArea{};
		if (!Read(tArea.m_uId) || !Read(tArea.m_iAttributeFlags) || !Read(tArea.m_vNwCorner) || !Read(tArea.m_vSeCorner) ||
			!Read(tArea.m_flNeZ) || !Read(tArea.m_flSwZ))
			return false;

		tArea.m_vCenter[0] = (tArea.m_vNwCorner[0] + tArea.m_vSeCorner[0]) / 2.0f;
		tArea.m_vCenter[1] = (tArea.m_vNwCorner[1] + tArea.m_vSeCorner[1]) / 2.0f;

		if ((tArea.m_vSeCorner.x - tArea.m_vNwCorner.x) > 0.0f &&
			(tArea.m_vSeCorner.y - tArea.m_vNwCorner.y) > 0.0f)
		{
			tArea.m_flInvDxCorners = 1.0f / (tArea.m_vSeCorner.x - tArea.m_vNwCorner.x);
			tArea.m_flInvDyCorners = 1.0f / (tArea.m_vSeCorner.y - tArea.m_vNwCorner.y);
		}
		else
			tArea.m_flInvDxCorners = tArea.m_flInvDyCorners = 0.0f;

		tArea.m_vCenter[2] = tArea.GetZ(tArea.m_vCenter.x, tArea.m_vCenter.y);
		tArea.m_flMinZ = std::min({ tArea.m_vNwCorner.z, tArea.m_flNeZ, tArea.m_flSwZ, tArea.m_vSeCorner.z }) - PLAYER_STEP_HEIGHT;
		tArea.m_flMaxZ = std::max({ tArea.m_vNwCorner.z, tArea.m_flNeZ, tArea.m_flSwZ, tArea.m_vSeCorner.z }) + PLAYER_STEP_HEIGHT;
		tArea.m_uConnectionCount = 0;

		for (int iDir = 0; iDir < 4; iDir++)
		{
			uint32_t uDirConnectionCount = 0;
			if (!Read(uDirConnectionCount) || !HasItems(uDirConnectionCount, sizeof(uint32_t)))
				return false;
			for (uint32_t j = 0; j < uDirConnectionCount; j++)
			{
				NavConnect_t tConnect{};
				if (!Read(tConnect.m_uId))
					return false;
				if (tConnect.m_uId == tArea.m_uId)
					continue;

				tArea.m_vConnections.push_back(tConnect);
				tArea.m_vConnectionsDir[iDir].push_back(tConnect);
				tArea.m_uConnectionCount++;
			}
		}

		if (!Read(tArea.m_uHidingSpotCount) || !HasItems(tArea.m_uHidingSpotCount, sizeof(uint32_t) + sizeof(Vector) + sizeof(unsigned char)))
			return false;
		for (uint8_t j = 0; j < tArea.m_uHidingSpotCount; j++)
		{
			CHidingSpot tSpot{};
			if (!Read(tSpot.m_uId) || !Read(tSpot.m_vPos) || !Read(tSpot.m_fFlags))
				return false;
			tArea.m_vHidingSpots.push_back(tSpot);
		}

		if (!Read(tArea.m_uEncounterSpotCount) || !HasItems(tArea.m_uEncounterSpotCount, 11))
			return false;

		for (uint32_t j = 0; j < tArea.m_uEncounterSpotCount; j++)
		{
			SpotEncounter_t tSpot{};
			unsigned char iFromDir = 0, iToDir = 0;
			if (!Read(tSpot.m_tFrom.m_uId) || !Read(iFromDir) || !Read(tSpot.m_tTo.m_uId) ||
				!Read(iToDir) || !Read(tSpot.m_uSpotCount) || !HasItems(tSpot.m_uSpotCount, sizeof(uint32_t) + sizeof(unsigned char)))
				return false;
			tSpot.m_iFromDir = iFromDir;
			tSpot.m_iToDir = iToDir;

			for (uint8_t s = 0; s < tSpot.m_uSpotCount; ++s)
			{
				SpotOrder_t tOrder{};
				unsigned char uT = 0;
				if (!Read(tOrder.m_uId) || !Read(uT))
					return false;
				tOrder.flT = uT;
				tSpot.m_vSpots.push_back(tOrder);
			}

			tArea.m_vSpotEncounters.push_back(tSpot);
		}

		if (!Read(tArea.m_uIndexType))
			return false;

		for (int iDir = 0; iDir < 2; iDir++)
		{
			if (!Read(tArea.m_uLadderCount) || !HasItems(tArea.m_uLadderCount, sizeof(uint32_t)))
				return false;
			for (uint32_t j = 0; j < tArea.m_uLadderCount; j++)
			{
				uint32_t uLadder = 0;
				if (!Read(uLadder))
					return false;
				tArea.m_vLadders[iDir].push_back(uLadder);
			}
		}

		for (float& j : tArea.m_flEarliestOccupyTime)
			if (!Read(j))
				return false;

		for (float& j : tArea.m_flLightIntensity)
			if (!Read(j))
				return false;

		if (!Read(tArea.m_uVisibleAreaCount) || !HasItems(tArea.m_uVisibleAreaCount, sizeof(uint32_t) + sizeof(unsigned char)))
			return false;
		for (uint32_t j = 0; j < tArea.m_uVisibleAreaCount; ++j)
		{
			AreaBindInfo_t tInfo{};
			if (!Read(tInfo.m_uId) || !Read(tInfo.m_uAttributes))
				return false;
			tArea.m_vPotentiallyVisibleAreas.push_back(tInfo);
		}

		if (!Read(tArea.m_uInheritVisibilityFrom) || !Read(tArea.m_iTFAttributeFlags))
			return false;

		vAreas.push_back(std::move(tArea));
	}

	m_uBspSize = uBspSize;
	m_bAnalyzed = bAnalyzed;
	m_bHasUnnamedAreas = bHasUnnamedAreas;
	m_vPlaces = std::move(vPlaces);
	m_vAreas = std::move(vAreas);

	std::unordered_map<uint32_t, CNavArea*> mAreasById;
	mAreasById.reserve(m_vAreas.size());
	for (auto& tArea : m_vAreas)
		mAreasById.emplace(tArea.m_uId, &tArea);

	auto Resolve = [&](NavConnect_t& tConnect)
	{
		if (const auto it = mAreasById.find(tConnect.m_uId); it != mAreasById.end())
			tConnect.m_pArea = it->second;
	};

	for (auto& tArea : m_vAreas)
	{
		for (auto& tConnection : tArea.m_vConnections)
			Resolve(tConnection);
		for (auto& vDir : tArea.m_vConnectionsDir)
			for (auto& tConnection : vDir)
				Resolve(tConnection);
		for (auto& tBind : tArea.m_vPotentiallyVisibleAreas)
			if (const auto it = mAreasById.find(tBind.m_uId); it != mAreasById.end())
				tBind.m_pArea = it->second;
	}

	BuildSpatialIndex();
	m_bOK = true;
	return true;
}

void CNavFile::BuildSpatialIndex()
{
	m_vGrid.clear();
	m_nGridW = 0;
	m_nGridH = 0;
	if (m_vAreas.empty())
		return;

	m_flGridMinX = m_vAreas.front().m_vNwCorner.x;
	m_flGridMinY = m_vAreas.front().m_vNwCorner.y;
	float flMaxX = m_vAreas.front().m_vSeCorner.x;
	float flMaxY = m_vAreas.front().m_vSeCorner.y;
	for (const auto& tArea : m_vAreas)
	{
		m_flGridMinX = std::min(m_flGridMinX, tArea.m_vNwCorner.x);
		m_flGridMinY = std::min(m_flGridMinY, tArea.m_vNwCorner.y);
		flMaxX = std::max(flMaxX, tArea.m_vSeCorner.x);
		flMaxY = std::max(flMaxY, tArea.m_vSeCorner.y);
	}

	m_flGridCell = 256.f;
	m_nGridW = std::max(1, static_cast<int>(std::ceil((flMaxX - m_flGridMinX) / m_flGridCell)));
	m_nGridH = std::max(1, static_cast<int>(std::ceil((flMaxY - m_flGridMinY) / m_flGridCell)));
	m_vGrid.assign(static_cast<size_t>(m_nGridW) * static_cast<size_t>(m_nGridH), {});

	auto ClampCell = [&](int iValue, int iMax) { return std::clamp(iValue, 0, iMax - 1); };

	for (auto& tArea : m_vAreas)
	{
		const int iX0 = ClampCell(static_cast<int>(std::floor((tArea.m_vNwCorner.x - m_flGridMinX) / m_flGridCell)), m_nGridW);
		const int iY0 = ClampCell(static_cast<int>(std::floor((tArea.m_vNwCorner.y - m_flGridMinY) / m_flGridCell)), m_nGridH);
		const int iX1 = ClampCell(static_cast<int>(std::floor((tArea.m_vSeCorner.x - m_flGridMinX) / m_flGridCell)), m_nGridW);
		const int iY1 = ClampCell(static_cast<int>(std::floor((tArea.m_vSeCorner.y - m_flGridMinY) / m_flGridCell)), m_nGridH);
		for (int iY = iY0; iY <= iY1; ++iY)
			for (int iX = iX0; iX <= iX1; ++iX)
				m_vGrid[static_cast<size_t>(iY) * static_cast<size_t>(m_nGridW) + static_cast<size_t>(iX)].push_back(&tArea);
	}
}

void CNavFile::QueryOverlapping(const Vector& vPos, float flRadius, std::vector<CNavArea*>& vOut) const
{
	vOut.clear();
	if (m_vGrid.empty() || m_nGridW <= 0 || m_nGridH <= 0)
	{
		vOut.reserve(m_vAreas.size());
		for (auto& tArea : const_cast<std::vector<CNavArea>&>(m_vAreas))
			vOut.push_back(&tArea);
		return;
	}

	const float flX0 = vPos.x - flRadius;
	const float flY0 = vPos.y - flRadius;
	const float flX1 = vPos.x + flRadius;
	const float flY1 = vPos.y + flRadius;
	auto Cell = [&](float flValue, float flMin, int iMax)
	{
		return std::clamp(static_cast<int>(std::floor((flValue - flMin) / m_flGridCell)), 0, iMax - 1);
	};

	const int iX0 = Cell(flX0, m_flGridMinX, m_nGridW);
	const int iY0 = Cell(flY0, m_flGridMinY, m_nGridH);
	const int iX1 = Cell(flX1, m_flGridMinX, m_nGridW);
	const int iY1 = Cell(flY1, m_flGridMinY, m_nGridH);

	std::unordered_map<CNavArea*, char> mSeen;
	for (int iY = iY0; iY <= iY1; ++iY)
	{
		for (int iX = iX0; iX <= iX1; ++iX)
		{
			for (CNavArea* pArea : m_vGrid[static_cast<size_t>(iY) * static_cast<size_t>(m_nGridW) + static_cast<size_t>(iX)])
			{
				if (mSeen.emplace(pArea, 1).second)
					vOut.push_back(pArea);
			}
		}
	}
}

bool CNavFile::Write(const char* szFilename)
{
	if (!m_bOK)
		return false;

	std::filesystem::path tFilePath;
	if (szFilename)
		tFilePath = std::filesystem::path(szFilename);
	else
	{
		const std::string sLevelName = SDK::GetLevelName();
		if (sLevelName.empty() || sLevelName == "None")
			return false;
		tFilePath = std::filesystem::current_path() / "unibox" / "Nav" / (sLevelName + ".nav");
	}
	if (tFilePath.empty())
		return false;

	if (m_vPlaces.size() > (std::numeric_limits<uint16_t>::max)() || m_vAreas.size() > (std::numeric_limits<uint32_t>::max)())
		return false;
	for (const auto& tPlace : m_vPlaces)
		if (tPlace.m_uLen > sizeof(tPlace.m_sName))
			return false;
	for (const auto& tArea : m_vAreas)
	{
		for (const auto& vConnections : tArea.m_vConnectionsDir)
			if (vConnections.size() > (std::numeric_limits<uint32_t>::max)())
				return false;
		if (tArea.m_vHidingSpots.size() > (std::numeric_limits<uint8_t>::max)() ||
			tArea.m_vSpotEncounters.size() > (std::numeric_limits<uint32_t>::max)() ||
			tArea.m_vPotentiallyVisibleAreas.size() > (std::numeric_limits<uint32_t>::max)())
			return false;
		for (const auto& tEncounter : tArea.m_vSpotEncounters)
			if (tEncounter.m_iFromDir < 0 || tEncounter.m_iFromDir > (std::numeric_limits<uint8_t>::max)() ||
				tEncounter.m_iToDir < 0 || tEncounter.m_iToDir > (std::numeric_limits<uint8_t>::max)() ||
				tEncounter.m_vSpots.size() > (std::numeric_limits<uint8_t>::max)())
				return false;
		for (const auto& vLadders : tArea.m_vLadders)
			if (vLadders.size() > (std::numeric_limits<uint32_t>::max)())
				return false;
	}

	std::error_code tError;
	if (!tFilePath.parent_path().empty())
		std::filesystem::create_directories(tFilePath.parent_path(), tError);
	if (tError)
		return false;

	std::filesystem::path tTempPath = tFilePath;
	tTempPath += ".tmp";
	std::ofstream file(tTempPath, std::ios::binary | std::ios::trunc);
	if (!file.is_open())
	{
		SDK::Output("CNavFile::Write", std::format("Couldn't open file {}", tFilePath.string()).c_str(), { 200, 150, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return false;
	}

	uint32_t uMagic = 0xFEEDFACE;
	uint32_t uVersion = 16;
	uint32_t uSubVersion = 2;
	file.write((char*)&uMagic, sizeof(uint32_t));
	file.write((char*)&uVersion, sizeof(uint32_t));
	file.write((char*)&uSubVersion, sizeof(uint32_t));
	file.write((char*)&m_uBspSize, sizeof(uint32_t));
	file.write((char*)&m_bAnalyzed, sizeof(unsigned char));

	uint16_t uPlacesCount = static_cast<uint16_t>(m_vPlaces.size());
	file.write((char*)&uPlacesCount, sizeof(uint16_t));
	for (auto& tPlace : m_vPlaces)
	{
		file.write((char*)&tPlace.m_uLen, sizeof(uint16_t));
		file.write((char*)&tPlace.m_sName, tPlace.m_uLen);
	}

	file.write((char*)&m_bHasUnnamedAreas, sizeof(unsigned char));

	uint32_t uAreaCount = static_cast<uint32_t>(m_vAreas.size());
	file.write((char*)&uAreaCount, sizeof(uint32_t));
	for (auto& tArea : m_vAreas)
	{
		file.write((char*)&tArea.m_uId, sizeof(uint32_t));
		file.write((char*)&tArea.m_iAttributeFlags, sizeof(uint32_t));
		file.write((char*)&tArea.m_vNwCorner, sizeof(Vector));
		file.write((char*)&tArea.m_vSeCorner, sizeof(Vector));
		file.write((char*)&tArea.m_flNeZ, sizeof(float));
		file.write((char*)&tArea.m_flSwZ, sizeof(float));

		for (int iDir = 0; iDir < 4; iDir++)
		{
			uint32_t uConnectionCount = static_cast<uint32_t>(tArea.m_vConnectionsDir[iDir].size());
			file.write((char*)&uConnectionCount, sizeof(uint32_t));
			for (auto& tConnect : tArea.m_vConnectionsDir[iDir])
				file.write((char*)&tConnect.m_uId, sizeof(uint32_t));
		}

		uint8_t uHidingSpotCount = static_cast<uint8_t>(tArea.m_vHidingSpots.size());
		file.write((char*)&uHidingSpotCount, sizeof(uint8_t));
		for (auto& tHidingSpot : tArea.m_vHidingSpots)
		{
			file.write((char*)&tHidingSpot.m_uId, sizeof(uint32_t));
			file.write((char*)&tHidingSpot.m_vPos, sizeof(Vector));
			file.write((char*)&tHidingSpot.m_fFlags, sizeof(unsigned char));
		}

		uint32_t uEncounterSpotCount = static_cast<uint32_t>(tArea.m_vSpotEncounters.size());
		file.write((char*)&uEncounterSpotCount, sizeof(uint32_t));
		for (auto& tEncounterSpot : tArea.m_vSpotEncounters)
		{
			file.write((char*)&tEncounterSpot.m_tFrom.m_uId, sizeof(uint32_t));
			file.write((char*)&tEncounterSpot.m_iFromDir, sizeof(unsigned char));
			file.write((char*)&tEncounterSpot.m_tTo.m_uId, sizeof(uint32_t));
			file.write((char*)&tEncounterSpot.m_iToDir, sizeof(unsigned char));

			uint8_t uSpotCount = static_cast<uint8_t>(tEncounterSpot.m_vSpots.size());
			file.write((char*)&uSpotCount, sizeof(unsigned char));
			for (auto& tOrder : tEncounterSpot.m_vSpots)
			{
				file.write((char*)&tOrder.m_uId, sizeof(uint32_t));
				const unsigned char uT = static_cast<unsigned char>(tOrder.flT);
				file.write((char*)&uT, sizeof(unsigned char));
			}
		}

		file.write((char*)&tArea.m_uIndexType, sizeof(uint16_t));

		for (int iDir = 0; iDir < 2; iDir++)
		{
			uint32_t uLadderCount = static_cast<uint32_t>(tArea.m_vLadders[iDir].size());
			file.write((char*)&uLadderCount, sizeof(uint32_t));
			for (auto& uLadder : tArea.m_vLadders[iDir])
				file.write((char*)&uLadder, sizeof(uint32_t));
		}

		for (float& j : tArea.m_flEarliestOccupyTime)
			file.write((char*)&j, sizeof(float));

		for (float& j : tArea.m_flLightIntensity)
			file.write((char*)&j, sizeof(float));

		uint32_t uPotentiallyVisibleCount = static_cast<uint32_t>(tArea.m_vPotentiallyVisibleAreas.size());
		file.write((char*)&uPotentiallyVisibleCount, sizeof(uint32_t));
		for (auto& tVisibleArea : tArea.m_vPotentiallyVisibleAreas)
		{
			file.write((char*)&tVisibleArea.m_uId, sizeof(uint32_t));
			file.write((char*)&tVisibleArea.m_uAttributes, sizeof(unsigned char));
		}

		file.write((char*)&tArea.m_uInheritVisibilityFrom, sizeof(uint32_t));
		file.write((char*)&tArea.m_iTFAttributeFlags, sizeof(uint32_t));
	}

	file.flush();
	if (!file)
	{
		file.close();
		std::filesystem::remove(tTempPath, tError);
		return false;
	}
	file.close();
	if (file.fail())
	{
		std::filesystem::remove(tTempPath, tError);
		return false;
	}

	std::filesystem::rename(tTempPath, tFilePath, tError);
	if (tError)
	{
		std::filesystem::remove(tTempPath, tError);
		return false;
	}
	return true;
}

namespace
{
	int s_iRoundState = -1;
	int s_iHighestCapturedPoint = -1;
	bool s_bHaveOwners = false;
	std::array<int, 8> s_aOwners{};

	int RequiredCaptureIndex(const CNavArea& tArea)
	{
		if (tArea.m_iTFAttributeFlags & TF_NAV_WITH_FIFTH_POINT)
			return 4;
		if (tArea.m_iTFAttributeFlags & TF_NAV_WITH_FOURTH_POINT)
			return 3;
		if (tArea.m_iTFAttributeFlags & TF_NAV_WITH_THIRD_POINT)
			return 2;
		if (tArea.m_iTFAttributeFlags & TF_NAV_WITH_SECOND_POINT)
			return 1;
		return 0;
	}

	bool IsPointCaptureBlocked(const CNavArea& tArea, int iHighestCapturedPoint)
	{
		if (tArea.m_iTFAttributeFlags & TF_NAV_BLOCKED_UNTIL_POINT_CAPTURE)
			return iHighestCapturedPoint < RequiredCaptureIndex(tArea);

		if (tArea.m_iTFAttributeFlags & TF_NAV_BLOCKED_AFTER_POINT_CAPTURE)
			return iHighestCapturedPoint >= RequiredCaptureIndex(tArea);

		return false;
	}
}

void NavPolicy::Update()
{
	auto pGameRules = I::TFGameRules();
	const int iRoundState = pGameRules ? pGameRules->m_iRoundState() : -1;
	if (iRoundState == GR_STATE_PREROUND && s_iRoundState != iRoundState)
	{
		s_iHighestCapturedPoint = -1;
		s_bHaveOwners = false;
	}
	s_iRoundState = iRoundState;

	auto pObjective = H::Entities.GetObjectiveResource();
	if (!pObjective)
		return;

	const int nPoints = std::clamp(pObjective->m_iNumControlPoints(), 0, 8);
	if (!s_bHaveOwners)
	{
		for (int i = 0; i < nPoints; ++i)
			s_aOwners[i] = pObjective->m_iOwner(i);
		s_bHaveOwners = true;
		return;
	}

	for (int i = 0; i < nPoints; ++i)
	{
		const int iOwner = pObjective->m_iOwner(i);
		if (iOwner != s_aOwners[i] && (iOwner == TF_TEAM_RED || iOwner == TF_TEAM_BLUE))
			s_iHighestCapturedPoint = std::max(s_iHighestCapturedPoint, i);
		s_aOwners[i] = iOwner;
	}
}

void NavPolicy::Reset()
{
	s_iRoundState = -1;
	s_iHighestCapturedPoint = -1;
	s_bHaveOwners = false;
	s_aOwners = {};
}

NavPolicyState NavPolicy::Snapshot(int iTeam)
{
	NavPolicyState tState{};
	tState.m_iTeam = iTeam;
	tState.m_bIgnoreSetupGates = Vars::Misc::Movement::NavEngine::PathInSetup.Value;
	tState.m_iHighestCapturedPoint = s_iHighestCapturedPoint;

	if (auto pGameRules = I::TFGameRules())
	{
		tState.m_bInSetup = pGameRules->m_bInSetup()
			|| pGameRules->m_iRoundState() == GR_STATE_PREROUND;
		tState.m_bRoundWon = pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN;
	}

	return tState;
}

bool NavPolicy::IsEnemySpawn(const CNavArea& tArea, int iTeam)
{
	const bool bRed = tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED;
	const bool bBlue = tArea.m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE;
	if (iTeam == TF_TEAM_RED)
		return bBlue && !bRed;
	if (iTeam == TF_TEAM_BLUE)
		return bRed && !bBlue;
	return false;
}

bool NavPolicy::IsAreaTraversable(const CNavArea& tArea, const NavPolicyState& tState)
{
	if (tArea.IsBlocked(tState.m_iTeam))
		return false;

	if (!tState.m_bIgnoreSetupGates && tState.m_bInSetup)
	{
		if (tArea.m_iTFAttributeFlags & (TF_NAV_BLUE_SETUP_GATE | TF_NAV_RED_SETUP_GATE))
			return false;
	}

	if (IsPointCaptureBlocked(tArea, tState.m_iHighestCapturedPoint))
		return false;

	if (!tState.m_bRoundWon && IsEnemySpawn(tArea, tState.m_iTeam))
		return false;

	return true;
}
