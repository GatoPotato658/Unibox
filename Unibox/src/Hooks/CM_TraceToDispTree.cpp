#include "../SDK/SDK.h"

#include "../SDK/Definitions/Render/TraceInfo.h"


#define DISPCOLL_DIST_EPSILON 0.03125f

static int s_iMask = 0;
static std::optional<Vec3> s_vOriginal = {};
static CDispCollTri* s_pTri = nullptr;

MAKE_HOOK(CM_TraceToDispTree_True, S::CM_TraceToDispTree_True(), float,
	TraceInfo_t* pTraceInfo, CDispCollTree* pDispTree, float startFrac, float endFrac)
{
	DEBUG_RETURN(CM_TraceToDispTree_True, pTraceInfo, pDispTree, startFrac, endFrac);

	s_iMask = pTraceInfo->m_contents;

	return CALL_ORIGINAL(pTraceInfo, pDispTree, startFrac, endFrac);
}

MAKE_HOOK(CM_TraceToDispTree_False, S::CM_TraceToDispTree_False(), float,
	TraceInfo_t* pTraceInfo, CDispCollTree* pDispTree, float startFrac, float endFrac)
{
	DEBUG_RETURN(CM_TraceToDispTree_False, pTraceInfo, pDispTree, startFrac, endFrac);

	s_iMask = pTraceInfo->m_contents;

	return CALL_ORIGINAL(pTraceInfo, pDispTree, startFrac, endFrac);
}

MAKE_HOOK(IntersectRayWithTriangle, S::IntersectRayWithTriangle(), float,
	const Ray_t& ray, const Vector& v1, const Vector& v2, const Vector& v3, bool oneSided)
{
	TRACK_HOOK;

	if (s_iMask & CONTENTS_DISPSOLID)
		oneSided = false;

	return CALL_ORIGINAL(ray, v1, v2, v3, oneSided);
}

MAKE_HOOK(CDispCollTree_SweepAABBTriIntersect, S::CDispCollTree_SweepAABBTriIntersect(), void,
	void* rcx, const Ray_t& ray, const Vector& rayDir, int iTri, CDispCollTri* pTri, CBaseTrace* pTrace)
{
	DEBUG_RETURN(CDispCollTree_SweepAABBTriIntersect, rcx, ray, rayDir, iTri, pTri, pTrace);

	if (s_iMask & CONTENTS_DISPSOLID && pTri->m_vecNormal.Dot(ray.m_Delta) > DISPCOLL_DIST_EPSILON)
	{
		s_vOriginal = pTri->m_vecNormal, s_pTri = pTri;
		pTri->m_vecNormal = -pTri->m_vecNormal;
	}

	CALL_ORIGINAL(rcx, ray, rayDir, iTri, pTri, pTrace);

	if (s_vOriginal)
		pTri->m_vecNormal = *s_vOriginal, s_vOriginal = std::nullopt;
}

MAKE_HOOK(CDispCollTree_EdgeCrossAxis_0, S::CDispCollTree_EdgeCrossAxis_0(), void,
	void* rcx, const Ray_t& ray, unsigned short iPlane, CDispCollHelper* pHelper)
{
	DEBUG_RETURN(CDispCollTree_EdgeCrossAxis_0, rcx, ray, iPlane, pHelper);

	if (s_vOriginal)
		s_pTri->m_vecNormal = *s_vOriginal, s_vOriginal = std::nullopt;

	CALL_ORIGINAL(rcx, ray, iPlane, pHelper);
}