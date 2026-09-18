#include "../SDK/SDK.h"

#include "../SDK/Definitions/Misc/TraceInfo.h"

namespace
{
	uintptr_t s_uVPhysicsStart = 0;
	uintptr_t s_uVPhysicsEnd = 0;

	void CacheVPhysicsRange()
	{
		if (s_uVPhysicsEnd)
			return;

		HMODULE hMod = nullptr;
		if (I::PhysicsCollision)
		{
			GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(I::PhysicsCollision),
				&hMod);
		}
		if (!hMod)
			hMod = GetModuleHandleW(L"vphysics.dll");
		if (!hMod)
			return;

		const auto pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
		const auto pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(hMod) + pDos->e_lfanew);
		s_uVPhysicsStart = reinterpret_cast<uintptr_t>(hMod);
		s_uVPhysicsEnd = s_uVPhysicsStart + pNt->OptionalHeader.SizeOfImage;
	}

	bool IsCollideReadable(const CPhysCollide* pCollide)
	{
		if (!pCollide)
			return false;

		__try
		{
			const auto pVTable = *reinterpret_cast<void** const*>(pCollide);
			if (!pVTable)
				return false;

			const auto uVTable = reinterpret_cast<uintptr_t>(pVTable);
			if (s_uVPhysicsEnd && (uVTable < s_uVPhysicsStart || uVTable >= s_uVPhysicsEnd))
				return false;

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

	if (!s_uVPhysicsEnd)
		CacheVPhysicsRange();
	if (!pCollide || !IsCollideReadable(pCollide))
	{
		WriteNoHitTrace(ray, ptr);
		return;
	}

	CALL_ORIGINAL(rcx, ray, contentsMask, pConvexInfo, pCollide, collideOrigin, collideAngles, ptr);
}
