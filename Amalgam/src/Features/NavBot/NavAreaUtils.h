#pragma once
#include "../../SDK/SDK.h"

class CNavArea;

namespace NavAreaUtils
{
	bool FindClosestHidingSpot(
		CNavArea* pArea,
		const Vector& vVischeckPoint,
		int iMaxDepth,
		std::pair<CNavArea*, int>& tOut,
		bool bVischeck = true,
		int iStartDepth = 0);
}
