#include "NavHull.h"

namespace NavHull
{
	bool SweepHull(ITraceFilter* pFilter, const Vector& vFrom, const Vector& vTo, float flHalfWidth, float flTop, CGameTrace& tTrace)
	{
		SDK::TraceHull(vFrom, vTo, { -flHalfWidth, -flHalfWidth, 0.f }, { flHalfWidth, flHalfWidth, flTop }, MASK_PLAYERSOLID, pFilter, &tTrace);
		return !tTrace.startsolid && !tTrace.allsolid && tTrace.fraction >= 1.f;
	}

	bool DropHullToFloor(ITraceFilter* pFilter, const Vector& vRaised, float flHalfWidth, float flTop, float flLowestZ, Vector& vFloor)
	{
		CGameTrace tTrace{};
		SDK::TraceHull(vRaised, { vRaised.x, vRaised.y, flLowestZ }, { -flHalfWidth, -flHalfWidth, 0.f }, { flHalfWidth, flHalfWidth, flTop }, MASK_PLAYERSOLID, pFilter, &tTrace);
		if (tTrace.startsolid || tTrace.allsolid || tTrace.fraction >= 1.f)
			return false;
		vFloor = tTrace.endpos;
		return true;
	}

	bool WalkHull(ITraceFilter* pFilter, const Vector& vFrom, const Vector& vTo, float flHalfWidth, bool bAllowCrouch,
		bool& bCrouch, PathHullSegment_t& tSegment, CBaseEntity*& pHit)
	{
		const Vector vStepUp = { 0.f, 0.f, PLAYER_STEP_HEIGHT };
		tSegment.m_vProfile.clear();
		Vector vDir = vTo - vFrom;
		vDir.z = 0.f;
		const float flLength = vDir.Normalize();
		const float flFloorMin = std::min(vFrom.z, vTo.z) - kHullSurfaceTolerance;
		const float flFloorMax = std::max(vFrom.z, vTo.z) + kHullSurfaceTolerance;
		Vector vPos = vFrom;
		Vector vFloor{};
		if (DropHullToFloor(pFilter, vFrom + vStepUp, flHalfWidth, bCrouch ? kHullDuckTop : kHullStandTop, vFrom.z - kHullSurfaceTolerance, vFloor))
			vPos = vFloor;
		tSegment.m_vProfile.push_back(vPos);

		const int nSteps = std::max(static_cast<int>(std::ceil(flLength / kHullWalkStep)), 1);
		for (int iStep = 1; iStep <= nSteps; ++iStep)
		{
			const float flT = std::min(static_cast<float>(iStep) * kHullWalkStep, flLength);
			const Vector vRaised = vPos + vStepUp;
			const Vector vRaisedNext = { vFrom.x + vDir.x * flT, vFrom.y + vDir.y * flT, vRaised.z };

			CGameTrace tTrace{};
			if (!SweepHull(pFilter, vRaised, vRaisedNext, flHalfWidth, bCrouch ? kHullDuckTop : kHullStandTop, tTrace))
			{
				CGameTrace tDuckTrace{};
				if (bCrouch || !bAllowCrouch || !SweepHull(pFilter, vRaised, vRaisedNext, flHalfWidth, kHullDuckTop, tDuckTrace))
				{
					tSegment.m_bBlocked = true;
					tSegment.m_vHit = tTrace.endpos - vStepUp;
					pHit = tTrace.m_pEnt;
					return false;
				}
				bCrouch = true;
			}

			if (!DropHullToFloor(pFilter, vRaisedNext, flHalfWidth, bCrouch ? kHullDuckTop : kHullStandTop, std::min(vPos.z - PLAYER_STEP_HEIGHT, flFloorMin), vFloor)
				|| vFloor.z > flFloorMax)
			{
				tSegment.m_bGap = true;
				tSegment.m_vHit = vRaisedNext - vStepUp;
				return false;
			}

			vPos = vFloor;
			tSegment.m_vProfile.push_back(vPos);
		}
		return true;
	}
}
