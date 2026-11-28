#include "../SDK/SDK.h"

#ifndef TEXTMODE
#include "../Features/Visuals/UltraToaster/UltraToaster.h"

#include <cstddef>

static_assert(sizeof(DrawModelInfo_t) == 0x1F0);
static_assert(offsetof(DrawModelInfo_t, m_Lod) == 0x30);

MAKE_HOOK(CBaseFlex_SetupWeights, S::CBaseFlex_SetupWeights(), void,
	IClientRenderable* rcx, const matrix3x4* pBoneToWorld, int iCount, float* pFlexWeights, float* pFlexDelayedWeights)
{
	DEBUG_RETURN(CBaseFlex_SetupWeights, rcx, pBoneToWorld, iCount, pFlexWeights, pFlexDelayedWeights);

	if (!pFlexWeights || iCount <= 0 || iCount > MAXSTUDIOFLEXDESC
		|| !Vars::Visuals::UltraToaster::ModelFlexes.Value || !UltraToaster::IsActive())
		return CALL_ORIGINAL(rcx, pBoneToWorld, iCount, pFlexWeights, pFlexDelayedWeights);

	std::fill_n(pFlexWeights, iCount, 0.f);
	if (pFlexDelayedWeights)
		std::fill_n(pFlexDelayedWeights, iCount, 0.f);
}

MAKE_HOOK(CStudioRender_DrawModel, U::Memory.GetVirtual(I::StudioRender, 29), void,
	IStudioRender* rcx, DrawModelResults_t* pResults, const DrawModelInfo_t& info, matrix3x4* pBoneToWorld,
	float* pFlexWeights, float* pFlexDelayedWeights, const Vector& modelOrigin, int flags)
{
	DEBUG_RETURN(CStudioRender_DrawModel, rcx, pResults, info, pBoneToWorld, pFlexWeights, pFlexDelayedWeights, modelOrigin, flags);

	if (Vars::Visuals::UltraToaster::ModelFlexes.Value && !(flags & STUDIORENDER_DRAW_NO_FLEXES)
		&& info.m_pStudioHdr && info.m_pStudioHdr->numflexdesc > 0 && UltraToaster::IsActive())
		flags |= STUDIORENDER_DRAW_NO_FLEXES;

	CALL_ORIGINAL(rcx, pResults, info, pBoneToWorld, pFlexWeights, pFlexDelayedWeights, modelOrigin, flags);
}
#endif

MAKE_HOOK(CStudioRender_DrawModelStaticProp, U::Memory.GetVirtual(I::StudioRender, 30), void,
	void* rcx, const DrawModelInfo_t& info, const matrix3x4& modelToWorld, int flags)
{
	DEBUG_RETURN(CStudioRender_DrawModelStaticProp, rcx, info, modelToWorld, flags);

#ifndef TEXTMODE
	const bool bUltraToaster = UltraToaster::IsActive();
	const bool bSkipShadows = bUltraToaster && Vars::Visuals::UltraToaster::PropShadows.Value;
	if (bSkipShadows && (flags & STUDIORENDER_SHADOWDEPTHTEXTURE))
		return;

	if (Vars::Visuals::World::NearPropFade.Value && !SDK::CleanScreenshot())
	{
		if (auto pLocal = H::Entities.GetLocal())
		{
			Vec3 vOrigin = { modelToWorld[0][3], modelToWorld[1][3], modelToWorld[2][3] };

			const float flDistance = pLocal->m_vecOrigin().DistTo(vOrigin);

			float flAlpha = 1.f;
			if (flDistance < 300.0f)
				flAlpha = Math::RemapVal(flDistance, 150.0f, 300.0f, 0.15f, 1.f);
			I::StudioRender->SetAlphaModulation(flAlpha);
		}
	}

	if (bSkipShadows)
		flags |= STUDIORENDER_DRAW_NO_SHADOWS;

	if (bUltraToaster && Vars::Visuals::UltraToaster::PropLOD.Value
		&& !(flags & (STUDIORENDER_SHADOWDEPTHTEXTURE | STUDIORENDER_SSAODEPTHTEXTURE)))
	{
		const int iLod = UltraToaster::LowerLod(info.m_pHardwareData, info.m_Lod,
			std::max(1, Vars::Visuals::UltraToaster::PropLODBias.Value));
		if (iLod != info.m_Lod)
		{
			auto tInfo = info;
			tInfo.m_Lod = iLod;
			return CALL_ORIGINAL(rcx, tInfo, modelToWorld, flags);
		}
	}

	CALL_ORIGINAL(rcx, info, modelToWorld, flags);
#endif
}

#ifndef TEXTMODE
MAKE_HOOK(CStudioRender_DrawStaticPropShadows, U::Memory.GetVirtual(I::StudioRender, 32), void,
	void* rcx, const DrawModelInfo_t& info, const matrix3x4& modelToWorld, int flags)
{
	DEBUG_RETURN(CStudioRender_DrawStaticPropShadows, rcx, info, modelToWorld, flags);

	if (Vars::Visuals::UltraToaster::PropShadows.Value && UltraToaster::IsActive())
		return;

	CALL_ORIGINAL(rcx, info, modelToWorld, flags);
}
#endif
