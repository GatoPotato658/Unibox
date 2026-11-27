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
	m_bCurrentBlockOccluded = false;
	m_iCurrentCullOverride = 0;
	m_pActiveRenderContext = nullptr;
	m_iActiveStencilRef = 0;
	m_iActivePhase = 0;
	m_bActiveSingleDraw = false;
	m_pPendingD3DOccluded = nullptr;
	m_pNeedsPhase2Fallback = nullptr;
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

void CChams::DrawModel(CBaseEntity* pEntity, const std::vector<ActiveLayer_t>& vLayers, IMatRenderContext* pRenderContext, int iModel, int iStencilReference)
{
	if (!pEntity || !pEntity->GetModel())
		return;

	if (!m_iFlags && iModel == ModelEnum::Visible)
		m_mEntities[pEntity->entindex()];

	const bool bTwoModel = iStencilReference != 0;
	if (iModel == ModelEnum::Occluded && vLayers.empty())
		return;

	Begin();
	m_pActiveRenderContext = pRenderContext;
	m_iActiveStencilRef = iStencilReference;

	switch (iModel)
	{
	case ModelEnum::Visible:
	{
		if (bTwoModel)
		{
			ConfigureSourceStencil(pRenderContext, false, iStencilReference);
			if (auto pMask = F::Materials.GetMaterial(FNV1A::Hash32Const("None")); pMask && pMask->m_pMaterial)
			{
				pRenderContext->OverrideColorWriteEnable(true, false);
				pRenderContext->OverrideAlphaWriteEnable(true, false);
				I::RenderView->SetBlend(1.f);
				I::ModelRender->ForcedMaterialOverride(pMask->m_pMaterial);
				m_tCurrentMaterial = {};
				m_bOccludedMaterial = false;
				m_bCurrentBlockOccluded = false;
				m_iCurrentCullOverride = 0;
				m_bRendering = true;
				pEntity->DrawModel(STUDIO_RENDER);
				m_bRendering = false;
				pRenderContext->OverrideColorWriteEnable(false, false);
				pRenderContext->OverrideAlphaWriteEnable(false, false);
			}
		}

		for (const auto& tLayer : vLayers)
		{
			const auto& tMaterial = tLayer.m_tMaterial;
			if (tMaterial.iEffect == 0)
			{
				F::D3DMaterials.FlushBatch(pRenderContext);
				if (bTwoModel)
					ConfigureSourceStencil(pRenderContext, false, iStencilReference, tLayer.m_bBlockOccluded);
			}

			m_tCurrentMaterial = tMaterial;
			m_bOccludedMaterial = false;
			m_bCurrentBlockOccluded = tLayer.m_bBlockOccluded;
			m_iCurrentCullOverride = tLayer.m_bInvertCull ? 1 : 0;
			F::Materials.SetColor(tLayer.m_pMaterial, tMaterial.tColor);
			I::ModelRender->ForcedMaterialOverride(tLayer.m_pMaterial ? tLayer.m_pMaterial->m_pMaterial : nullptr);
			if (tMaterial.iEffect == 0 && tLayer.m_bInvertCull)
				pRenderContext->CullMode(MATERIAL_CULLMODE_CW);

			m_bRendering = true;
			pEntity->DrawModel(STUDIO_RENDER);
			m_bRendering = false;

			if (tMaterial.iEffect == 0 && tLayer.m_bInvertCull)
				pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
		}

		if (bTwoModel)
			pRenderContext->SetStencilEnable(false);
		break;
	}
	case ModelEnum::Occluded:
	{
		bool bSourceDepthActive = false;
		auto fBeginSourceState = [&]()
			{
				if (bSourceDepthActive)
					return;
				F::D3DMaterials.FlushBatch(pRenderContext);
				if (bTwoModel)
					ConfigureSourceStencil(pRenderContext, true, iStencilReference);
				pRenderContext->OverrideDepthEnable(true, false);
				pRenderContext->DepthRange(0.f, 0.2f);
				bSourceDepthActive = true;
			};
		auto fEndSourceState = [&]()
			{
				if (!bSourceDepthActive)
					return;
				pRenderContext->DepthRange(0.f, 1.f);
				pRenderContext->OverrideDepthEnable(false, false);
				if (bTwoModel)
					pRenderContext->SetStencilEnable(false);
				bSourceDepthActive = false;
			};

		for (const auto& tLayer : vLayers)
		{
			const auto& tMaterial = tLayer.m_tMaterial;
			if (tMaterial.iEffect == 0)
				fBeginSourceState();
			else
				fEndSourceState();

			m_tCurrentMaterial = tMaterial;
			m_bOccludedMaterial = true;
			m_bCurrentBlockOccluded = false;
			m_iCurrentCullOverride = tLayer.m_bInvertCull ? 1 : 0;
			F::Materials.SetColor(tLayer.m_pMaterial, tMaterial.tColor);
			I::ModelRender->ForcedMaterialOverride(tLayer.m_pMaterial ? tLayer.m_pMaterial->m_pMaterial : nullptr);
			if (tMaterial.iEffect == 0 && tLayer.m_bInvertCull)
				pRenderContext->CullMode(MATERIAL_CULLMODE_CW);

			m_bRendering = true;
			pEntity->DrawModel(STUDIO_RENDER);
			m_bRendering = false;

			if (tMaterial.iEffect == 0 && tLayer.m_bInvertCull)
				pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
		}

		fEndSourceState();
		break;
	}
	}

	m_pActiveRenderContext = nullptr;
	m_iActiveStencilRef = 0;
	m_bCurrentBlockOccluded = false;
	m_iCurrentCullOverride = 0;
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
		ConfigureSourceStencil(m_pActiveRenderContext, bOccluded, iStencilRef, tLayer.m_bBlockOccluded);
	if (bOccluded)
	{
		m_pActiveRenderContext->OverrideDepthEnable(true, false);
		m_pActiveRenderContext->DepthRange(0.f, 0.2f);
	}

	F::Materials.SetColor(tLayer.m_pMaterial, tLayer.m_tMaterial.tColor);
	I::ModelRender->ForcedMaterialOverride(tLayer.m_pMaterial ? tLayer.m_pMaterial->m_pMaterial : nullptr);
	if (tLayer.m_bInvertCull)
		m_pActiveRenderContext->CullMode(MATERIAL_CULLMODE_CW);

	fOriginalDME(pModelRender, pState, pInfo, pBoneToWorld);

	if (tLayer.m_bInvertCull)
		m_pActiveRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
	if (bOccluded)
	{
		m_pActiveRenderContext->DepthRange(0.f, 1.f);
		m_pActiveRenderContext->OverrideDepthEnable(false, false);
	}
	if (bTwoModel)
		m_pActiveRenderContext->SetStencilEnable(false);
}

void CChams::RenderActiveModel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (m_iFlags && pInfo.entity_index != I::EngineClient->GetLocalPlayer())
	{
		RenderBacktrackChams(pModelRender, pState, pInfo, m_iFlags, m_vActiveVisible, m_vActiveOccluded, m_pActiveRenderContext, m_iActiveStencilRef, m_iActivePhase, m_pPendingD3DOccluded, m_pNeedsPhase2Fallback);
		return;
	}

	matrix3x4* pDrawBones = m_iFlags ? F::FakeAngle.aBones : pBoneToWorld;
	const bool bTwoModel = m_iActiveStencilRef != 0;

	CD3DMaterials::GeometryRef pGeometry;
	CD3DMaterials::PoseRef pPoses;
	bool bNeedsFallback = false;
	const bool bGeomOk = F::D3DMaterials.PrepareGeometry(pState, pInfo, pGeometry, bNeedsFallback)
		&& F::D3DMaterials.PreparePoses(pGeometry, pDrawBones, pPoses);
	const bool bCanUseD3D = bGeomOk && !bNeedsFallback;

	static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
	auto fOriginalDME = IVModelRender_DrawModelExecute ? IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>() : nullptr;

	if (m_iActivePhase == 1)
	{
		bool bHasSolidD3DVisible = false;
		for (const auto& tLayer : m_vActiveVisible)
		{
			if (tLayer.m_tMaterial.iEffect != 0 && tLayer.m_tMaterial.iEffect != 10)
			{
				bHasSolidD3DVisible = true;
				break;
			}
		}

		if (bTwoModel && !(bCanUseD3D && bHasSolidD3DVisible))
		{
			if (bCanUseD3D)
			{
				D3DStencilState_t stMask = MakeD3DStencil(true, false, m_iActiveStencilRef);
				ChamsMaterial_t tDummy = {};
				tDummy.iEffect = 1;
				F::D3DMaterials.DrawPrepared(m_pActiveRenderContext, pGeometry, pPoses, tDummy, false, 0, &stMask, true);
			}
			else if (fOriginalDME)
			{
				F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
				ConfigureSourceStencil(m_pActiveRenderContext, false, m_iActiveStencilRef);
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

		for (const auto& tLayer : m_vActiveVisible)
		{
			if (tLayer.m_tMaterial.iEffect != 0 && bGeomOk)
			{
				if (bNeedsFallback && fOriginalDME)
				{
					F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
					if (bTwoModel)
						ConfigureSourceStencil(m_pActiveRenderContext, false, m_iActiveStencilRef, tLayer.m_bBlockOccluded);
					I::ModelRender->ForcedMaterialOverride(nullptr);
					fOriginalDME(pModelRender, pState, pInfo, pDrawBones);
					if (bTwoModel)
						m_pActiveRenderContext->SetStencilEnable(false);
				}
				D3DStencilState_t stVisible = MakeD3DStencil(bTwoModel, false, m_iActiveStencilRef, tLayer.m_bBlockOccluded);
				F::D3DMaterials.DrawPrepared(m_pActiveRenderContext, pGeometry, pPoses, tLayer.m_tMaterial, false, tLayer.m_bInvertCull ? 1 : 0, bTwoModel ? &stVisible : nullptr, false);
			}
			else
				DrawSourceLayer(pModelRender, pState, pInfo, pDrawBones, tLayer, false, bTwoModel, m_iActiveStencilRef);
		}

		if (!m_vActiveOccluded.empty())
		{
			if (bCanUseD3D && m_pPendingD3DOccluded)
				m_pPendingD3DOccluded->push_back({ pGeometry, { { pPoses, 1.f } }, false });
			else if (m_pNeedsPhase2Fallback)
				*m_pNeedsPhase2Fallback = true;
		}
		return;
	}

	for (const auto& tLayer : m_vActiveOccluded)
	{
		if (tLayer.m_tMaterial.iEffect != 0 && bGeomOk)
		{
			if (bNeedsFallback && fOriginalDME)
			{
				F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
				if (bTwoModel)
					ConfigureSourceStencil(m_pActiveRenderContext, true, m_iActiveStencilRef);
				m_pActiveRenderContext->OverrideDepthEnable(true, false);
				m_pActiveRenderContext->DepthRange(0.f, 0.2f);
				I::ModelRender->ForcedMaterialOverride(nullptr);
				fOriginalDME(pModelRender, pState, pInfo, pDrawBones);
				m_pActiveRenderContext->DepthRange(0.f, 1.f);
				m_pActiveRenderContext->OverrideDepthEnable(false, false);
				if (bTwoModel)
					m_pActiveRenderContext->SetStencilEnable(false);
			}
			D3DStencilState_t stOccluded = MakeD3DStencil(bTwoModel, true, m_iActiveStencilRef);
			F::D3DMaterials.DrawPrepared(m_pActiveRenderContext, pGeometry, pPoses, tLayer.m_tMaterial, true, tLayer.m_bInvertCull ? 1 : 0, bTwoModel ? &stOccluded : nullptr, false);
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

	static thread_local std::vector<PreparedEntity_t> vPrepared;
	vPrepared.clear();
	vPrepared.reserve(m_vEntities.size());

	bool bUseStencil = false;
	const bool bD3DBackend = I::MaterialSystem->GetRenderBackend() == RENDER_BACKEND_D3D9;
	for (auto& tInfo : m_vEntities)
	{
		auto pEntity = tInfo.m_pEntity;
		if (!pEntity || !pEntity->GetModel())
			continue;

		if (!tInfo.m_iFlags)
			m_mEntities[pEntity->entindex()];

		m_flCurrentDistance = tInfo.m_flDistance;
		const auto& tChams = *tInfo.m_pChams;

		PreparedEntity_t tPrepared;
		tPrepared.m_tInfo = tInfo;
		fCollectLayers(tChams.GetVisible(), tPrepared.m_vVisible);
		if (!tChams.Occluded.empty())
			fCollectLayers(tChams.GetOccluded(), tPrepared.m_vOccluded);

		if (tPrepared.m_vVisible.empty() && tPrepared.m_vOccluded.empty())
			continue;

		bool bPureD3D = bD3DBackend;
		if (bPureD3D)
		{
			for (const auto& tLayer : tPrepared.m_vVisible)
			{
				if (tLayer.m_tMaterial.iEffect == 0)
				{
					bPureD3D = false;
					break;
				}
			}
			if (bPureD3D)
			{
				for (const auto& tLayer : tPrepared.m_vOccluded)
				{
					if (tLayer.m_tMaterial.iEffect == 0)
					{
						bPureD3D = false;
						break;
					}
				}
			}
		}

		tPrepared.m_bPureD3D = bPureD3D;
		bUseStencil |= !tPrepared.m_vOccluded.empty();
		vPrepared.push_back(std::move(tPrepared));
	}

	if (vPrepared.empty())
		return;

	std::ranges::stable_partition(vPrepared, [](const PreparedEntity_t& tEntry) { return !tEntry.m_tInfo.m_iFlags; });

	if (bUseStencil)
		pRenderContext->ClearBuffers(false, false, true);
	const int iStencilRef = bUseStencil ? 1 : 0;

	F::D3DMaterials.BeginBatch(pRenderContext);

	auto fInvokeDrawModel = [](const ChamsInfo_t& tInfo)
		{
			if (!tInfo.m_iFlags)
			{
				tInfo.m_pEntity->DrawModel(STUDIO_RENDER);
				return;
			}
			auto pPlayer = tInfo.m_pEntity->As<CTFPlayer>();
			const float flOldInvisibility = pPlayer->m_flInvisibility();
			pPlayer->m_flInvisibility() = 0.f;
			tInfo.m_pEntity->DrawModel(STUDIO_RENDER);
			pPlayer->m_flInvisibility() = flOldInvisibility;
		};

	for (auto& tPrepared : vPrepared)
	{
		if (!tPrepared.m_bPureD3D)
		{
			if (tPrepared.m_vVisible.empty() && !bUseStencil)
				continue;

			F::D3DMaterials.FlushBatch(pRenderContext);
			m_flCurrentDistance = tPrepared.m_tInfo.m_flDistance;
			m_iFlags = tPrepared.m_tInfo.m_iFlags;
			if (!m_iFlags)
				DrawModel(tPrepared.m_tInfo.m_pEntity, tPrepared.m_vVisible, pRenderContext, ModelEnum::Visible, iStencilRef);
			else
			{
				auto pPlayer = tPrepared.m_tInfo.m_pEntity->As<CTFPlayer>();
				const float flOldInvisibility = pPlayer->m_flInvisibility();
				pPlayer->m_flInvisibility() = 0.f;
				DrawModel(tPrepared.m_tInfo.m_pEntity, tPrepared.m_vVisible, pRenderContext, ModelEnum::Visible, iStencilRef);
				pPlayer->m_flInvisibility() = flOldInvisibility;
			}
			m_iFlags = 0;
			continue;
		}

		Begin();
		m_flCurrentDistance = tPrepared.m_tInfo.m_flDistance;
		m_iFlags = tPrepared.m_tInfo.m_iFlags;
		m_pActiveRenderContext = pRenderContext;
		m_iActiveStencilRef = iStencilRef;
		m_iActivePhase = 1;
		m_bActiveSingleDraw = true;
		m_vActiveVisible = tPrepared.m_vVisible;
		m_vActiveOccluded = tPrepared.m_vOccluded;
		m_pPendingD3DOccluded = &tPrepared.m_vPendingD3D;
		m_pNeedsPhase2Fallback = &tPrepared.m_bNeedsPhase2Fallback;
		m_bRendering = true;

		fInvokeDrawModel(tPrepared.m_tInfo);

		m_bRendering = false;
		m_bActiveSingleDraw = false;
		m_iActivePhase = 0;
		m_iActiveStencilRef = 0;
		m_pActiveRenderContext = nullptr;
		m_pPendingD3DOccluded = nullptr;
		m_pNeedsPhase2Fallback = nullptr;
		m_iFlags = 0;
		m_vActiveVisible.clear();
		m_vActiveOccluded.clear();
		End();
	}

	if (bUseStencil)
	{
		F::D3DMaterials.FlushBatch(pRenderContext);

		for (auto& tPrepared : vPrepared)
		{
			if (tPrepared.m_vOccluded.empty())
				continue;

			if (!tPrepared.m_bPureD3D)
			{
				F::D3DMaterials.FlushBatch(pRenderContext);
				m_flCurrentDistance = tPrepared.m_tInfo.m_flDistance;
				m_iFlags = tPrepared.m_tInfo.m_iFlags;
				if (!m_iFlags)
					DrawModel(tPrepared.m_tInfo.m_pEntity, tPrepared.m_vOccluded, pRenderContext, ModelEnum::Occluded, iStencilRef);
				else
				{
					auto pPlayer = tPrepared.m_tInfo.m_pEntity->As<CTFPlayer>();
					const float flOldInvisibility = pPlayer->m_flInvisibility();
					pPlayer->m_flInvisibility() = 0.f;
					DrawModel(tPrepared.m_tInfo.m_pEntity, tPrepared.m_vOccluded, pRenderContext, ModelEnum::Occluded, iStencilRef);
					pPlayer->m_flInvisibility() = flOldInvisibility;
				}
				m_iFlags = 0;
				continue;
			}

			if (!tPrepared.m_bNeedsPhase2Fallback && !tPrepared.m_vPendingD3D.empty())
			{
				const D3DStencilState_t stOccluded = MakeD3DStencil(true, true, iStencilRef);
				for (const auto& tPending : tPrepared.m_vPendingD3D)
				{
					for (const auto& tLayer : tPrepared.m_vOccluded)
					{
						const float flBaseAlpha = tLayer.m_tMaterial.tColor.a / 255.f;
						for (const auto& tPose : tPending.m_vPoses)
						{
							ChamsMaterial_t tMat = tLayer.m_tMaterial;
							if (tPending.m_bScaleAlpha)
								tMat.tColor.a = static_cast<byte>(std::clamp(tPose.m_flBlend * flBaseAlpha, 0.f, 1.f) * 255.f);
							F::D3DMaterials.DrawPrepared(pRenderContext, tPending.m_pGeometry, tPose.m_pPose, tMat, true, tLayer.m_bInvertCull ? 1 : 0, &stOccluded, false);
						}
					}
				}
				continue;
			}

			Begin();
			m_flCurrentDistance = tPrepared.m_tInfo.m_flDistance;
			m_iFlags = tPrepared.m_tInfo.m_iFlags;
			m_pActiveRenderContext = pRenderContext;
			m_iActiveStencilRef = iStencilRef;
			m_iActivePhase = 2;
			m_bActiveSingleDraw = true;
			m_vActiveVisible.clear();
			m_vActiveOccluded = tPrepared.m_vOccluded;
			m_pPendingD3DOccluded = nullptr;
			m_pNeedsPhase2Fallback = nullptr;
			m_bRendering = true;

			fInvokeDrawModel(tPrepared.m_tInfo);

			m_bRendering = false;
			m_bActiveSingleDraw = false;
			m_iActivePhase = 0;
			m_iActiveStencilRef = 0;
			m_pActiveRenderContext = nullptr;
			m_iFlags = 0;
			m_vActiveOccluded.clear();
			End();
		}
	}

	F::D3DMaterials.EndBatch(pRenderContext);
	if (bUseStencil)
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
	if (m_pActiveRenderContext)
	{
		const int iCull = m_bViewmodelMaterial ? m_iViewmodelCullOverride : m_iCurrentCullOverride;
		CD3DMaterials::GeometryRef pGeometry;
		CD3DMaterials::PoseRef pPoses;
		bool bNeedsFallback = false;
		if (!F::D3DMaterials.PrepareGeometry(pState, pInfo, pGeometry, bNeedsFallback)
			|| !F::D3DMaterials.PreparePoses(pGeometry, pBoneToWorld, pPoses))
			return false;

		const bool bUseStencil = !m_bViewmodelMaterial && m_iActiveStencilRef != 0;
		const D3DStencilState_t stState = MakeD3DStencil(bUseStencil, m_bOccludedMaterial, m_iActiveStencilRef, m_bCurrentBlockOccluded);

		if (bNeedsFallback)
		{
			static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
			auto fOriginal = IVModelRender_DrawModelExecute ? IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>() : nullptr;
			if (!fOriginal || !I::ModelRender)
				return false;
			F::D3DMaterials.FlushBatch(m_pActiveRenderContext);
			I::ModelRender->ForcedMaterialOverride(nullptr);
			if (bUseStencil)
				ConfigureSourceStencil(m_pActiveRenderContext, m_bOccludedMaterial, m_iActiveStencilRef, m_bCurrentBlockOccluded);
			if (m_bOccludedMaterial && !m_bViewmodelMaterial)
			{
				m_pActiveRenderContext->OverrideDepthEnable(true, false);
				m_pActiveRenderContext->DepthRange(0.f, 0.2f);
			}
			fOriginal(I::ModelRender, pState, pInfo, pBoneToWorld);
			if (m_bOccludedMaterial && !m_bViewmodelMaterial)
			{
				m_pActiveRenderContext->DepthRange(0.f, 1.f);
				m_pActiveRenderContext->OverrideDepthEnable(false, false);
			}
			if (bUseStencil && m_bOccludedMaterial)
				m_pActiveRenderContext->SetStencilEnable(false);
		}
		return F::D3DMaterials.DrawPrepared(m_pActiveRenderContext, pGeometry, pPoses, tMaterial, m_bOccludedMaterial, iCull, bUseStencil ? &stState : nullptr, false);
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
