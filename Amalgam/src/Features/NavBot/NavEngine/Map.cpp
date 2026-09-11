#include "NavEngine.h"
#include "../Hazards/Hazards.h"
#include "../NavRuntime.h"
#include <atomic>

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
				tCrumb.m_vPos.z = pArea->GetZ(tCrumb.m_vPos.x, tCrumb.m_vPos.y);
				tCrumb.m_vApproachDir = vApproachDir;
				if (pDrop && iStep == iSteps)
				{
					tCrumb.m_bRequiresDrop = true;
					tCrumb.m_flDropHeight = pDrop->m_flDropHeight;
					tCrumb.m_flApproachDistance = pDrop->m_flApproachDistance;
					if (!pDrop->m_vApproachDir.IsZero())
						tCrumb.m_vApproachDir = pDrop->m_vApproachDir;
				}
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
		if (pNextArea->IsBlocked(iTeam)) continue;

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

			if (!tCtx.m_bIgnoreTraces && flUpDelta > PLAYER_CROUCHED_JUMP_HEIGHT)
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
	const auto vCurrentCenter = pCurrentArea->m_vCenter;
	const auto vNextCenter = pNextArea->m_vCenter;
	auto ResolveAxis = [](float flCurrentMin, float flCurrentMax, float flNextMin, float flNextMax, float flMidpoint)
		{
			const float flOverlapMin = std::max(flCurrentMin, flNextMin);
			const float flOverlapMax = std::min(flCurrentMax, flNextMax);
			if (flOverlapMin <= flOverlapMax)
			{
				const float flGate = std::clamp(flMidpoint, flOverlapMin, flOverlapMax);
				return std::pair(flGate, flGate);
			}
			return flCurrentMax < flNextMin
				? std::pair(flCurrentMax, flNextMin)
				: std::pair(flCurrentMin, flNextMax);
		};

	const auto [flCurrentX, flNextX] = ResolveAxis(pCurrentArea->m_vNwCorner.x, pCurrentArea->m_vSeCorner.x,
		pNextArea->m_vNwCorner.x, pNextArea->m_vSeCorner.x, (vCurrentCenter.x + vNextCenter.x) * 0.5f);
	const auto [flCurrentY, flNextY] = ResolveAxis(pCurrentArea->m_vNwCorner.y, pCurrentArea->m_vSeCorner.y,
		pNextArea->m_vNwCorner.y, pNextArea->m_vSeCorner.y, (vCurrentCenter.y + vNextCenter.y) * 0.5f);
	const Vector vCurrentGate = GetSafePointOnArea(pCurrentArea, { flCurrentX, flCurrentY, 0.0f });
	const Vector vNextGate = GetSafePointOnArea(pNextArea, { flNextX, flNextY, 0.0f });
	return NavPoints_t(vCurrentCenter, vCurrentGate, vNextGate, vNextCenter);
}

DropdownHint_t CMap::HandleDropdown(const NavPoints_t& tPoints)
{
	DropdownHint_t tHint{};
	tHint.m_vAdjustedPos = tPoints.m_vCenter;

	Vector vHorizontal = tPoints.m_vCenterNext - tPoints.m_vCenter;
	const float flHeightDiff = vHorizontal.z;
	vHorizontal.z = 0.f;
	const float flHorizontalLength = vHorizontal.Length();
	if (flHorizontalLength <= 1.f || -flHeightDiff <= PLAYER_JUMP_HEIGHT)
		return tHint;

	const Vector vDirection = vHorizontal / flHorizontalLength;
	tHint.m_bRequiresDrop = true;
	tHint.m_flDropHeight = -flHeightDiff;
	tHint.m_vApproachDir = vDirection;
	tHint.m_flApproachDistance = std::clamp(tHint.m_flDropHeight * 0.5f, PLAYER_WIDTH * 0.85f, PLAYER_WIDTH * 1.5f);
	tHint.m_flApproachDistance = std::min(tHint.m_flApproachDistance, flHorizontalLength * 0.95f);
	tHint.m_vAdjustedPos = tPoints.m_vCenter + vDirection * tHint.m_flApproachDistance;
	tHint.m_vAdjustedPos.z = tPoints.m_vCenter.z;
	return tHint;
}

bool CMap::HasDirectConnection(CNavArea* pFrom, CNavArea* pTo) const
{
	if (!pFrom || !pTo) return false;
	if (pFrom == pTo) return true;
	for (const auto& tConnection : pFrom->m_vConnections)
		if (tConnection.m_pArea == pTo)
			return pFrom->m_flMaxZ > pTo->m_flMinZ - PLAYER_CROUCHED_JUMP_HEIGHT;
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
	float flBestScore = FLT_MAX;
	CNavArea* pBest = nullptr;
	float flBestReachableScore = FLT_MAX;
	CNavArea* pBestReachable = nullptr;

	for (auto& tArea : m_navfile.m_vAreas)
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
	}

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
