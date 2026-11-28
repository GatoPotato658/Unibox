#include "../SDK.h"

#include "../../Features/Aimbot/AimbotProjectile/ProjProfiler.h"

bool SDK::W2S(const Vec3& vOrigin, Vec3& vScreen, bool bAlways)
{
	const auto& worldToScreen = H::Draw.m_mWorldToProjection.As3x4();

	float flW = worldToScreen[3][0] * vOrigin.x + worldToScreen[3][1] * vOrigin.y + worldToScreen[3][2] * vOrigin.z + worldToScreen[3][3];
	vScreen.z = 0;

	bool bOnScreen = flW > 0.001f;
	if (bAlways || bOnScreen)
	{
		const float fl1DBw = 1 / fabs(flW);
		vScreen.x = (H::Draw.m_nScreenW / 2.f) + ((worldToScreen[0][0] * vOrigin.x + worldToScreen[0][1] * vOrigin.y + worldToScreen[0][2] * vOrigin.z + worldToScreen[0][3]) * fl1DBw) * H::Draw.m_nScreenW / 2 + 0.5f;
		vScreen.y = (H::Draw.m_nScreenH / 2.f) - ((worldToScreen[1][0] * vOrigin.x + worldToScreen[1][1] * vOrigin.y + worldToScreen[1][2] * vOrigin.z + worldToScreen[1][3]) * fl1DBw) * H::Draw.m_nScreenH / 2 + 0.5f;
	}

	return bOnScreen;
}

bool SDK::ClipSegmentToScreen(const Vec3& vStart, const Vec3& vEnd, Vec3& vScreenStart, Vec3& vScreenEnd)
{
	constexpr float NEAR_W = 0.01f;

	const auto& worldToScreen = H::Draw.m_mWorldToProjection.As3x4();
	const auto fnW = [&](const Vec3& vPoint)
	{
		return worldToScreen[3][0] * vPoint.x + worldToScreen[3][1] * vPoint.y + worldToScreen[3][2] * vPoint.z + worldToScreen[3][3];
	};

	const float flStartW = fnW(vStart), flEndW = fnW(vEnd);
	if (flStartW < NEAR_W && flEndW < NEAR_W)
		return false;

	Vec3 vVisibleStart = vStart, vVisibleEnd = vEnd;
	if (flStartW < NEAR_W)
		vVisibleStart = vStart + (vEnd - vStart) * ((NEAR_W - flStartW) / (flEndW - flStartW));
	else if (flEndW < NEAR_W)
		vVisibleEnd = vEnd + (vStart - vEnd) * ((NEAR_W - flEndW) / (flStartW - flEndW));

	return W2S(vVisibleStart, vScreenStart) && W2S(vVisibleEnd, vScreenEnd);
}

bool SDK::IsOnScreen(CBaseEntity* pEntity, const matrix3x4& mTransform, float* pLeft, float* pRight, float* pTop, float* pBottom, bool bAll)
{
	float flLeft = 0.f, flRight = 0.f, flTop = 0.f, flBottom = 0.f;

	Vec3 vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();
	const Vec3 vPoints[] = {
		Vec3(0.f, 0.f, vMins.z),
		Vec3(0.f, 0.f, vMaxs.z),
		Vec3(vMins.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMins.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f)
	};

	bool bInit = false;
	for (int n = 0; n < 6; n++)
	{
		Vec3 vPoint; Math::VectorTransform(vPoints[n], mTransform, vPoint);

		Vec3 vScreen;
		if (!W2S(vPoint, vScreen))
		{
			if (bAll)
				return false;
			continue;
		}

		flLeft = bInit ? std::min(flLeft, vScreen.x) : vScreen.x;
		flRight = bInit ? std::max(flRight, vScreen.x) : vScreen.x;
		flTop = bInit ? std::max(flTop, vScreen.y) : vScreen.y;
		flBottom = bInit ? std::min(flBottom, vScreen.y) : vScreen.y;
		bInit = true;
	}
	if (!bInit)
		return false;

	if (pLeft) *pLeft = flLeft;
	if (pRight) *pRight = flRight;
	if (pTop) *pTop = flTop;
	if (pBottom) *pBottom = flBottom;

	return !(flRight < 0 || flLeft > H::Draw.m_nScreenW || flTop < 0 || flBottom > H::Draw.m_nScreenH);
}

bool SDK::IsOnScreen(CBaseEntity* pEntity, const Vec3& vOrigin, bool bAll)
{
	float flLeft = 0.f, flRight = 0.f, flTop = 0.f, flBottom = 0.f;

	Vec3 vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();
	const Vec3 vPoints[] = {
		Vec3(0.f, 0.f, vMins.z),
		Vec3(0.f, 0.f, vMaxs.z),
		Vec3(vMins.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMins.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f)
	};

	bool bInit = false;
	for (int n = 0; n < 6; n++)
	{
		Vec3 vPoint = vOrigin + vPoints[n];

		Vec3 vScreen;
		if (!W2S(vPoint, vScreen))
		{
			if (bAll)
				return false;
			continue;
		}

		flLeft = bInit ? std::min(flLeft, vScreen.x) : vScreen.x;
		flRight = bInit ? std::max(flRight, vScreen.x) : vScreen.x;
		flTop = bInit ? std::max(flTop, vScreen.y) : vScreen.y;
		flBottom = bInit ? std::min(flBottom, vScreen.y) : vScreen.y;
		bInit = true;
	}
	if (!bInit)
		return false;

	return !(flRight < 0 || flLeft > H::Draw.m_nScreenW || flTop < 0 || flBottom > H::Draw.m_nScreenH);
}

bool SDK::IsOnScreen(CBaseEntity* pEntity, bool bShouldGetOwner)
{
	if (bShouldGetOwner)
	{
		if (auto pOwner = pEntity->m_hOwnerEntity().Get())
			pEntity = pOwner;
	}

	return IsOnScreen(pEntity, pEntity->GetAbsOrigin());
}

void SDK::Trace(const Vec3& vStart, const Vec3& vEnd, unsigned int nMask, ITraceFilter* pFilter, CGameTrace* pTrace)
{
	PROJ_ZONE_ROOT(Trace);

	Ray_t ray;
	ray.Init(vStart, vEnd);
	I::EngineTrace->TraceRay(ray, nMask, pFilter, pTrace);

#ifdef DEBUG_TRACES
	if (Vars::Debug::VisualizeTraces.Value)
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vStart, Vars::Debug::VisualizeTraceHits.Value ? pTrace->endpos : vEnd), I::GlobalVars->curtime + 0.015f, Color_t(), bool(GetAsyncKeyState(VK_MENU) & 0x8000));
#endif
}

void SDK::TraceHull(const Vec3& vStart, const Vec3& vEnd, const Vec3& vHullMin, const Vec3& vHullMax, unsigned int nMask, ITraceFilter* pFilter, CGameTrace* pTrace)
{
	PROJ_ZONE_ROOT(TraceHull);

	Ray_t ray;
	ray.Init(vStart, vEnd, vHullMin, vHullMax);
	I::EngineTrace->TraceRay(ray, nMask, pFilter, pTrace);

#ifdef DEBUG_TRACES
	if (Vars::Debug::VisualizeTraces.Value)
	{
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vStart, Vars::Debug::VisualizeTraceHits.Value ? pTrace->endpos : vEnd), I::GlobalVars->curtime + 0.015f, Color_t(), bool(GetAsyncKeyState(VK_MENU) & 0x8000));
		if (!(vHullMax - vHullMin).IsZero())
		{
			G::BoxStorage.emplace_back(vStart, vHullMin, vHullMax, Vec3(), I::GlobalVars->curtime + 0.015f, Color_t(), Color_t(0, 0, 0, 0), bool(GetAsyncKeyState(VK_MENU) & 0x8000));
			G::BoxStorage.emplace_back(Vars::Debug::VisualizeTraceHits.Value ? pTrace->endpos : vEnd, vHullMin, vHullMax, Vec3(), I::GlobalVars->curtime + 0.015f, Color_t(), Color_t(0, 0, 0, 0), bool(GetAsyncKeyState(VK_MENU) & 0x8000));
		}
	}
#endif
}

bool SDK::VisPos(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterHitscan filter(pSkip);
	Trace(vFrom, vTo, nMask, &filter, &trace);
	if (trace.DidHit())
		return trace.m_pEnt && trace.m_pEnt == pEntity;
	return true;
}
bool SDK::VisPosCollideable(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterCollideable filter(pSkip);
	filter.m_iType = SKIP_CHECK;
	Trace(vFrom, vTo, nMask, &filter, &trace);
	if (trace.DidHit())
		return trace.m_pEnt && trace.m_pEnt == pEntity;
	return true;
}
bool SDK::VisPosWorld(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter(pSkip);
	Trace(vFrom, vTo, nMask, &filter, &trace);
	if (trace.DidHit())
		return trace.m_pEnt && trace.m_pEnt == pEntity;
	return true;
}

Vec3 SDK::PredictOrigin(const Vec3& vOrigin, const Vec3& vVelocity, float flLatency, bool bTrace, const Vec3& vMins, const Vec3& vMaxs, unsigned int nMask, float flNormal)
{
	if (vVelocity.IsZero() || !flLatency)
		return vOrigin;

	Vec3 vOut = vOrigin + vVelocity * flLatency;
	if (!bTrace)
		return vOut;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	SDK::TraceHull(vOrigin, vOut, vMins, vMaxs, nMask, &filter, &trace);
	return trace.endpos + (flNormal ? trace.plane.normal * flNormal : Vec3());
}

bool SDK::PredictOrigin(Vec3& vOut, const Vec3& vOrigin, const Vec3& vVelocity, float flLatency, bool bTrace, const Vec3& vMins, const Vec3& vMaxs, unsigned int nMask, float flNormal)
{
	vOut = vOrigin;
	if (vVelocity.IsZero() || !flLatency)
		return true;

	vOut = vOrigin + vVelocity * flLatency;
	if (!bTrace)
		return true;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	SDK::TraceHull(vOrigin, vOut, vMins, vMaxs, nMask, &filter, &trace);
	vOut = trace.endpos + (flNormal ? trace.plane.normal * flNormal : Vec3());
	return !trace.DidHit();
}
