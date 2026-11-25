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



void CChams::Store(CTFPlayer* pLocal)
{
	m_vEntities.clear();
	if (!pLocal || !F::Groups.GroupsActive())
		return;

	Vector vLocalOrigin = pLocal->m_vecOrigin();
	for (auto& [pEntity, pGroup] : F::Groups.GetGroup())
	{
		if (pEntity->IsDormant() || !pEntity->ShouldDraw())
			continue;

		Vector vEntOrigin = pEntity->m_vecOrigin();
		bool bWearableOrViewmodel = pEntity->IsBaseCombatWeapon() || pEntity->IsWearable();
		if (bWearableOrViewmodel && pEntity->m_hOwnerEntity().Get())
			vEntOrigin = pEntity->m_hOwnerEntity().Get()->m_vecOrigin();
		
		float flDistance = vLocalOrigin.DistTo(vEntOrigin);
		if (pGroup->m_tChams() && !pEntity->IsWearableVM()
			&& SDK::IsOnScreen(pEntity, bWearableOrViewmodel))
			m_vEntities.emplace_back(pEntity, &pGroup->m_tChams, flDistance);

		if (pEntity->IsPlayer() && pEntity != pLocal && pGroup->m_iBacktrack & BacktrackEnum::Enabled && pGroup->m_tBacktrackChams(false)
			&& (F::Backtrack.GetFakeLatency() || F::Backtrack.GetFakeInterp() > G::Lerp || F::Backtrack.GetWindow()))
		{	// backtrack
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
	{	// fakeangle
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

	int iLastStencilReference = 0;
	const int iStencilMax = (1 << std::clamp(I::MaterialSystem->StencilBufferBits(), 1, 8)) - 1;
	for (auto& tInfo : m_vEntities)
	{
		m_flCurrentDistance = tInfo.m_flDistance;
		const auto& tChams = *tInfo.m_pChams;
		const bool bSame = tChams.Visible == tChams.Occluded
			&& std::ranges::none_of(tChams.Visible, [](const auto& tLayer) { return tLayer.second.iEffect != 0; });
		int iStencilReference = 0;
		if (!tChams.Occluded.empty() && !bSame)
		{
			if (!iLastStencilReference || iLastStencilReference == iStencilMax)
			{
				pRenderContext->ClearBuffers(false, false, true);
				iLastStencilReference = 0;
			}
			iStencilReference = ++iLastStencilReference;
		}
		const auto draw = [&]
		{
			DrawModel(tInfo.m_pEntity, tChams, pRenderContext, ModelEnum::Visible, bSame, iStencilReference);
			DrawModel(tInfo.m_pEntity, tChams, pRenderContext, ModelEnum::Occluded, bSame, iStencilReference);
		};
		if (!tInfo.m_iFlags)
			draw();
		else
		{
			m_iFlags = tInfo.m_iFlags;

			auto pPlayer = tInfo.m_pEntity->As<CTFPlayer>();
			const float flOldInvisibility = pPlayer->m_flInvisibility();
			pPlayer->m_flInvisibility() = 0.f;
			draw();
			pPlayer->m_flInvisibility() = flOldInvisibility;

			m_iFlags = false;
		}
	}

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
	return F::D3DMaterials.Draw(pState, pInfo, pBoneToWorld, tMaterial, m_bOccludedMaterial, m_bViewmodelMaterial);
}

template <typename FDraw>
bool CChams::RenderViewmodelMaterial(IMatRenderContext* pRenderContext, Group_t* pGroup, MaterialCullMode_t restore, FDraw fDraw)
{
	Begin();
	m_bViewmodelMaterial = true;
	m_bOccludedMaterial = false;
	for (auto& [sName, tMaterial] : pGroup->m_tChams.Visible)
	{
		auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

		m_tCurrentMaterial = tMaterial;
		F::Materials.SetColor(pMaterial, tMaterial.tColor);
		I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);

		bool bFlip = pMaterial && pMaterial->m_bInvertCull ? !G::FlipViewmodels : G::FlipViewmodels;
		pRenderContext->CullMode(bFlip ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);

		m_bRendering = true;
		fDraw();
		m_bRendering = false;
	}
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
