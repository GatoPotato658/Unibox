#include "../SDK/SDK.h"

#include "../SDK/Definitions/Misc/TraceInfo.h"

namespace
{
	bool IsCollideReadable(const CPhysCollide* pCollide)
	{
		__try
		{
			const auto pVTable = *reinterpret_cast<void***>(const_cast<CPhysCollide*>(pCollide));
			if (!pVTable)
				return false;

			HMODULE hOwner = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(pVTable), &hOwner) || hOwner != GetModuleHandleW(L"vphysics.dll"))
				return false;

			volatile auto pUnused = pVTable[12];
			pUnused = pVTable[14];
			(void)pUnused;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void WriteNoHitTrace(const Ray_t& ray, trace_t* pTrace)
	{
		if (!pTrace)
			return;

		*pTrace = {};
		pTrace->startpos = ray.m_Start;
		pTrace->endpos = ray.m_Start + ray.m_Delta;
		pTrace->fraction = 1.f;
		pTrace->fractionleftsolid = 1.f;
		pTrace->contents = 0;
	}
}

MAKE_HOOK(IPhysicsCollision_TraceBox, U::Memory.GetVirtual(I::PhysicsCollision, 31), void,
	void* rcx, const Ray_t& ray, unsigned int contentsMask, IConvexInfo* pConvexInfo, const CPhysCollide* pCollide, const Vec3& collideOrigin, const QAngle& collideAngles, trace_t* ptr)
	{
		DEBUG_RETURN(IPhysicsCollision_TraceBox, rcx, ray, contentsMask, pConvexInfo, pCollide, collideOrigin, collideAngles, ptr);

	if (!pCollide || !IsCollideReadable(pCollide))
	{
		WriteNoHitTrace(ray, ptr);
		return;
	}

	CALL_ORIGINAL(rcx, ray, contentsMask, pConvexInfo, pCollide, collideOrigin, collideAngles, ptr);
}
