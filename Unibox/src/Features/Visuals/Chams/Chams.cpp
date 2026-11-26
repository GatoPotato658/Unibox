#include "Chams.h"
#include "D3DMaterials.h"

#include "../Visuals.h"
#include "../Groups/Groups.h"
#include "../Materials/Materials.h"
#include "../FakeAngle/FakeAngle.h"
#include "../../Backtrack/Backtrack.h"
#include <memory>

static inline bool GetDistanceThing(float flDistance, const ChamsMaterial_t& tMaterial, Color_t& tColorOut)
{
	if (flDistance < tMaterial.flStart || flDistance > tMaterial.flEnd)
		return false;

	if (tMaterial.bSmoothAlpha)
	{
		float flAlpha = std::clamp((tMaterial.flEnd - flDistance) / 256.f, 0.f, 1.f);
		if (tMaterial.flStart > 0.f)
			flAlpha *= std::clamp((flDistance - tMaterial.flStart) / 256.f, 0.f, 1.f);
		tColorOut.a = static_cast<byte>(tColorOut.a * flAlpha);
	}
	return tColorOut.a;
}

void CChams::Reset()
{
	m_vEntities.clear();
	m_vActiveVisible.clear();
	m_vActiveOccluded.clear();
	m_mEntities.clear();
	m_tOriginalColor = {};
	m_flOriginalBlend = 1.f;
	m_pOriginalMaterial = nullptr;
	m_iOriginalOverride = OVERRIDE_NORMAL;
	m_flCurrentDistance = -1.f;
	m_iFlags = 0;
	m_bRendering = false;
	m_tCurrentMaterial = {};
	m_bOccludedMaterial = false;
	m_bViewmodelMaterial = false;
	m_iViewmodelCullOverride = -1;
	m_pActiveRenderContext = nullptr;
	m_iActiveStencilRef = 0;
	m_bActiveSame = false;
	m_bActiveSingleDraw = false;
}

void CChams::Begin()
{
	m_tOriginalColor = I::RenderView->GetColorModulation();
	m_flOriginalBlend = I::RenderView->GetBlend();
	I::ModelRender->GetMaterialOverride(&m_pOriginalMaterial, &m_iOriginalOverride);
}
void CChams::End()
{
	I::RenderView->SetColorModulation(m_tOriginalColor);
	I::RenderView->SetBlend(m_flOriginalBlend);
	I::ModelRender->ForcedMaterialOverride(m_pOriginalMaterial, m_iOriginalOverride);
}

void CChams::DrawModel(CBaseEntity* pEntity, const Chams_t& tChams, IMatRenderContext* pRenderContext, int iModel, bool bSame, int iStencilReference)
{
	if (!pEntity || !pEntity->GetModel())
		return;

	if (!m_iFlags && iModel == ModelEnum::Visible)
		m_mEntities[pEntity->entindex()];

	const bool bTwoModel = iStencilReference != 0;
	if (iModel == ModelEnum::Visible && bSame)
		return;
	if (iModel == ModelEnum::Occluded && tChams.Occluded.empty())
		return;

	Begin();
	switch (iModel)
	{
	case ModelEnum::Visible:
	{
		if (bTwoModel)
		{
			pRenderContext->SetStencilEnable(true);
			pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
			pRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
			pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilReferenceValue(iStencilReference);
			pRenderContext->SetStencilWriteMask(0xFF);
			pRenderContext->SetStencilTestMask(0x0);

			if (auto pMask = F::Materials.GetMaterial(FNV1A::Hash32Const("None")); pMask && pMask->m_pMaterial)
			{
				pRenderContext->OverrideColorWriteEnable(true, false);
				pRenderContext->OverrideAlphaWriteEnable(true, false);
				I::RenderView->SetBlend(1.f);
				I::ModelRender->ForcedMaterialOverride(pMask->m_pMaterial);
				m_tCurrentMaterial = {};
				m_bOccludedMaterial = false;
				m_bRendering = true;
				pEntity->DrawModel(STUDIO_RENDER);
				m_bRendering = false;
				pRenderContext->OverrideColorWriteEnable(false, false);
				pRenderContext->OverrideAlphaWriteEnable(false, false);
			}
		}

		auto& vMaterials = tChams.GetVisible();
		for (auto& [sName, tMaterial] : vMaterials)
		{
			Color_t tColor = tMaterial.tColor;
			if (m_flCurrentDistance > -1.f && !GetDistanceThing(m_flCurrentDistance, tMaterial, tColor))
				continue;

			auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

			m_tCurrentMaterial = tMaterial;
			m_tCurrentMaterial.tColor = tColor;
			m_bOccludedMaterial = false;
			F::Materials.SetColor(pMaterial, tColor);
			I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);
			if (pMaterial)
			{
				if (pMaterial->m_bInvertCull)
					pRenderContext->CullMode(MATERIAL_CULLMODE_CW);
				if (pMaterial->m_bBlockOccluded)
					pRenderContext->SetStencilZFailOperation(STENCILOPERATION_REPLACE);
			}

			m_bRendering = true;
			pEntity->DrawModel(STUDIO_RENDER);
			m_bRendering = false;

			if (pMaterial)
			{
				if (pMaterial->m_bInvertCull)
					pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
				if (pMaterial->m_bBlockOccluded)
					pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
			}
		}

		if (bTwoModel)
			pRenderContext->SetStencilEnable(false);
		break;
	}
	case ModelEnum::Occluded:
	{
		if (bTwoModel)
		{
			pRenderContext->SetStencilEnable(true);
			pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_NOTEQUAL);
			pRenderContext->SetStencilPassOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilReferenceValue(iStencilReference);
			pRenderContext->SetStencilWriteMask(0x0);
			pRenderContext->SetStencilTestMask(0xFF);
		}
		pRenderContext->DepthRange(0.f, 0.2f);

		auto& vMaterials = tChams.GetOccluded();
		for (auto& [sName, tMaterial] : vMaterials)
		{
			Color_t tColor = tMaterial.tColor;
			if (m_flCurrentDistance > -1.f && !GetDistanceThing(m_flCurrentDistance, tMaterial, tColor))
				continue;

			auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

			m_tCurrentMaterial = tMaterial;
			m_tCurrentMaterial.tColor = tColor;
			m_bOccludedMaterial = true;
			F::Materials.SetColor(pMaterial, tColor);
			I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);
			if (pMaterial && pMaterial->m_bInvertCull)
				pRenderContext->CullMode(MATERIAL_CULLMODE_CW);

			m_bRendering = true;
			pEntity->DrawModel(STUDIO_RENDER);
			m_bRendering = false;

			if (pMaterial && pMaterial->m_bInvertCull)
				pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
		}

		if (bTwoModel)
			pRenderContext->SetStencilEnable(false);
		pRenderContext->DepthRange(0.f, 1.f);
	}
	}
	End();
}

void CChams::DrawSourceLayer(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld, const ActiveLayer_t& tLayer, bool bOccluded, bool bTwoModel, int iStencilRef)
{
	static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
	auto fOriginalDME = IVModelRender_DrawModelExecute ? IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>() : nullptr;
	if (!fOriginalDME)
		return;

	F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
	if (bTwoModel)
	{
		m_pActiveRenderContext->SetStencilEnable(true);
		m_pActiveRenderContext->SetStencilCompareFunction(bOccluded ? STENCILCOMPARISONFUNCTION_NOTEQUAL : STENCILCOMPARISONFUNCTION_ALWAYS);
		m_pActiveRenderContext->SetStencilPassOperation(bOccluded ? STENCILOPERATION_KEEP : STENCILOPERATION_REPLACE);
		m_pActiveRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
		m_pActiveRenderContext->SetStencilZFailOperation((!bOccluded && tLayer.m_bBlockOccluded) ? STENCILOPERATION_REPLACE : STENCILOPERATION_KEEP);
		m_pActiveRenderContext->SetStencilReferenceValue(iStencilRef);
		m_pActiveRenderContext->SetStencilWriteMask(bOccluded ? 0x0 : 0xFF);
		m_pActiveRenderContext->SetStencilTestMask(bOccluded ? 0xFF : 0x0);
	}
	if (bOccluded)
		m_pActiveRenderContext->DepthRange(0.f, 0.2f);

	F::Materials.SetColor(tLayer.m_pMaterial, tLayer.m_tMaterial.tColor);
	I::ModelRender->ForcedMaterialOverride(tLayer.m_pMaterial ? tLayer.m_pMaterial->m_pMaterial : nullptr);
	if (tLayer.m_bInvertCull)
		m_pActiveRenderContext->CullMode(MATERIAL_CULLMODE_CW);

	fOriginalDME(pModelRender, pState, pInfo, pBoneToWorld);

	if (tLayer.m_bInvertCull)
		m_pActiveRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
	if (bOccluded)
		m_pActiveRenderContext->DepthRange(0.f, 1.f);
	if (bTwoModel)
		m_pActiveRenderContext->SetStencilEnable(false);
}

void CChams::RenderActiveModel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (m_iFlags && pInfo.entity_index != I::EngineClient->GetLocalPlayer())
	{
		RenderBacktrackChams(pModelRender, pState, pInfo, m_iFlags, m_vActiveVisible, m_vActiveOccluded, m_pActiveRenderContext, m_iActiveStencilRef, m_bActiveSame);
		return;
	}

	matrix3x4* pDrawBones = m_iFlags ? F::FakeAngle.aBones : pBoneToWorld;
	const bool bTwoModel = m_iActiveStencilRef != 0;

	bool bAnyD3D = false;
	bool bHasSolidD3DVisible = false;
	if (!m_bActiveSame)
	{
		for (const auto& tLayer : m_vActiveVisible)
		{
			if (tLayer.m_tMaterial.iEffect != 0)
			{
				bAnyD3D = true;
				if (tLayer.m_tMaterial.iEffect != 10)
					bHasSolidD3DVisible = true;
			}
		}
	}
	for (const auto& tLayer : m_vActiveOccluded)
	{
		if (tLayer.m_tMaterial.iEffect != 0)
			bAnyD3D = true;
	}

	CD3DMaterials::GeometryRef pGeometry;
	CD3DMaterials::PoseRef pPoses;
	bool bNeedsFallback = false;
	const bool bGeomOk = bAnyD3D
		&& F::D3DMaterials.PrepareGeometry(pState, pInfo, pGeometry, bNeedsFallback)
		&& F::D3DMaterials.PreparePoses(pGeometry, pDrawBones, pPoses);
	const bool bCanUseD3D = bGeomOk && !bNeedsFallback;

	static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
	auto fOriginalDME = IVModelRender_DrawModelExecute ? IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>() : nullptr;

	if (bTwoModel && !m_bActiveSame && !(bCanUseD3D && bHasSolidD3DVisible))
	{
		if (bCanUseD3D)
		{
			D3DStencilState_t stMask{ true, 8, 3, 1, 1, std::uint32_t(m_iActiveStencilRef), 0xFF, 0x0 };
			ChamsMaterial_t tDummy = {};
			tDummy.iEffect = 1;
			F::D3DMaterials.DrawPrepared(m_pActiveRenderContext, pGeometry, pPoses, tDummy, false, 0, &stMask, true);
		}
		else if (fOriginalDME)
		{
			F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
			m_pActiveRenderContext->SetStencilEnable(true);
			m_pActiveRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
			m_pActiveRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
			m_pActiveRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
			m_pActiveRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
			m_pActiveRenderContext->SetStencilReferenceValue(m_iActiveStencilRef);
			m_pActiveRenderContext->SetStencilWriteMask(0xFF);
			m_pActiveRenderContext->SetStencilTestMask(0x0);
			if (auto pMask = F::Materials.GetMaterial(FNV1A::Hash32Const("None")); pMask && pMask->m_pMaterial)
			{
				m_pActiveRenderContext->OverrideColorWriteEnable(true, false);
				m_pActiveRenderContext->OverrideAlphaWriteEnable(true, false);
				I::RenderView->SetBlend(1.f);
				I::ModelRender->ForcedMaterialOverride(pMask->m_pMaterial);
				fOriginalDME(pModelRender, pState, pInfo, pDrawBones);
				m_pActiveRenderContext->OverrideColorWriteEnable(false, false);
				m_pActiveRenderContext->OverrideAlphaWriteEnable(false, false);
			}
			m_pActiveRenderContext->SetStencilEnable(false);
		}
	}

	if (!m_bActiveSame)
	{
		for (const auto& tLayer : m_vActiveVisible)
		{
			if (tLayer.m_tMaterial.iEffect != 0 && bGeomOk)
			{
				if (bNeedsFallback && fOriginalDME)
				{
					F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
					I::ModelRender->ForcedMaterialOverride(nullptr);
					fOriginalDME(pModelRender, pState, pInfo, pDrawBones);
				}
				D3DStencilState_t stVisible{ bTwoModel, 8, 3, 1, tLayer.m_bBlockOccluded ? 3u : 1u, std::uint32_t(m_iActiveStencilRef), 0xFF, 0x0 };
				F::D3DMaterials.DrawPrepared(m_pActiveRenderContext, pGeometry, pPoses, tLayer.m_tMaterial, false, tLayer.m_bInvertCull ? 1 : 0, &stVisible, false);
			}
			else
				DrawSourceLayer(pModelRender, pState, pInfo, pDrawBones, tLayer, false, bTwoModel, m_iActiveStencilRef);
		}
	}

	for (const auto& tLayer : m_vActiveOccluded)
	{
		if (tLayer.m_tMaterial.iEffect != 0 && bGeomOk)
		{
			if (bNeedsFallback && fOriginalDME)
			{
				F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
				m_pActiveRenderContext->DepthRange(0.f, 0.2f);
				I::ModelRender->ForcedMaterialOverride(nullptr);
				fOriginalDME(pModelRender, pState, pInfo, pDrawBones);
				m_pActiveRenderContext->DepthRange(0.f, 1.f);
			}
			D3DStencilState_t stOccluded{ bTwoModel, 6, 1, 1, 1, std::uint32_t(m_iActiveStencilRef), 0x0, 0xFF };
			F::D3DMaterials.DrawPrepared(m_pActiveRenderContext, pGeometry, pPoses, tLayer.m_tMaterial, true, tLayer.m_bInvertCull ? 1 : 0, &stOccluded, false);
		}
		else
			DrawSourceLayer(pModelRender, pState, pInfo, pDrawBones, tLayer, true, bTwoModel, m_iActiveStencilRef);
	}
}

void CChams::Store(CTFPlayer* pLocal)
{
	m_vEntities.clear();
	if (!pLocal || !F::Groups.GroupsActive())
		return;

	struct OwnerScreenCache_t
	{
		CBaseEntity* m_pOwner = nullptr;
		float m_flDistance = 0.f;
		bool m_bOnScreen = false;
	};
	static thread_local std::vector<OwnerScreenCache_t> vOwnerCache;
	vOwnerCache.clear();

	auto fGetOwnerScreen = [&](CBaseEntity* pOwner, float flOwnerDist) -> bool
		{
			for (const auto& tEntry : vOwnerCache)
			{
				if (tEntry.m_pOwner == pOwner)
					return tEntry.m_bOnScreen;
			}
			const bool bOnScreen = SDK::IsOnScreen(pOwner, false);
			vOwnerCache.push_back({ pOwner, flOwnerDist, bOnScreen });
			return bOnScreen;
		};

	Vector vLocalOrigin = pLocal->m_vecOrigin();
	for (auto& [pEntity, pGroup] : F::Groups.GetGroup())
	{
		if (pEntity->IsDormant() || !pEntity->ShouldDraw())
			continue;

		CBaseEntity* pOwner = nullptr;
		Vector vEntOrigin = pEntity->m_vecOrigin();
		bool bWearableOrViewmodel = pEntity->IsBaseCombatWeapon() || pEntity->IsWearable();
		if (bWearableOrViewmodel)
		{
			pOwner = pEntity->m_hOwnerEntity().Get();
			if (pOwner)
				vEntOrigin = pOwner->m_vecOrigin();
		}

		float flDistance = vLocalOrigin.DistTo(vEntOrigin);
		if (pGroup->m_tChams() && !pEntity->IsWearableVM())
		{
			const bool bOnScreen = pOwner ? fGetOwnerScreen(pOwner, flDistance) : fGetOwnerScreen(pEntity, flDistance);
			if (bOnScreen)
				m_vEntities.emplace_back(pEntity, &pGroup->m_tChams, flDistance);
		}

		if (pEntity->IsPlayer() && pEntity != pLocal && pGroup->m_iBacktrack & BacktrackEnum::Enabled && pGroup->m_tBacktrackChams(false)
			&& (F::Backtrack.GetFakeLatency() || F::Backtrack.GetFakeInterp() > G::Lerp || F::Backtrack.GetWindow()))
		{
			auto pWeapon = H::Entities.GetWeapon();
			if (pWeapon && (pGroup->m_iBacktrack & BacktrackEnum::Always || G::PrimaryWeaponType != EWeaponType::PROJECTILE))
			{
				bool bShowFriendly = false, bShowEnemy = true;
				if (G::PrimaryWeaponType == EWeaponType::MELEE && SDK::AttribHookValue(0, "speed_buff_ally", pWeapon) > 0)
					bShowFriendly = true;
				else if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
					bShowFriendly = true, bShowEnemy = false;

				if (bShowEnemy && pEntity->m_iTeamNum() != pLocal->m_iTeamNum() || bShowFriendly && pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
					m_vEntities.emplace_back(pEntity, &pGroup->m_tBacktrackChams, flDistance, pGroup->m_iBacktrack);
			}
		}
	}

	Group_t* pGroup = nullptr;
	if (F::FakeAngle.bDrawChams && F::FakeAngle.bBonesSetup
		&& F::Groups.GetGroup(TargetsEnum::FakeAngle, pGroup) && pGroup->m_tChams(false))
	{
		m_vEntities.emplace_back(pLocal, &pGroup->m_tChams, -1.f, 1);
	}
}

void CChams::RenderMain()
{
	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext)
		return;
	const auto tContext = std::unique_ptr<IMatRenderContext, void(*)(IMatRenderContext*)>(pRenderContext, [](auto p) { p->Release(); });

	m_mEntities.clear();
	if (m_vEntities.empty())
		return;

	F::D3DMaterials.BeginBatch(pRenderContext);

	auto fCollectLayers = [&](const std::vector<std::pair<std::string, ChamsMaterial_t>>& vSource, std::vector<ActiveLayer_t>& vOut)
		{
			vOut.clear();
			vOut.reserve(vSource.size());
			for (const auto& [sName, tMaterial] : vSource)
			{
				Color_t tColor = tMaterial.tColor;
				if (m_flCurrentDistance > -1.f && !GetDistanceThing(m_flCurrentDistance, tMaterial, tColor))
					continue;

				auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));
				ActiveLayer_t tLayer;
				tLayer.m_tMaterial = tMaterial;
				tLayer.m_tMaterial.tColor = tColor;
				tLayer.m_pMaterial = pMaterial;
				tLayer.m_bInvertCull = pMaterial ? pMaterial->m_bInvertCull : false;
				tLayer.m_bBlockOccluded = pMaterial ? pMaterial->m_bBlockOccluded : false;
				vOut.push_back(tLayer);
			}
		};

	int iLastStencilReference = 0;
	const int iStencilMax = (1 << std::clamp(I::MaterialSystem->StencilBufferBits(), 1, 8)) - 1;
	for (auto& tInfo : m_vEntities)
	{
		auto pEntity = tInfo.m_pEntity;
		if (!pEntity || !pEntity->GetModel())
			continue;

		m_flCurrentDistance = tInfo.m_flDistance;
		const auto& tChams = *tInfo.m_pChams;
		const bool bSame = tChams.Visible == tChams.Occluded
			&& std::ranges::none_of(tChams.Visible, [](const auto& tLayer) { return tLayer.second.iEffect != 0; });
		int iStencilReference = 0;
		if (!tChams.Occluded.empty() && !bSame)
		{
			if (!iLastStencilReference || iLastStencilReference == iStencilMax)
			{
				F::D3DMaterials.FlushBatch(pRenderContext);
				pRenderContext->ClearBuffers(false, false, true);
				iLastStencilReference = 0;
			}
			iStencilReference = ++iLastStencilReference;
		}

		if (!tInfo.m_iFlags)
			m_mEntities[pEntity->entindex()];

		if (bSame && tChams.Occluded.empty())
			continue;

		m_vActiveVisible.clear();
		m_vActiveOccluded.clear();
		if (!bSame)
			fCollectLayers(tChams.GetVisible(), m_vActiveVisible);
		if (!tChams.Occluded.empty())
			fCollectLayers(tChams.GetOccluded(), m_vActiveOccluded);

		if (m_vActiveVisible.empty() && m_vActiveOccluded.empty())
			continue;

		bool bPureD3D = I::MaterialSystem->GetRenderBackend() == RENDER_BACKEND_D3D9;
		if (bPureD3D)
		{
			for (const auto& tLayer : m_vActiveVisible)
			{
				if (tLayer.m_tMaterial.iEffect == 0)
				{
					bPureD3D = false;
					break;
				}
			}
			if (bPureD3D)
			{
				for (const auto& tLayer : m_vActiveOccluded)
				{
					if (tLayer.m_tMaterial.iEffect == 0)
					{
						bPureD3D = false;
						break;
					}
				}
			}
		}

		if (!bPureD3D)
		{
			F::D3DMaterials.FlushBatch(pRenderContext);
			m_vActiveVisible.clear();
			m_vActiveOccluded.clear();
			m_iFlags = tInfo.m_iFlags;
			if (!tInfo.m_iFlags)
			{
				DrawModel(pEntity, tChams, pRenderContext, ModelEnum::Visible, bSame, iStencilReference);
				DrawModel(pEntity, tChams, pRenderContext, ModelEnum::Occluded, bSame, iStencilReference);
			}
			else
			{
				auto pPlayer = pEntity->As<CTFPlayer>();
				const float flOldInvisibility = pPlayer->m_flInvisibility();
				pPlayer->m_flInvisibility() = 0.f;
				DrawModel(pEntity, tChams, pRenderContext, ModelEnum::Visible, bSame, iStencilReference);
				DrawModel(pEntity, tChams, pRenderContext, ModelEnum::Occluded, bSame, iStencilReference);
				pPlayer->m_flInvisibility() = flOldInvisibility;
			}
			m_iFlags = false;
			continue;
		}

		Begin();

		m_iFlags = tInfo.m_iFlags;
		m_pActiveRenderContext = pRenderContext;
		m_iActiveStencilRef = iStencilReference;
		m_bActiveSame = bSame;
		m_bActiveSingleDraw = true;
		m_bRendering = true;

		if (!tInfo.m_iFlags)
			pEntity->DrawModel(STUDIO_RENDER);
		else
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			const float flOldInvisibility = pPlayer->m_flInvisibility();
			pPlayer->m_flInvisibility() = 0.f;
			pEntity->DrawModel(STUDIO_RENDER);
			pPlayer->m_flInvisibility() = flOldInvisibility;
		}

		m_bRendering = false;
		m_bActiveSingleDraw = false;
		m_pActiveRenderContext = nullptr;
		m_iFlags = false;
		m_vActiveVisible.clear();
		m_vActiveOccluded.clear();
		End();
	}

	F::D3DMaterials.EndBatch(pRenderContext);
	pRenderContext->ClearBuffers(false, false, true);
}

void CChams::RenderFakeAngle(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo)
{
	if (RenderMaterial(pState, pInfo, F::FakeAngle.aBones))
		return;

	static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
	IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, F::FakeAngle.aBones);
}

void CChams::RenderHandler(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (m_bActiveSingleDraw)
	{
		RenderActiveModel(pModelRender, pState, pInfo, pBoneToWorld);
		return;
	}

	if (!m_iFlags)
	{
		if (RenderMaterial(pState, pInfo, pBoneToWorld))
			return;

		static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
		IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
	}
	else
	{
		if (pInfo.entity_index != I::EngineClient->GetLocalPlayer())
			RenderBacktrackModel(pModelRender, pState, pInfo, m_iFlags);
		else
			RenderFakeAngle(pModelRender, pState, pInfo);
	}
}

bool CChams::RenderMaterial(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (!m_bRendering || !m_tCurrentMaterial.iEffect)
		return false;

	auto tMaterial = m_tCurrentMaterial;
	tMaterial.tColor.a = static_cast<byte>(std::clamp(I::RenderView->GetBlend(), 0.f, 1.f) * 255.f);
	if (m_bViewmodelMaterial && m_pActiveRenderContext && m_iViewmodelCullOverride >= 0)
	{
		CD3DMaterials::GeometryRef pGeometry;
		CD3DMaterials::PoseRef pPoses;
		bool bNeedsFallback = false;
		if (!F::D3DMaterials.PrepareGeometry(pState, pInfo, pGeometry, bNeedsFallback)
			|| !F::D3DMaterials.PreparePoses(pGeometry, pBoneToWorld, pPoses))
			return false;
		if (bNeedsFallback)
		{
			static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
			auto fOriginal = IVModelRender_DrawModelExecute ? IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>() : nullptr;
			if (!fOriginal || !I::ModelRender)
				return false;
			F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
			fOriginal(I::ModelRender, pState, pInfo, pBoneToWorld);
		}
		return F::D3DMaterials.DrawPrepared(m_pActiveRenderContext, pGeometry, pPoses, tMaterial, m_bOccludedMaterial, m_iViewmodelCullOverride, nullptr, false);
	}
	return F::D3DMaterials.Draw(pState, pInfo, pBoneToWorld, tMaterial, m_bOccludedMaterial, m_bViewmodelMaterial);
}

template <typename FDraw>
bool CChams::RenderViewmodelMaterial(IMatRenderContext* pRenderContext, Group_t* pGroup, MaterialCullMode_t restore, FDraw fDraw)
{
	Begin();
	F::D3DMaterials.BeginBatch(pRenderContext);
	m_pActiveRenderContext = pRenderContext;
	m_bViewmodelMaterial = true;
	m_bOccludedMaterial = false;
	for (auto& [sName, tMaterial] : pGroup->m_tChams.Visible)
	{
		auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

		m_tCurrentMaterial = tMaterial;
		bool bFlip = pMaterial && pMaterial->m_bInvertCull ? !G::FlipViewmodels : G::FlipViewmodels;
		m_iViewmodelCullOverride = bFlip ? 1 : 0;
		if (tMaterial.iEffect == 0)
			F::D3DMaterials.FlushBatch(pRenderContext);
		F::Materials.SetColor(pMaterial, tMaterial.tColor);
		I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);
		pRenderContext->CullMode(bFlip ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);

		m_bRendering = true;
		fDraw();
		m_bRendering = false;
	}
	F::D3DMaterials.EndBatch(pRenderContext);
	m_iViewmodelCullOverride = -1;
	m_pActiveRenderContext = nullptr;
	m_bViewmodelMaterial = false;
	pRenderContext->CullMode(restore);
	End();
	return true;
}

bool CChams::RenderViewmodel(CBaseAnimating* rcx, int flags, int* iReturn)
{
	if (!F::Groups.GroupsActive())
		return false;

	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext)
		return false;
	const auto tContext = std::unique_ptr<IMatRenderContext, void(*)(IMatRenderContext*)>(pRenderContext, [](auto p) { p->Release(); });

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(rcx->IsValid() ? TargetsEnum::ViewmodelHands : TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tChams(true))
		return false;

	static auto CBaseAnimating_InternalDrawModel = U::Hooks.Get("CBaseAnimating_InternalDrawModel");
	return RenderViewmodelMaterial(pRenderContext, pGroup, G::FlipViewmodels ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW, [&]()
	{
		*iReturn = CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
	});
}

bool CChams::RenderViewmodel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (!F::Groups.GroupsActive())
		return false;

	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext)
		return false;
	const auto tContext = std::unique_ptr<IMatRenderContext, void(*)(IMatRenderContext*)>(pRenderContext, [](auto p) { p->Release(); });

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tChams(true))
		return false;

	static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
	return RenderViewmodelMaterial(pRenderContext, pGroup, MATERIAL_CULLMODE_CCW, [&]()
	{
		if (!RenderMaterial(pState, pInfo, pBoneToWorld))
			IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
	});
}
