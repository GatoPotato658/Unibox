#include "Chams.h"

#include "../Visuals.h"
#include "../Groups/Groups.h"
#include "../Materials/Materials.h"
#include "../FakeAngle/FakeAngle.h"
#include "../../Backtrack/Backtrack.h"

static inline bool GetDistanceThing(float flDistance, const ChamsMaterial_t& tMaterial, Color_t& tColorOut)
{
	if (flDistance < tMaterial.flStart || flDistance > tMaterial.flEnd)
		return false;

	if (tMaterial.bSmoothAlpha)
	{
		tColorOut.a = Math::RemapVal(flDistance, tMaterial.flEnd - 256.f, tMaterial.flEnd, tColorOut.a, 0.f);
		if (tMaterial.flStart)
			tColorOut.a = Math::RemapVal(flDistance, tMaterial.flStart + 256.f, tMaterial.flStart, tColorOut.a, 0.f);
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

void CChams::DrawModel(CBaseEntity* pEntity, const Chams_t& tChams, IMatRenderContext* pRenderContext, int iModel, bool bTwoModel)
{
	if (!pEntity || !pEntity->GetModel())
		return;

	if (!m_iFlags && iModel == ModelEnum::Visible)
		m_mEntities[pEntity->entindex()];

	bool bOccluded = !tChams.Occluded.empty();
	bool bSame = tChams.Visible == tChams.Occluded;
	bTwoModel &= bOccluded && !bSame;

	if (iModel == ModelEnum::Visible && !bTwoModel && bSame)
		return;
	if (iModel == ModelEnum::Occluded && !bTwoModel && !bOccluded)
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
			pRenderContext->SetStencilReferenceValue(1);
			pRenderContext->SetStencilWriteMask(0xFF);
			pRenderContext->SetStencilTestMask(0x0);
		}

		auto& vMaterials = tChams.GetVisible();
		for (auto& [sName, tMaterial] : vMaterials)
		{
			Color_t tColor = tMaterial.tColor;
			if (m_flCurrentDistance > -1.f && !GetDistanceThing(m_flCurrentDistance, tMaterial, tColor))
				continue;
		
			auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

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
			pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_EQUAL);
			pRenderContext->SetStencilPassOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilReferenceValue(0);
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

	m_mEntities.clear();
	if (m_vEntities.empty())
		return;

	pRenderContext->ClearBuffers(false, false, true);

	for (auto& tInfo : m_vEntities)
	{
		m_flCurrentDistance = tInfo.m_flDistance;
		if (!tInfo.m_iFlags)
		{
			DrawModel(tInfo.m_pEntity, *tInfo.m_pChams, pRenderContext, ModelEnum::Visible, true);
			DrawModel(tInfo.m_pEntity, *tInfo.m_pChams, pRenderContext, ModelEnum::Occluded, true);
		}
		else
		{
			m_iFlags = tInfo.m_iFlags;

			auto pPlayer = tInfo.m_pEntity->As<CTFPlayer>();
			const float flOldInvisibility = pPlayer->m_flInvisibility();
			pPlayer->m_flInvisibility() = 0.f;
			DrawModel(tInfo.m_pEntity, *tInfo.m_pChams, pRenderContext, ModelEnum::Visible, true);
			DrawModel(tInfo.m_pEntity, *tInfo.m_pChams, pRenderContext, ModelEnum::Occluded, true);
			pPlayer->m_flInvisibility() = flOldInvisibility;

			m_iFlags = false;
		}
		pRenderContext->ClearBuffers(false, false, true);
	}

	pRenderContext->ClearBuffers(false, false, true);
}

void CChams::RenderFakeAngle(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo)
{
	static auto IVModelRender_DrawModelExecute = U::Hooks.m_mHooks["IVModelRender_DrawModelExecute"];
	IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, F::FakeAngle.aBones);
}
void CChams::RenderHandler(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (!m_iFlags)
	{
		static auto IVModelRender_DrawModelExecute = U::Hooks.m_mHooks["IVModelRender_DrawModelExecute"];
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

template <typename FDraw>
bool CChams::RenderViewmodelMaterial(IMatRenderContext* pRenderContext, Group_t* pGroup, MaterialCullMode_t restore, FDraw fDraw)
{
	Begin();
	for (auto& [sName, tMaterial] : pGroup->m_tChams.Visible)
	{
		auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

		F::Materials.SetColor(pMaterial, tMaterial.tColor);
		I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);

		bool bFlip = pMaterial && pMaterial->m_bInvertCull ? !G::FlipViewmodels : G::FlipViewmodels;
		pRenderContext->CullMode(bFlip ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);

		fDraw();
	}
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

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(rcx->IsValid() ? TargetsEnum::ViewmodelHands : TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tChams(true))
		return false;

	static auto CBaseAnimating_InternalDrawModel = U::Hooks.m_mHooks["CBaseAnimating_InternalDrawModel"];
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

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tChams(true))
		return false;

	static auto IVModelRender_DrawModelExecute = U::Hooks.m_mHooks["IVModelRender_DrawModelExecute"];
	return RenderViewmodelMaterial(pRenderContext, pGroup, MATERIAL_CULLMODE_CCW, [&]()
	{
		IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
	});
}
