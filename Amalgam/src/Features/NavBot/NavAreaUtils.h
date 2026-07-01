#pragma once
#include "../../SDK/SDK.h"

class CNavArea;

namespace NavAreaUtils
{
	bool FindClosestHidingSpot(
		CNavArea* pArea,
		const Vector& vVischeckPoint,
		int iRecursionCount,
		std::pair<CNavArea*, int>& tOut,
		bool bVischeck = true,
		int iRecursionIndex = 0);
}