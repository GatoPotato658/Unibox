#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"


MAKE_HOOK(CBaseAnimating_SetupBones, S::CBaseAnimating_SetupBones(), bool,
	void* rcx, matrix3x4* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
	DEBUG_RETURN(CBaseAnimating_SetupBones, rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	if (G::Unload || !Vars::Misc::Game::SetupBonesOptimization.Value || F::Backtrack.IsSettingUpBones() || !rcx)
		return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	const auto pAnimating = reinterpret_cast<CBaseEntity*>(uintptr_t(rcx) - 8);
	if (!pAnimating->IsPlayer() || pAnimating->entindex() == I::EngineClient->GetLocalPlayer())
		return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	if (pBoneToWorldOut)
	{
		auto pHeader = pAnimating->GetStudiomodel();
		auto& aBones = pAnimating->As<CBaseAnimating>()->m_CachedBoneData();
		if (!pHeader || aBones.Count() != pHeader->numbones)
			return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
		if (nMaxBones >= aBones.Count())
			memcpy(pBoneToWorldOut, aBones.Base(), sizeof(matrix3x4) * aBones.Count());
		else
			return false;
	}

	return true;
}
