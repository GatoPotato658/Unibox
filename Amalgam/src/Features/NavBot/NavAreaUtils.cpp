#include "NavAreaUtils.h"

#include "NavEngine/NavEngine.h"

static bool FindClosestHidingSpotRecursive(
	CNavArea* pArea,
	const Vector& vVischeckPoint,
	int iRecursionCount,
	std::pair<CNavArea*, int>& tOut,
	bool bVischeck,
	int iRecursionIndex,
	std::vector<CNavArea*>& vVisited)
{
	if (!pArea || iRecursionCount <= 0)
		return false;

	Vector vAreaOrigin = pArea->m_vCenter;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	const int iNextIndex = iRecursionIndex + 1;
	if (bVischeck && !F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vVischeckPoint))
	{
		tOut = { pArea, iRecursionIndex };
		return true;
	}

	if (iNextIndex >= iRecursionCount)
		return false;

	std::pair<CNavArea*, int> tBestSpot{};
	for (const auto& tConnection : pArea->m_vConnections)
	{
		auto pNextArea = tConnection.m_pArea;
		if (!pNextArea)
			continue;

		if (std::find(vVisited.begin(), vVisited.end(), pNextArea) != vVisited.end())
			continue;

		vVisited.push_back(pNextArea);

		std::pair<CNavArea*, int> tSpot{};
		if (FindClosestHidingSpotRecursive(pNextArea, vVischeckPoint, iRecursionCount, tSpot, bVischeck, iNextIndex, vVisited)
			&& (!tBestSpot.first || tSpot.second < tBestSpot.second))
			tBestSpot = tSpot;
	}

	tOut = tBestSpot;
	return tBestSpot.first != nullptr;
}

namespace NavAreaUtils
{
	bool FindClosestHidingSpot(
		CNavArea* pArea,
		const Vector& vVischeckPoint,
		int iRecursionCount,
		std::pair<CNavArea*, int>& tOut,
		bool bVischeck,
		int iRecursionIndex)
	{
		std::vector<CNavArea*> vVisited{};
		vVisited.reserve(32);
		return FindClosestHidingSpotRecursive(pArea, vVischeckPoint, iRecursionCount, tOut, bVischeck, iRecursionIndex, vVisited);
	}
}
