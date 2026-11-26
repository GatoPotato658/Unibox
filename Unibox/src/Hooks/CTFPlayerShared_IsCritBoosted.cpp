#include "../SDK/SDK.h"

#include "../Features/CritHack/CritHack.h"


MAKE_HOOK(CTFPlayerShared_IsCritBoosted, S::CTFPlayerShared_IsCritBoosted(), bool,
	void* rcx)
{
	DEBUG_RETURN(CTFPlayerShared_IsCritBoosted, rcx);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CProxyModelGlowColor_OnBind_IsCritBoosted_Call();

	if (dwRetAddr != dwDesired)
		return CALL_ORIGINAL(rcx);

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || pLocal->m_Shared() != rcx || !F::CritHack.m_bCachedForceEffects)
		return CALL_ORIGINAL(rcx);

	return true;
}