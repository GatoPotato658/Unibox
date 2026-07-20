#include "NavEngine.h"
#include "../Hazards/Hazards.h"
#include "../NavRuntime.h"
#include <cassert>

float CMap::GetBlacklistPenalty(const BlacklistReason_t& tReason) const
{
	switch (tReason.m_eValue)
	{
	case BlacklistReasonEnum::Sentry:        return HAZARD_COST_SENTRY;
	case BlacklistReasonEnum::SentryMedium:  return HAZARD_COST_SENTRY_MEDIUM;
	case BlacklistReasonEnum::SentryLow:     return HAZARD_COST_SENTRY_LOW;
	case BlacklistReasonEnum::EnemyInvuln:   return HAZARD_COST_ENEMY_INVULN;
	case BlacklistReasonEnum::Sticky:        return HAZARD_COST_STICKY;
	case BlacklistReasonEnum::EnemyNormal:   return HAZARD_COST_ENEMY_NORMAL;
	case BlacklistReasonEnum::EnemyDormant:  return HAZARD_COST_ENEMY_DORMANT;
	case BlacklistReasonEnum::BadBuildSpot:  return HAZARD_COST_AVOID;
	default:                                 return 0.f;
	}
}


static float GetAreaVerticalOutside(const CNavArea& tArea, const Vector& vPos)
{
	const float flBelow = std::max(tArea.m_flMinZ - vPos.z, 0.0f);
	const float flAbove = std::max(vPos.z - tArea.m_flMaxZ, 0.0f);
	return flBelow + flAbove;
}

static float GetNearestAreaScore(const CNavArea& tArea, const Vector& vPos, bool bLocalOrigin, bool* pIsTightOverlap = nullptr)
{
	const float flNearestX = std::clamp(vPos.x, tArea.m_vNwCorner.x, tArea.m_vSeCorner.x);
	const float flNearestY = std::clamp(vPos.y, tArea.m_vNwCorner.y, tArea.m_vSeCorner.y);
	const float flNearestZ = tArea.GetZ(flNearestX, flNearestY);
	const float flVerticalToSurface = std::fabs(flNearestZ - vPos.z);
	const float flVerticalOutside = GetAreaVerticalOutside(tArea, vPos);

	const float flDx = flNearestX - vPos.x;
	const float flDy = flNearestY - vPos.y;
	const float flPlanarDistSqr = flDx * flDx + flDy * flDy;

	const bool bOverlapping = tArea.IsOverlapping(vPos);
	const bool bTightOverlap = bOverlapping && flVerticalOutside <= 18.0f;
	if (pIsTightOverlap) *pIsTightOverlap = bTightOverlap;

	float flScore = flPlanarDistSqr + (flVerticalToSurface * flVerticalToSurface * 6.0f) + (flVerticalOutside * flVerticalOutside * (bLocalOrigin ? 18.0f : 10.0f));
	if (bOverlapping) flScore *= bLocalOrigin ? 0.45f : 0.7f;
	if (bTightOverlap) flScore *= 0.15f;
	else if (bLocalOrigin && bOverlapping && flVerticalOutside > PLAYER_JUMP_HEIGHT)
		flScore += flVerticalOutside * flVerticalOutside * 8.0f;

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
	tStart.m_bInOpen = true;

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
		tCurrent.m_bInOpen = false;

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
				tNext.m_bInOpen = false;
			}

			const float flTentativeG = tCurrent.m_g + tEdge.m_flCost;
			if (flTentativeG < tNext.m_g)
			{
				tNext.m_pParent = pCurrentArea;
				tNext.m_g = flTentativeG;
				tNext.m_f = flTentativeG + pNextArea->m_vCenter.DistTo(pEnd->m_vCenter);
				tNext.m_bInOpen = true;
				openSet.push({ tNext.m_f, uNextIdx });
			}
		}
	}

	return 1;
}

std::vector<CNavArea*> CMap::FindPath(CNavArea* pLocalArea, CNavArea* pDestArea, int* pOutResult)
{
	if (m_eState != NavStateEnum::Active) return {};
	SolveContext tCtx = BuildSolveContext();
	std::lock_guard lock(m_mutex);
	std::vector<CNavArea*> vPath;
	float flCost;
	const int iResult = Solve(pLocalArea, pDestArea, tCtx, vPath, &flCost);
	if (pOutResult) *pOutResult = iResult;
	return vPath;
}

SolveContext CMap::BuildSolveContext()
{
	SolveContext tCtx{};
	auto pLocal = H::Entities.GetLocal();
	tCtx.m_iTeam = pLocal ? pLocal->m_iTeamNum() : 0;
	tCtx.m_iTickcount = I::GlobalVars ? I::GlobalVars->tickcount : 0;
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

std::array<uint8_t, 20> CMap::ComputeNavMeshSha() const
{
	boost::uuids::detail::sha1 tSha;
	auto Add = [&tSha](const auto& value)
		{ tSha.process_bytes(&value, sizeof(value)); };

	const uint32_t uAreaCount = static_cast<uint32_t>(m_navfile.m_vAreas.size());
	Add(uAreaCount);
	for (const auto& tArea : m_navfile.m_vAreas)
	{
		Add(tArea.m_uId);
		Add(tArea.m_iAttributeFlags);
		Add(tArea.m_iTFAttributeFlags);
		Add(tArea.m_vNwCorner);
		Add(tArea.m_vSeCorner);
		Add(tArea.m_vCenter);
		Add(tArea.m_flInvDxCorners);
		Add(tArea.m_flInvDyCorners);
		Add(tArea.m_flNeZ);
		Add(tArea.m_flSwZ);
		Add(tArea.m_flMinZ);
		Add(tArea.m_flMaxZ);
		const uint32_t uConnectionCount = static_cast<uint32_t>(tArea.m_vConnections.size());
		Add(uConnectionCount);
		for (const auto& tConnection : tArea.m_vConnections)
			Add(tConnection.m_uId);
	}

	boost::uuids::detail::sha1::digest_type aDigest{};
	tSha.get_digest(aDigest);
	std::array<uint8_t, 20> aResult{};
	std::copy(std::begin(aDigest), std::end(aDigest), aResult.begin());
	return aResult;
}

size_t CMap::AddCrumbGraphNode(CNavArea* pArea, const Vector& vPos)
{
	const size_t uNode = m_vCrumbGraph.size();
	m_vCrumbGraph.push_back({ pArea, vPos });
	m_mAreaCrumbNodes[pArea].push_back(uNode);
	return uNode;
}

void CMap::AddCrumbGraphEdge(size_t uFrom, size_t uTo, bool bBidirectional, bool bRequiresDrop,
	float flDropHeight, float flApproachDistance, const Vector& vApproachDir)
{
	if (uFrom >= m_vCrumbGraph.size() || uTo >= m_vCrumbGraph.size() || uFrom == uTo)
		return;

	auto Add = [&](size_t uA, size_t uB, bool bDrop, const Vector& vDir)
		{
			const Vector vDelta = m_vCrumbGraph[uB].m_vPos - m_vCrumbGraph[uA].m_vPos;
			const float flCost = std::max(vDelta.Length(), 1.f) + (bDrop ? flDropHeight * 3.25f : 0.f);
			m_vCrumbGraph[uA].m_vEdges.push_back({ uB, flCost, bDrop, bDrop ? flDropHeight : 0.f,
				bDrop ? flApproachDistance : 0.f, bDrop ? vDir : Vector{} });
		};

	Add(uFrom, uTo, bRequiresDrop, vApproachDir);
	if (bBidirectional)
		Add(uTo, uFrom, false, {});
}

size_t CMap::FindNearestCrumbGraphNode(CNavArea* pArea, const Vector& vPos) const
{
	const auto it = m_mAreaCrumbNodes.find(pArea);
	if (it == m_mAreaCrumbNodes.end() || it->second.empty())
		return std::numeric_limits<size_t>::max();

	float flBest = FLT_MAX;
	size_t uBest = std::numeric_limits<size_t>::max();
	for (const size_t uNode : it->second)
	{
		const float flDist = m_vCrumbGraph[uNode].m_vPos.DistToSqr(vPos);
		if (flDist < flBest)
		{
			flBest = flDist;
			uBest = uNode;
		}
	}
	return uBest;
}

void CMap::BuildCrumbGraph()
{
	m_vCrumbGraph.clear();
	m_mAreaCrumbNodes.clear();
	m_vCrumbPathNodes.clear();

	constexpr float flMaxEdgeLength = 100.f;
	constexpr float flGridPitch = flMaxEdgeLength / 1.41421356237f;
	std::unordered_map<CNavArea*, std::vector<size_t>> mGridNodes;

	for (auto& tArea : m_navfile.m_vAreas)
	{
		const float flWidth = std::max(tArea.m_vSeCorner.x - tArea.m_vNwCorner.x, 0.f);
		const float flHeight = std::max(tArea.m_vSeCorner.y - tArea.m_vNwCorner.y, 0.f);
		const int iColumns = std::max(static_cast<int>(std::ceil(flWidth / flGridPitch)), 1);
		const int iRows = std::max(static_cast<int>(std::ceil(flHeight / flGridPitch)), 1);
		auto& vNodes = mGridNodes[&tArea];
		vNodes.resize(static_cast<size_t>((iColumns + 1) * (iRows + 1)));

		for (int iY = 0; iY <= iRows; ++iY)
		{
			const float flY = std::lerp(tArea.m_vNwCorner.y, tArea.m_vSeCorner.y, static_cast<float>(iY) / iRows);
			for (int iX = 0; iX <= iColumns; ++iX)
			{
				const float flX = std::lerp(tArea.m_vNwCorner.x, tArea.m_vSeCorner.x, static_cast<float>(iX) / iColumns);
				const size_t uNode = AddCrumbGraphNode(&tArea, { flX, flY, tArea.GetZ(flX, flY) });
				vNodes[static_cast<size_t>(iY * (iColumns + 1) + iX)] = uNode;

				for (const auto [iDx, iDy] : { std::pair{ -1, 0 }, std::pair{ 0, -1 }, std::pair{ -1, -1 }, std::pair{ 1, -1 } })
				{
					const int iPrevX = iX + iDx;
					const int iPrevY = iY + iDy;
					if (iPrevX < 0 || iPrevY < 0 || iPrevX > iColumns || iPrevY > iRows) continue;
					const size_t uPrevious = vNodes[static_cast<size_t>(iPrevY * (iColumns + 1) + iPrevX)];
					AddCrumbGraphEdge(uNode, uPrevious, true);
				}
			}
		}
	}

	for (auto& tArea : m_navfile.m_vAreas)
	{
		for (const auto& tConnection : tArea.m_vConnections)
		{
			CNavArea* pNextArea = tConnection.m_pArea;
			if (!pNextArea || !IsAreaValid(pNextArea) || !HasDirectConnection(&tArea, pNextArea))
				continue;

			const bool bOneWay = IsOneWay(&tArea, pNextArea);
			const NavPoints_t tPoints = DeterminePoints(&tArea, pNextArea, bOneWay);
			const DropdownHint_t tDrop = HandleDropdown(tPoints.m_vCenter, tPoints.m_vCenterNext, bOneWay);
			const size_t uFromGrid = FindNearestCrumbGraphNode(&tArea, tDrop.m_vAdjustedPos);
			const size_t uToGrid = FindNearestCrumbGraphNode(pNextArea, tPoints.m_vCenterNext);
			const size_t uFromPortal = AddCrumbGraphNode(&tArea, tDrop.m_vAdjustedPos);
			const size_t uToPortal = AddCrumbGraphNode(pNextArea, tPoints.m_vCenterNext);
			AddCrumbGraphEdge(uFromGrid, uFromPortal, true);
			AddCrumbGraphEdge(uToGrid, uToPortal, true);
			AddCrumbGraphEdge(uFromPortal, uToPortal, false, tDrop.m_bRequiresDrop, tDrop.m_flDropHeight,
				tDrop.m_flApproachDistance, tDrop.m_vApproachDir);
		}
	}

#ifdef _DEBUG
	for (const auto& tNode : m_vCrumbGraph)
	{
		assert(IsAreaValid(tNode.m_pNavArea));
		for (const auto& tEdge : tNode.m_vEdges)
		{
			assert(tEdge.m_uTo < m_vCrumbGraph.size());
			const auto& tNext = m_vCrumbGraph[tEdge.m_uTo];
			if (tNode.m_pNavArea == tNext.m_pNavArea && !tEdge.m_bRequiresDrop)
				assert(tNode.m_vPos.DistTo(tNext.m_vPos) <= 100.01f);
			else if (tNode.m_pNavArea != tNext.m_pNavArea)
				assert(HasDirectConnection(tNode.m_pNavArea, tNext.m_pNavArea));
		}
	}
#endif
}

bool CMap::RefreshCrumbGraph(bool bForce)
{
	std::lock_guard lock(m_mutex);
	if (m_eState != NavStateEnum::Active)
		return false;

	const auto aSha = ComputeNavMeshSha();
	if (!bForce && !m_vCrumbGraph.empty() && aSha == m_aCrumbGraphSha)
		return false;

	m_mVischeckCache.clear();
	m_mConnectionStuckTime.clear();
	BuildCrumbGraph();
	m_aCrumbGraphSha = aSha;
	return true;
}

bool CMap::IsGraphEdgeUsable(const CrumbGraphNode_t& tFrom, const CrumbGraphEdge_t& tEdge, const SolveContext& tCtx,
	float& flCost) const
{
	if (tEdge.m_uTo >= m_vCrumbGraph.size()) return false;
	const auto& tTo = m_vCrumbGraph[tEdge.m_uTo];
	if (!tTo.m_pNavArea || tTo.m_pNavArea->IsBlocked(tCtx.m_iTeam)) return false;
	if (!tCtx.m_bCanJump && !tEdge.m_bRequiresDrop && tTo.m_vPos.z - tFrom.m_vPos.z > 18.f) return false;

	const auto itHazard = tCtx.m_mHazardCosts.find(tTo.m_pNavArea);
	if (itHazard != tCtx.m_mHazardCosts.end() && !std::isfinite(itHazard->second)) return false;

	if (tFrom.m_pNavArea != tTo.m_pNavArea)
	{
		const auto tKey = std::pair<CNavArea*, CNavArea*>(tFrom.m_pNavArea, tTo.m_pNavArea);
		if (const auto it = m_mVischeckCache.find(tKey); it != m_mVischeckCache.end()
			&& it->second.m_eVischeckState == VischeckStateEnum::NotVisible
			&& (it->second.m_iExpireTick == 0 || it->second.m_iExpireTick > tCtx.m_iTickcount))
			return false;
	}

	flCost = tEdge.m_flCost;
	if (itHazard != tCtx.m_mHazardCosts.end())
		flCost += std::clamp(itHazard->second * 0.28f, 0.f, 650.f);
	if (m_bSkipSpawn && (tFrom.m_pNavArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)
		|| tTo.m_pNavArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE)))
		flCost += 5000.f;
	return std::isfinite(flCost) && flCost > 0.f;
}

int CMap::SolveCrumbs(const Vector& vStart, CNavArea* pStartArea, const Vector& vEnd, CNavArea* pEndArea,
	const SolveContext& tCtx, std::vector<CachedPathCrumb_t>& vOutPath, float* pflCost)
{
	vOutPath.clear();
	if (!pStartArea || !pEndArea || m_vCrumbGraph.empty()) return 2;
	const size_t uStart = FindNearestCrumbGraphNode(pStartArea, vStart);
	const size_t uEnd = FindNearestCrumbGraphNode(pEndArea, vEnd);
	if (uStart == std::numeric_limits<size_t>::max() || uEnd == std::numeric_limits<size_t>::max()) return 2;

	if (m_vCrumbPathNodes.size() != m_vCrumbGraph.size())
		m_vCrumbPathNodes.assign(m_vCrumbGraph.size(), {});
	if (++m_uCrumbQueryId == 0)
	{
		m_uCrumbQueryId = 1;
		for (auto& tNode : m_vCrumbPathNodes) tNode.m_uQueryId = 0;
	}

	auto Init = [&](size_t uNode) -> CrumbPathNode_t&
		{
			auto& tNode = m_vCrumbPathNodes[uNode];
			if (tNode.m_uQueryId != m_uCrumbQueryId)
			{
				tNode = {};
				tNode.m_uQueryId = m_uCrumbQueryId;
			}
			return tNode;
		};

	using QueueNode = std::pair<float, size_t>;
	std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> tOpen;
	auto& tStart = Init(uStart);
	tStart.m_flG = vStart.DistTo(m_vCrumbGraph[uStart].m_vPos);
	tStart.m_flF = tStart.m_flG + m_vCrumbGraph[uStart].m_vPos.DistTo(vEnd);
	tOpen.push({ tStart.m_flF, uStart });

	while (!tOpen.empty())
	{
		const auto [flF, uCurrent] = tOpen.top();
		tOpen.pop();
		auto& tCurrentPath = Init(uCurrent);
		if (flF > tCurrentPath.m_flF) continue;
		if (uCurrent == uEnd)
		{
			std::vector<size_t> vNodes;
			for (size_t uNode = uEnd; uNode != std::numeric_limits<size_t>::max(); uNode = Init(uNode).m_uParent)
				vNodes.push_back(uNode);
			std::reverse(vNodes.begin(), vNodes.end());
			for (size_t i = 0; i < vNodes.size(); ++i)
			{
				const auto& tGraphNode = m_vCrumbGraph[vNodes[i]];
				const auto& tPathNode = Init(vNodes[i]);
				CachedPathCrumb_t tCrumb{};
				tCrumb.m_pNavArea = tGraphNode.m_pNavArea;
				tCrumb.m_vPos = tGraphNode.m_vPos;
				if (tPathNode.m_pIncoming)
				{
					tCrumb.m_vApproachDir = tPathNode.m_pIncoming->m_vApproachDir;
					tCrumb.m_bRequiresDrop = tPathNode.m_pIncoming->m_bRequiresDrop;
					tCrumb.m_flDropHeight = tPathNode.m_pIncoming->m_flDropHeight;
					tCrumb.m_flApproachDistance = tPathNode.m_pIncoming->m_flApproachDistance;
				}
				vOutPath.push_back(tCrumb);
			}
			CachedPathCrumb_t tEnd{};
			tEnd.m_pNavArea = pEndArea;
			tEnd.m_vPos = vEnd;
			if (!vOutPath.empty())
			{
				Vector vDir = vEnd - vOutPath.back().m_vPos; vDir.z = 0.f;
				if (vDir.Normalize() > 0.01f) tEnd.m_vApproachDir = vDir;
			}
			vOutPath.push_back(tEnd);
			if (pflCost) *pflCost = tCurrentPath.m_flG + m_vCrumbGraph[uEnd].m_vPos.DistTo(vEnd);
			return uStart == uEnd ? 3 : 0;
		}

		const auto& tCurrent = m_vCrumbGraph[uCurrent];
		for (const auto& tEdge : tCurrent.m_vEdges)
		{
			float flEdgeCost = 0.f;
			if (!IsGraphEdgeUsable(tCurrent, tEdge, tCtx, flEdgeCost)) continue;
			auto& tNext = Init(tEdge.m_uTo);
			const float flG = tCurrentPath.m_flG + flEdgeCost;
			if (flG >= tNext.m_flG) continue;
			tNext.m_flG = flG;
			tNext.m_flF = flG + m_vCrumbGraph[tEdge.m_uTo].m_vPos.DistTo(vEnd);
			tNext.m_uParent = uCurrent;
			tNext.m_pIncoming = &tEdge;
			tOpen.push({ tNext.m_flF, tEdge.m_uTo });
		}
	}

	return 1;
}

void CMap::GetAdjacent(CNavArea* pCurrentArea, const SolveContext& tCtx, std::vector<AdjacentEntry>& vOut)
{
	if (!pCurrentArea) return;

	const int iTeam = tCtx.m_iTeam;
	const int iNow = tCtx.m_iTickcount;
	const float flTickInterval = I::GlobalVars ? I::GlobalVars->interval_per_tick : (1.0f / 66.f);
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
			&& std::isfinite(tEntry.m_flCachedCost) && tEntry.m_flCachedCost < std::numeric_limits<float>::max()
			&& !tEntry.m_vCrumbs.empty())
		{
			tPoints = tEntry.m_tPoints;
			tDropdown = tEntry.m_tDropdown;
			flBaseCost = tEntry.m_flCachedCost;
			bPassable = true;
			tEntry.m_bStuckBlacklist = false;
			m_mConnectionStuckTime.erase(tKey);
		}
		else if (bValidCache && !tEntry.m_bPassable && tEntry.m_bStuckBlacklist)
		{
			continue;
		}
		else
		{
			const bool bIsOneWay = IsOneWay(pCurrentArea, pNextArea);
			tPoints = DeterminePoints(pCurrentArea, pNextArea, bIsOneWay);
			tDropdown = HandleDropdown(tPoints.m_vCenter, tPoints.m_vCenterNext, bIsOneWay);

			const float flUpDelta = tPoints.m_vCenterNext.z - tPoints.m_vCenter.z;
			if (!tCtx.m_bCanJump && flUpDelta > 18.0f)
				continue;

			if (!tCtx.m_bIgnoreTraces && flUpDelta > PLAYER_CROUCHED_JUMP_HEIGHT)
			{
				tEntry.m_iExpireTick = iUnreachableCacheExpiry;
				tEntry.m_eVischeckState = VischeckStateEnum::NotVisible;
				tEntry.m_bPassable = false;
				tEntry.m_bStuckBlacklist = false;
				tEntry.m_flCachedCost = std::numeric_limits<float>::max();
				tEntry.m_tPoints = tPoints;
				tEntry.m_tDropdown = tDropdown;
				tEntry.m_vCrumbs.clear();
				continue;
			}

			NavPoints_t tCostPoints = tPoints;
			tCostPoints.m_vCenter = tDropdown.m_vAdjustedPos;
			bPassable = true;
			flBaseCost = EvaluateConnectionCost(pCurrentArea, pNextArea, tCostPoints, tDropdown, iTeam);

			tEntry.m_iExpireTick = iCacheExpiry;
			tEntry.m_eVischeckState = VischeckStateEnum::Visible;
			tEntry.m_bPassable = true;
			tEntry.m_bStuckBlacklist = false;
			tEntry.m_tPoints = tPoints;
			tEntry.m_tDropdown = tDropdown;
			tEntry.m_flCachedCost = flBaseCost;
			if (tEntry.m_vCrumbs.empty())
				CacheConnectionCrumbs(tEntry, pCurrentArea, pNextArea, tPoints, tDropdown);
			m_mConnectionStuckTime.erase(tKey);
		}

		if (!bPassable || !std::isfinite(flBaseCost) || flBaseCost <= 0.f)
			continue;

		float flFinalCost = std::max(flBaseCost, 1.f);
		if (m_bSkipSpawn && bTouchesSpawn)
			flFinalCost += 5000.f;

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

NavPoints_t CMap::DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea, bool /*bIsOneWay*/)
{
	const auto vCurrentCenter = pCurrentArea->m_vCenter;
	const auto vNextCenter = pNextArea->m_vCenter;

	const auto vCurrentClosest = pCurrentArea->GetNearestPoint(Vector2D(vNextCenter.x, vNextCenter.y));
	const auto vNextClosest = pNextArea->GetNearestPoint(Vector2D(vCurrentCenter.x, vCurrentCenter.y));

	Vector vTransition = vCurrentClosest;
	if (vTransition.x != vCurrentCenter.x && vTransition.y != vCurrentCenter.y
		&& vTransition.x != vNextCenter.x && vTransition.y != vNextCenter.y)
	{
		vTransition = vNextClosest;
		vTransition.z = pCurrentArea->GetNearestPoint(Vector2D(vNextClosest.x, vNextClosest.y)).z;
	}

	Vector vTransitionOffset = vCurrentCenter - vTransition;
	vTransitionOffset.z = 0.f;
	if (const float flTransitionLength = vTransitionOffset.Length(); flTransitionLength > 0.01f)
		vTransition += vTransitionOffset * (std::min(18.0f, flTransitionLength * 0.5f) / flTransitionLength);
	vTransition.z = pCurrentArea->GetNearestPoint(Vector2D(vTransition.x, vTransition.y)).z;

	Vector vCenterNext = pNextArea->GetNearestPoint(Vector2D(vTransition.x, vTransition.y));
	Vector vNextOffset = vNextCenter - vCenterNext;
	vNextOffset.z = 0.f;
	if (const float flNextLength = vNextOffset.Length(); flNextLength > 0.01f)
		vCenterNext += vNextOffset * (std::min(18.0f, flNextLength * 0.5f) / flNextLength);
	vCenterNext.z = pNextArea->GetNearestPoint(Vector2D(vCenterNext.x, vCenterNext.y)).z;

	return NavPoints_t(vCurrentCenter, vTransition, vCenterNext, vNextCenter);
}

DropdownHint_t CMap::HandleDropdown(const Vector& vCurrentPos, const Vector& vNextPos, bool bIsOneWay)
{
	DropdownHint_t tHint{};
	tHint.m_vAdjustedPos = vCurrentPos;

	const Vector vToTarget = vNextPos - vCurrentPos;
	const float flHeightDiff = vToTarget.z;

	Vector vHorizontal = vToTarget;
	vHorizontal.z = 0.f;
	const float flHorizontalLength = vHorizontal.Length();
	if (flHorizontalLength <= 1.f) return tHint;

	constexpr float kSmallDropGrace = 18.f;
	if (flHeightDiff < 0.f && -flHeightDiff > kSmallDropGrace)
	{
		const float flDropDistance = -flHeightDiff;
		const Vector vDirection = vHorizontal / flHorizontalLength;

		tHint.m_bRequiresDrop = true;
		tHint.m_flDropHeight = flDropDistance;
		tHint.m_vApproachDir = vDirection;

		float flAdvance = std::clamp(flDropDistance * 0.5f, PLAYER_WIDTH * 0.85f, PLAYER_WIDTH * 2.5f);
		flAdvance = std::min(flAdvance, flHorizontalLength * 0.95f);
		const float flMinAdvance = std::min(flHorizontalLength * 0.95f,
			std::max(PLAYER_WIDTH * (bIsOneWay ? 0.5f : 0.75f),
			flHorizontalLength * (bIsOneWay ? 0.35f : 0.5f)));
		flAdvance = std::clamp(flAdvance, flMinAdvance, flHorizontalLength * 0.95f);

		tHint.m_flApproachDistance = flAdvance;
		tHint.m_vAdjustedPos = vCurrentPos + vDirection * flAdvance;
		tHint.m_vAdjustedPos.z = vCurrentPos.z;
		return tHint;
	}

	if (!bIsOneWay && flHeightDiff > 0.f)
	{
		const Vector vDirection = vHorizontal / flHorizontalLength;
		const float flRetreat = std::clamp(flHeightDiff * 0.35f, PLAYER_WIDTH * 0.3f, PLAYER_WIDTH);
		tHint.m_vAdjustedPos = vCurrentPos - vDirection * flRetreat;
		tHint.m_vAdjustedPos.z = vCurrentPos.z;
		tHint.m_vApproachDir = -vDirection;
		tHint.m_flApproachDistance = flRetreat;
	}

	return tHint;
}

void CMap::CacheConnectionCrumbs(CachedConnection_t& tEntry, CNavArea* pFrom, CNavArea* pTo, const NavPoints_t& tPoints, const DropdownHint_t& tDropdown) const
{
	tEntry.m_vCrumbs.clear();

	auto AppendSegment = [&](const Vector& vStart, const Vector& vEnd, CNavArea* pArea, bool bRequiresDrop, float flDropHeight, float flApproachDistance, const Vector& vDropDir)
		{
			constexpr float flSpacing = 100.0f;
			const Vector vDelta = vEnd - vStart;
			Vector vPlanar = vDelta;
			vPlanar.z = 0.f;
			const float flDistance = std::max(vPlanar.Length(), std::fabs(vDelta.z));
			const int iSteps = std::max(static_cast<int>(std::ceil(flDistance / flSpacing)), 1);

			Vector vApproachDir = vPlanar;
			if (const float flLength = vApproachDir.Length(); flLength > 0.01f)
				vApproachDir /= flLength;
			else
				vApproachDir = {};

			for (int iStep = 1; iStep <= iSteps; ++iStep)
			{
				CachedPathCrumb_t tCrumb{};
				tCrumb.m_pNavArea = pArea;
				tCrumb.m_vPos = vStart + vDelta * (static_cast<float>(iStep) / iSteps);
				tCrumb.m_vPos.z = pArea->GetZ(tCrumb.m_vPos.x, tCrumb.m_vPos.y);
				tCrumb.m_vApproachDir = vApproachDir;
				tCrumb.m_bRequiresDrop = bRequiresDrop && iStep == iSteps;
				tCrumb.m_flDropHeight = tCrumb.m_bRequiresDrop ? flDropHeight : 0.f;
				tCrumb.m_flApproachDistance = tCrumb.m_bRequiresDrop ? flApproachDistance : 0.f;
				if (tCrumb.m_bRequiresDrop && vDropDir.LengthSqr() > 0.f)
					tCrumb.m_vApproachDir = vDropDir;
				tEntry.m_vCrumbs.push_back(tCrumb);
			}
		};

	AppendSegment(tPoints.m_vCenter, tDropdown.m_vAdjustedPos, pFrom,
		tDropdown.m_bRequiresDrop, tDropdown.m_flDropHeight, tDropdown.m_flApproachDistance, tDropdown.m_vApproachDir);

	if (tDropdown.m_bRequiresDrop)
		AppendSegment(tDropdown.m_vAdjustedPos, tPoints.m_vCenterNext, pTo, false, 0.f, 0.f, {});

	AppendSegment(tDropdown.m_bRequiresDrop ? tPoints.m_vCenterNext : tDropdown.m_vAdjustedPos,
		tPoints.m_vNext, pTo, false, 0.f, 0.f, {});
}

const std::vector<CachedPathCrumb_t>* CMap::GetConnectionCrumbs(CNavArea* pFrom, CNavArea* pTo) const
{
	const auto it = m_mVischeckCache.find({ pFrom, pTo });
	if (it == m_mVischeckCache.end() || it->second.m_vCrumbs.empty())
		return nullptr;
	return &it->second.m_vCrumbs;
}

bool CMap::IsOneWay(CNavArea* pFrom, CNavArea* pTo) const
{
	if (!pFrom || !pTo) return true;
	bool bBackConnected = false;
	for (const auto& tBack : pTo->m_vConnections)
		if (tBack.m_pArea == pFrom) { bBackConnected = true; break; }
	return !(bBackConnected && pTo->m_flMaxZ > pFrom->m_flMinZ - PLAYER_CROUCHED_JUMP_HEIGHT);
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
	float flBestTightScore = FLT_MAX; CNavArea* pBestTight = nullptr;
	float flBestOverlapScore = FLT_MAX; CNavArea* pBestOverlap = nullptr;
	float flBestScore = FLT_MAX; CNavArea* pBest = nullptr;

	for (auto& tArea : m_navfile.m_vAreas)
	{
		bool bTight = false;
		const float flScore = GetNearestAreaScore(tArea, vPos, bLocalOrigin, &bTight);
		const bool bOverlapping = tArea.IsOverlapping(vPos);

		if (bOverlapping)
		{
			if (bTight && flScore < flBestTightScore) { flBestTightScore = flScore; pBestTight = &tArea; }
			if (flScore < flBestOverlapScore) { flBestOverlapScore = flScore; pBestOverlap = &tArea; }
		}
		if (flScore < flBestScore) { flBestScore = flScore; pBest = &tArea; }
	}

	if (pBestTight)   return pBestTight;
	if (pBestOverlap) return pBestOverlap;
	return pBest;
}
