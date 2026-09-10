#include "NavAreaUtils.h"

#include "NavEngine/NavEngine.h"
#include <queue>
#include <unordered_set>

namespace NavAreaUtils
{
	bool FindClosestHidingSpot(
		CNavArea* pArea,
		const Vector& vVischeckPoint,
		int iMaxDepth,
		std::pair<CNavArea*, int>& tOut,
		bool bVischeck,
		int iStartDepth)
	{
		tOut = {};
		if (!pArea || iMaxDepth <= iStartDepth)
			return false;
		if (!bVischeck)
		{
			tOut = { pArea, iStartDepth };
			return true;
		}

		std::queue<std::pair<CNavArea*, int>> vAreas;
		std::unordered_set<CNavArea*> vVisited;
		vAreas.push({ pArea, iStartDepth });
		vVisited.insert(pArea);

		while (!vAreas.empty())
		{
			auto [pCurrentArea, iDepth] = vAreas.front();
			vAreas.pop();

			Vector vAreaOrigin = pCurrentArea->m_vCenter;
			vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
			if (!F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vVischeckPoint))
			{
				tOut = { pCurrentArea, iDepth };
				return true;
			}

			if (iDepth + 1 >= iMaxDepth)
				continue;
			for (const auto& tConnection : pCurrentArea->m_vConnections)
				if (tConnection.m_pArea && vVisited.insert(tConnection.m_pArea).second)
					vAreas.push({ tConnection.m_pArea, iDepth + 1 });
		}
		return false;
	}
}
