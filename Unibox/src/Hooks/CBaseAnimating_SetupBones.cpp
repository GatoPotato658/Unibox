#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"

MAKE_SIGNATURE(CBaseAnimating_SetupBones, "client.dll", "48 8B C4 44 89 40 ? 48 89 50 ? 55 53", 0x0);

MAKE_HOOK(CBaseAnimating_SetupBones, S::CBaseAnimating_SetupBones(), bool,
	void* rcx, matrix3x4* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
	DEBUG_RETURN(CBaseAnimating_SetupBones, rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	const auto pAnimating = reinterpret_cast<CBaseEntity*>(uintptr_t(rcx) - 8);

	if (G::SpyBoneSnapshot && pAnimating && pAnimating->IsPlayer() && pAnimating->As<CTFPlayer>()->m_iClass() == TF_CLASS_SPY)
	{
		static CStudioHdr* pStudio = nullptr;
		static unsigned short uHandle = 0xFFFF;
		static studiohdr_t* pHeader = nullptr;
		static int iModel = -1;

		if (iModel < 0)
			iModel = I::ModelInfoClient->GetModelIndex("models/player/spy.mdl");

		const auto pModel = iModel >= 0 ? I::ModelInfoClient->GetModel(iModel) : nullptr;
		const auto pCurrentHeader = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;

		if (pStudio && pHeader != pCurrentHeader)
		{
			pStudio = nullptr;
			uHandle = 0xFFFF;
		}

		if (pCurrentHeader)
		{
			auto pAnim = pAnimating->As<CBaseAnimating>();
			const auto iOldModel = pAnim->m_nModelIndex();
			auto pOldStudio = pAnim->m_pStudioHdr();
			const auto uOldHandle = pAnim->m_hStudioHdr();

			pAnim->m_pStudioHdr() = pStudio;
			pAnim->m_hStudioHdr() = pStudio ? uHandle : 0xFFFF;
			pAnim->m_nModelIndex() = iModel;
			pAnim->InvalidateBoneCache();

			const bool bResult = CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

			pStudio = pAnim->m_pStudioHdr();
			uHandle = pAnim->m_hStudioHdr();
			pHeader = pCurrentHeader;

			pAnim->m_nModelIndex() = iOldModel;
			pAnim->m_pStudioHdr() = pOldStudio;
			pAnim->m_hStudioHdr() = uOldHandle;
			pAnim->InvalidateBoneCache();

			return bResult;
		}
	}

	if (!G::Unload && !Vars::Misc::Game::SetupBonesOptimization.Value || F::Backtrack.IsSettingUpBones())
		return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	if (!pAnimating)
		return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	auto pOwner = pAnimating->GetRootMoveParent();
	auto pEntity = pOwner ? pOwner : pAnimating;
	if (!pEntity->IsPlayer() || pEntity->entindex() == I::EngineClient->GetLocalPlayer())
		return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	if (pBoneToWorldOut)
	{
		auto& aBones = pEntity->As<CBaseAnimating>()->m_CachedBoneData();
		if (nMaxBones >= aBones.Count())
			memcpy(pBoneToWorldOut, aBones.Base(), sizeof(matrix3x4) * aBones.Count());
		else
			return false;
	}

	return true;
}
