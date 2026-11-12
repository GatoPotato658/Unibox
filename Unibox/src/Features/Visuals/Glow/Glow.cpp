#include "Glow.h"

#include "../Visuals.h"
#include "../Groups/Groups.h"
#include "../Materials/Materials.h"
#include "../FakeAngle/FakeAngle.h"
#include "../../Backtrack/Backtrack.h"

static inline bool GetDistanceThing(float flDistance, const Glow_t& tGlow, Color_t& tColorOut)
{
	if (flDistance < tGlow.Start || flDistance > tGlow.End)
		return false;

	if (tGlow.SmoothAlpha)
	{
		tColorOut.a = Math::RemapVal(flDistance, tGlow.End - 256.f, tGlow.End, tColorOut.a, 0.f);
		if (tGlow.Start)
			tColorOut.a = Math::RemapVal(flDistance, tGlow.Start + 256.f, tGlow.Start, tColorOut.a, 0.f);
	}
	return tColorOut.a;
}

void CGlow::Begin()
{
	m_tOriginalColor = I::RenderView->GetColorModulation();
	m_flOriginalBlend = I::RenderView->GetBlend();
	I::ModelRender->GetMaterialOverride(&m_pOriginalMaterial, &m_iOriginalOverride);

	I::RenderView->SetBlend(0.f);
	I::RenderView->SetColorModulation(1.f, 1.f, 1.f);
	I::ModelRender->ForcedMaterialOverride(m_pMatGlowColor.get());
}
void CGlow::End()
{
	I::RenderView->SetColorModulation(m_tOriginalColor);
	I::RenderView->SetBlend(m_flOriginalBlend);
	I::ModelRender->ForcedMaterialOverride(m_pOriginalMaterial, m_iOriginalOverride);
}

void CGlow::FirstBegin(IMatRenderContext* pRenderContext)
{
	Begin();

	pRenderContext->SetStencilEnable(true);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilReferenceValue(1);
	pRenderContext->SetStencilWriteMask(0xFF);
	pRenderContext->SetStencilTestMask(0x0);
}
void CGlow::FirstEnd(IMatRenderContext* pRenderContext)
{
	pRenderContext->SetStencilEnable(false);

	End();
}

void CGlow::SecondBegin(IMatRenderContext* pRenderContext, int w, int h)
{
	Begin();

	pRenderContext->PushRenderTargetAndViewport();
	pRenderContext->SetRenderTarget(m_pRenderBuffer1.get());
	pRenderContext->Viewport(0, 0, w, h);
	pRenderContext->ClearColor4ub(0, 0, 0, 0);
	pRenderContext->ClearBuffers(true, false, false);
}
void CGlow::SecondEnd(Glow_t tGlow, IMatRenderContext* pRenderContext, int w, int h)
{
	pRenderContext->PopRenderTargetAndViewport();

	if (tGlow.Blur && m_pMatBlurX && m_pMatBlurY)
	{
		if (auto pBloomAmount = m_pMatBlurY->FindVar("$bloomamount", nullptr))
			pBloomAmount->SetFloatValue(tGlow.Blur);

		pRenderContext->PushRenderTargetAndViewport();
		{
			pRenderContext->Viewport(0, 0, w, h);
			pRenderContext->SetRenderTarget(m_pRenderBuffer2.get());
			pRenderContext->DrawScreenSpaceRectangle(m_pMatBlurX.get(), 0, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
			pRenderContext->SetRenderTarget(m_pRenderBuffer1.get());
			pRenderContext->DrawScreenSpaceRectangle(m_pMatBlurY.get(), 0, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		}
		pRenderContext->PopRenderTargetAndViewport();
	}

	pRenderContext->SetStencilEnable(true);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_EQUAL);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilReferenceValue(0);
	pRenderContext->SetStencilWriteMask(0x0);
	pRenderContext->SetStencilTestMask(0xFF);

	if (tGlow.Stencil)
	{
		int iSide = (tGlow.Stencil + 1) / 2.f;
		pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), -iSide, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), 0, -iSide, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), iSide, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), 0, iSide, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		if (int iCorner = tGlow.Stencil / 2.f)
		{
			pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), -iCorner, -iCorner, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
			pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), iCorner, iCorner, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
			pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), iCorner, -iCorner, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
			pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), -iCorner, iCorner, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		}
	}
	if (tGlow.Blur && m_pMatBlurX && m_pMatBlurY)
		pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen.get(), 0, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);

	pRenderContext->SetStencilEnable(false);

	End();
}

void CGlow::DrawModel(CBaseEntity* pEntity)
{
	m_bRendering = true;

	if (pEntity->IsPlayer())
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		float flOldInvisibility = pPlayer->m_flInvisibility();
		pPlayer->m_flInvisibility() = 0.f;
		pEntity->DrawModel(STUDIO_RENDER | STUDIO_NOSHADOWS);
		pPlayer->m_flInvisibility() = flOldInvisibility;
	}
	else
		pEntity->DrawModel(STUDIO_RENDER | STUDIO_NOSHADOWS);

	m_bRendering = false;
}



void CGlow::Store(CTFPlayer* pLocal)
{
	m_mEntities.clear();
	if (!pLocal || !F::Groups.GroupsActive())
		return;

	Vector vLocalOrigin = pLocal->m_vecOrigin();
	int iLocalTeam = pLocal->m_iTeamNum();
	for (auto& [pEntity, pGroup] : F::Groups.GetGroup())
	{
		if (pEntity->IsDormant() || !pEntity->ShouldDraw())
			continue;

		Vector vEntOrigin = pEntity->m_vecOrigin();
		bool bWearableOrViewmodel = pEntity->IsBaseCombatWeapon() || pEntity->IsWearable();
		if (bWearableOrViewmodel && pEntity->m_hOwnerEntity().Get())
			vEntOrigin = pEntity->m_hOwnerEntity().Get()->m_vecOrigin();

		float flDistance = vLocalOrigin.DistTo(vEntOrigin);

		Color_t tColor = F::Groups.GetColor(pEntity, pGroup);
		Color_t tBacktrackColor = tColor;
		if (pGroup->m_tGlow() && !pEntity->IsWearableVM()
			&& GetDistanceThing(flDistance, pGroup->m_tGlow, tColor) && SDK::IsOnScreen(pEntity, pEntity->IsBaseCombatWeapon() || pEntity->IsWearable()))
			m_mEntities[pGroup->m_tGlow].emplace_back(pEntity, tColor);

		if (pEntity->IsPlayer() && pEntity != pLocal && pGroup->m_iBacktrack & BacktrackEnum::Enabled && pGroup->m_tBacktrackGlow()
			&& GetDistanceThing(flDistance, pGroup->m_tBacktrackGlow, tBacktrackColor)
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

				bool bIsTeammate = pEntity->m_iTeamNum() == iLocalTeam;
				if (bShowEnemy && !bIsTeammate || bShowFriendly && bIsTeammate)
					m_mEntities[pGroup->m_tBacktrackGlow].emplace_back(pEntity, tBacktrackColor, pGroup->m_iBacktrack);
			}
		}
	}

	Group_t* pGroup = nullptr;
	if (F::FakeAngle.bDrawChams && F::FakeAngle.bBonesSetup
		&& F::Groups.GetGroup(TargetsEnum::FakeAngle, pGroup) && pGroup->m_tGlow())
	{	// fakeangle
		m_mEntities[pGroup->m_tGlow].emplace_back(pLocal, pGroup->m_tColor, 1);
	}
}

void CGlow::RenderFirst()
{
	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext || !m_pMatGlowColor || !m_pRenderBuffer1 || !m_pMatHaloAddToScreen)
		return;

	FirstBegin(pRenderContext);
	for (auto& [tGlow, vInfo] : m_mEntities)
	{
		for (auto& tInfo : vInfo)
		{
			m_iFlags = tInfo.m_iFlags;
			DrawModel(tInfo.m_pEntity);
			m_iFlags = false;
		}
	}
	FirstEnd(pRenderContext);
}

void CGlow::RenderSecond()
{
	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext || !m_pMatGlowColor)
		return;

	if (m_pRenderBuffer1 && m_pMatHaloAddToScreen)
	{
		const int w = H::Draw.m_nScreenW, h = H::Draw.m_nScreenH;
		for (auto& [tGlow, vInfo] : m_mEntities)
		{
			SecondBegin(pRenderContext, w, h);
			for (auto& tInfo : vInfo)
			{
				I::RenderView->SetColorModulation(tInfo.m_tColor);
				I::RenderView->SetBlend(tInfo.m_tColor.a / 255.f);

				m_iFlags = tInfo.m_iFlags;
				DrawModel(tInfo.m_pEntity);
				m_iFlags = false;
			}
			SecondEnd(tGlow, pRenderContext, w, h);
		}
		return;
	}

	m_tOriginalColor = I::RenderView->GetColorModulation();
	m_flOriginalBlend = I::RenderView->GetBlend();
	I::ModelRender->GetMaterialOverride(&m_pOriginalMaterial, &m_iOriginalOverride);
	I::ModelRender->ForcedMaterialOverride(m_pMatGlowColor.get());
	for (auto& [tGlow, vInfo] : m_mEntities)
	{
		for (auto& tInfo : vInfo)
		{
			I::RenderView->SetColorModulation(tInfo.m_tColor);
			I::RenderView->SetBlend(tInfo.m_tColor.a / 255.f);
			m_iFlags = tInfo.m_iFlags;
			DrawModel(tInfo.m_pEntity);
			m_iFlags = false;
		}
	}
	End();
}

void CGlow::RenderFakeAngle(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo)
{
	static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");
	IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, F::FakeAngle.aBones);
}
void CGlow::RenderHandler(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (!m_iFlags)
	{
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

void CGlow::RenderViewmodel(CBaseAnimating* rcx, int flags)
{
	if (!F::Groups.GroupsActive())
		return;

	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext || !m_pMatGlowColor)
		return;

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(rcx->IsValid() ? TargetsEnum::ViewmodelHands : TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tGlow())
		return;

	static auto CBaseAnimating_InternalDrawModel = U::Hooks.Get("CBaseAnimating_InternalDrawModel");

	if (m_pRenderBuffer1 && m_pMatHaloAddToScreen)
	{
		const int w = H::Draw.m_nScreenW, h = H::Draw.m_nScreenH;

		pRenderContext->CullMode(MATERIAL_CULLMODE_CCW); // glow won't work properly with MATERIAL_CULLMODE_CW
		FirstBegin(pRenderContext);
		CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
		FirstEnd(pRenderContext);
		SecondBegin(pRenderContext, w, h);
		I::RenderView->SetColorModulation(pGroup->m_tColor);
		I::RenderView->SetBlend(pGroup->m_tColor.a / 255.f);
		CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
		SecondEnd(pGroup->m_tGlow, pRenderContext, w, h);
		pRenderContext->CullMode(G::FlipViewmodels ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);
		return;
	}

	m_tOriginalColor = I::RenderView->GetColorModulation();
	m_flOriginalBlend = I::RenderView->GetBlend();
	I::ModelRender->GetMaterialOverride(&m_pOriginalMaterial, &m_iOriginalOverride);
	I::ModelRender->ForcedMaterialOverride(m_pMatGlowColor.get());
	I::RenderView->SetColorModulation(pGroup->m_tColor);
	I::RenderView->SetBlend(pGroup->m_tColor.a / 255.f);
	CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
	End();
}
void CGlow::RenderViewmodel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (!F::Groups.GroupsActive())
		return;

	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext || !m_pMatGlowColor)
		return;

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tGlow())
		return;

	static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");

	if (m_pRenderBuffer1 && m_pMatHaloAddToScreen)
	{
		const int w = H::Draw.m_nScreenW, h = H::Draw.m_nScreenH;

		FirstBegin(pRenderContext);
		IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
		FirstEnd(pRenderContext);
		SecondBegin(pRenderContext, w, h);
		I::RenderView->SetColorModulation(pGroup->m_tColor);
		I::RenderView->SetBlend(pGroup->m_tColor.a / 255.f);
		IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
		SecondEnd(pGroup->m_tGlow, pRenderContext, w, h);
		return;
	}

	m_tOriginalColor = I::RenderView->GetColorModulation();
	m_flOriginalBlend = I::RenderView->GetBlend();
	I::ModelRender->GetMaterialOverride(&m_pOriginalMaterial, &m_iOriginalOverride);
	I::ModelRender->ForcedMaterialOverride(m_pMatGlowColor.get());
	I::RenderView->SetColorModulation(pGroup->m_tColor);
	I::RenderView->SetBlend(pGroup->m_tColor.a / 255.f);
	IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
	End();
}



void CGlow::Initialize()
{
	SDK::CInitTimingScope tInit("Glow.Initialize");
	int nWidth, nHeight; I::MatSystemSurface->GetScreenSize(nWidth, nHeight);
	const bool bDx9Shaders = SDK::SupportsDX9Shaders();

	if (!m_pMatGlowColor || m_pMatGlowColor->IsErrorMaterial())
	{
		m_pMatGlowColor.reset();
		auto pMaterial = I::MaterialSystem->FindMaterial("dev/glow_color", TEXTURE_GROUP_OTHER);
		if (pMaterial && !pMaterial->IsErrorMaterial())
			m_pMatGlowColor = F::Materials.Share(pMaterial);
	}

	if (!bDx9Shaders)
		return;

	auto fCreateBuffer = [&](const char* sName)
	{
		auto pNew = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
			sName,
			nWidth, nHeight,
			RT_SIZE_LITERAL,
			IMAGE_FORMAT_RGB888,
			MATERIAL_RT_DEPTH_SHARED,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_EIGHTBITALPHA,
			CREATERENDERTARGETFLAGS_HDR
		);
		return std::shared_ptr<ITexture>(
			pNew,
			[](ITexture* pTexture)
			{
				pTexture->DecrementReferenceCount();
				pTexture->DeleteIfUnreferenced();
			}
		);
	};
	m_pRenderBuffer1 = fCreateBuffer("RenderBuffer1");
	m_pRenderBuffer2 = fCreateBuffer("RenderBuffer2");

	if (!m_pMatHaloAddToScreen)
		m_pMatHaloAddToScreen = F::Materials.Share(F::Materials.create_from_vmt("MatHaloAddToScreen",
			"\"UnlitGeneric\""
			"\n{"
			"\n\t$basetexture \"RenderBuffer1\""
			"\n\t$additive \"1\""
			"\n}"));

	if (!m_pMatBlurX)
		m_pMatBlurX = F::Materials.Share(F::Materials.create_from_vmt("MatBlurX",
			"\"BlurFilterX\""
			"\n{"
			"\n\t$basetexture \"RenderBuffer1\""
			"\n}"));

	if (!m_pMatBlurY)
		m_pMatBlurY = F::Materials.Share(F::Materials.create_from_vmt("MatBlurY",
			"\"BlurFilterY\""
			"\n{"
			"\n\t$basetexture \"RenderBuffer2\""
			"\n}"));
}

void CGlow::Unload()
{
	m_pMatGlowColor.reset();
	m_pMatBlurX = {};
	m_pMatBlurY = {};
	m_pMatHaloAddToScreen = {};
	m_pRenderBuffer1 = {};
	m_pRenderBuffer2 = {};
}
