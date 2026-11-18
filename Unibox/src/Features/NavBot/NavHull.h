#pragma once
#include "Map.h"
#include <vector>

struct PathHullSegment_t
{
	Vector m_vStart;
	Vector m_vEnd;
	Vector m_vMins;
	Vector m_vMaxs;
	Vector m_vHit;
	std::vector<Vector> m_vProfile;
	bool m_bBlocked = false;
	bool m_bGap = false;
	bool m_bSkipped = false;
	bool m_bTight = false;
	bool m_bSnug = false;
	bool m_bAdjusted = false;
};

namespace NavHull
{
	inline constexpr float kHullWalkStep = 10.f;
	inline constexpr float kHullSurfaceTolerance = PLAYER_STEP_HEIGHT * 2.f;
	inline constexpr float kHullStandTop = PLAYER_HEIGHT - PLAYER_STEP_HEIGHT;
	inline constexpr float kHullDuckTop = PLAYER_DUCK_HEIGHT - PLAYER_STEP_HEIGHT;

	bool SweepHull(ITraceFilter* pFilter, const Vector& vFrom, const Vector& vTo, float flHalfWidth, float flTop, CGameTrace& tTrace);
	bool DropHullToFloor(ITraceFilter* pFilter, const Vector& vRaised, float flHalfWidth, float flTop, float flLowestZ, Vector& vFloor);
	bool WalkHull(ITraceFilter* pFilter, const Vector& vFrom, const Vector& vTo, float flHalfWidth, bool bAllowCrouch,
		bool& bCrouch, PathHullSegment_t& tSegment, CBaseEntity*& pHit);
}
