#include "NavBotJobs.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace NavJobUtils
{
	bool TryNavToAreaScores(std::vector<NavAreaScore_t>& vAreaScores, PriorityListEnum::PriorityListEnum ePriority, bool bLowestScoreFirst, size_t nMaxAttempts)
	{
		if (vAreaScores.empty())
			return false;

		std::sort(vAreaScores.begin(), vAreaScores.end(), [bLowestScoreFirst](const NavAreaScore_t& a, const NavAreaScore_t& b)
			{
				return bLowestScoreFirst ? a.m_flScore < b.m_flScore : a.m_flScore > b.m_flScore;
			});

		size_t nAttempts = 0;
		for (const auto& tAreaScore : vAreaScores)
		{
			if (!tAreaScore.m_pArea)
				continue;

			if (nMaxAttempts && nAttempts++ >= nMaxAttempts)
				break;

			if (F::NavEngine.NavTo(tAreaScore.m_pArea->m_vCenter, ePriority))
				return true;
		}

		return false;
	}
}

namespace NavAreaUtils
{
	bool FindClosestHidingSpot(CNavArea* pArea, const Vector& vVischeckPoint, int iMaxDepth, std::pair<CNavArea*, int>& tOut, bool bVischeck, int iStartDepth)
	{
		tOut = {};
		if (!pArea || iMaxDepth <= iStartDepth)
			return false;
		if (!bVischeck)
		{
			tOut = { pArea, iStartDepth };
			return true;
		}

		std::queue<std::pair<CNavArea*, int>> qAreas;
		std::unordered_set<CNavArea*> setVisited;
		qAreas.push({ pArea, iStartDepth });
		setVisited.insert(pArea);

		while (!qAreas.empty())
		{
			auto [pCurrentArea, iDepth] = qAreas.front();
			qAreas.pop();

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
				if (tConnection.m_pArea && setVisited.insert(tConnection.m_pArea).second)
					qAreas.push({ tConnection.m_pArea, iDepth + 1 });
		}
		return false;
	}
}
