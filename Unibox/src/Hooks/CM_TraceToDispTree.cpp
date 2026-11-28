#include "../SDK/SDK.h"

#include "../SDK/Definitions/Render/TraceInfo.h"

#include <cstddef>
#include <utility>


#define DISPCOLL_DIST_EPSILON 0.03125f

static_assert(offsetof(TraceInfo_t, m_contents) == 296);
static_assert(sizeof(CDispCollTri) == 24);
static_assert(offsetof(CDispCollTri, m_vecNormal) == 8);

static thread_local bool s_bTwoSided = false;
static thread_local CDispCollTri* s_pFlippedTri = nullptr;

MAKE_HOOK(CM_TraceToDispTree_True, S::CM_TraceToDispTree_True(), void,
	TraceInfo_t* pTraceInfo, CDispCollTree* pDispTree)
{
	DEBUG_RETURN(CM_TraceToDispTree_True, pTraceInfo, pDispTree);

	const bool bPrevious = std::exchange(s_bTwoSided, (pTraceInfo->m_contents & CONTENTS_DISPSOLID) != 0);
	CALL_ORIGINAL(pTraceInfo, pDispTree);
	s_bTwoSided = bPrevious;
}

MAKE_HOOK(CM_TraceToDispTree_False, S::CM_TraceToDispTree_False(), void,
	TraceInfo_t* pTraceInfo, CDispCollTree* pDispTree)
{
	DEBUG_RETURN(CM_TraceToDispTree_False, pTraceInfo, pDispTree);

	const bool bPrevious = std::exchange(s_bTwoSided, (pTraceInfo->m_contents & CONTENTS_DISPSOLID) != 0);
	CALL_ORIGINAL(pTraceInfo, pDispTree);
	s_bTwoSided = bPrevious;
}

MAKE_HOOK(IntersectRayWithTriangle, S::IntersectRayWithTriangle(), float,
	const Ray_t& ray, const Vector& v1, const Vector& v2, const Vector& v3, bool oneSided)
{
	TRACK_HOOK;

	if (s_bTwoSided)
		oneSided = false;

	return CALL_ORIGINAL(ray, v1, v2, v3, oneSided);
}

MAKE_HOOK(CDispCollTree_SweepAABBTriIntersect, S::CDispCollTree_SweepAABBTriIntersect(), void,
	void* rcx, const Ray_t& ray, const Vector& rayDir, int iTri, CDispCollTri* pTri, CBaseTrace* pTrace)
{
	DEBUG_RETURN(CDispCollTree_SweepAABBTriIntersect, rcx, ray, rayDir, iTri, pTri, pTrace);

	if (!s_bTwoSided || !(pTri->m_vecNormal.Dot(ray.m_Delta) > DISPCOLL_DIST_EPSILON))
		return CALL_ORIGINAL(rcx, ray, rayDir, iTri, pTri, pTrace);

	auto tTri = *pTri;
	tTri.m_vecNormal = -tTri.m_vecNormal;
	auto* pPrevious = std::exchange(s_pFlippedTri, &tTri);
	CALL_ORIGINAL(rcx, ray, rayDir, iTri, &tTri, pTrace);
	s_pFlippedTri = pPrevious;
}

MAKE_HOOK(CDispCollTree_EdgeCrossAxis_0, S::CDispCollTree_EdgeCrossAxis_0(), bool,
	void* rcx, const Ray_t& ray, unsigned short iPlane, CDispCollHelper* pHelper)
{
	DEBUG_RETURN(CDispCollTree_EdgeCrossAxis_0, rcx, ray, iPlane, pHelper);

	if (auto* pTri = std::exchange(s_pFlippedTri, nullptr))
		pTri->m_vecNormal = -pTri->m_vecNormal;

	return CALL_ORIGINAL(rcx, ray, iPlane, pHelper);
}
