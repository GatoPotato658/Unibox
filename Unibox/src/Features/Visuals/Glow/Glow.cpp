#include "Glow.h"

#include "../Visuals.h"
#include "../Groups/Groups.h"
#include "../Materials/Materials.h"
#include "../FakeAngle/FakeAngle.h"
#include "../../Backtrack/Backtrack.h"
#include "../Chams/RenderQueue.h"
#include "D3DGlow.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

#if defined(_WIN32) && !defined(TEXTMODE)
static CD3DGlow s_D3DGlow;
#endif

static auto GetGlowContext()
{
	return std::unique_ptr<IMatRenderContext, void(*)(IMatRenderContext*)>(
		I::MaterialSystem->GetRenderContext(), [](auto pContext) { if (pContext) pContext->Release(); });
}

static void ClearGlowStencil(IMatRenderContext* pRenderContext)
{
	pRenderContext->SetStencilEnable(true);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilReferenceValue(0);
	pRenderContext->SetStencilWriteMask(0xC0);
	pRenderContext->SetStencilTestMask(0xC0);
	pRenderContext->PerformFullScreenStencilOperation();
	pRenderContext->SetStencilEnable(false);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilWriteMask(0xFF);
	pRenderContext->SetStencilTestMask(0xFF);
}

static Glow_t NormalizeGlow(Glow_t tGlow)
{
	tGlow.Sanitize();
	return tGlow;
}

static inline bool GetDistanceThing(float flDistance, const Glow_t& tGlow, Color_t& tColorOut)
{
	if (flDistance < tGlow.Start || flDistance > tGlow.End)
		return false;

	if (tGlow.SmoothAlpha)
	{
		float flAlpha = std::clamp((tGlow.End - flDistance) / 256.f, 0.f, 1.f);
		if (tGlow.Start > 0.f)
			flAlpha *= std::clamp((flDistance - tGlow.Start) / 256.f, 0.f, 1.f);
		tColorOut.a = static_cast<unsigned char>(tColorOut.a * flAlpha);
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
	I::ModelRender->ForcedMaterialOverride(m_pMatGlowColor);
}
void CGlow::End()
{
	I::RenderView->SetColorModulation(m_tOriginalColor);
	I::RenderView->SetBlend(m_flOriginalBlend);
	I::ModelRender->ForcedMaterialOverride(m_pOriginalMaterial, m_iOriginalOverride);
}

void CGlow::LegacyBegin()
{
	m_tOriginalColor = I::RenderView->GetColorModulation();
	m_flOriginalBlend = I::RenderView->GetBlend();
	I::ModelRender->GetMaterialOverride(&m_pOriginalMaterial, &m_iOriginalOverride);

	I::RenderView->SetBlend(0.f);
	I::RenderView->SetColorModulation(1.f, 1.f, 1.f);
	I::ModelRender->ForcedMaterialOverride(m_pLegacyGlowColor);
}

bool CGlow::LegacyReady() const
{
	return m_pLegacyGlowColor && m_pLegacyBuffer1 && m_pLegacyHalo;
}

void CGlow::LegacyFirstBegin(IMatRenderContext* pRenderContext)
{
	LegacyBegin();

	pRenderContext->SetStencilEnable(true);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilReferenceValue(1);
	pRenderContext->SetStencilWriteMask(0xFF);
	pRenderContext->SetStencilTestMask(0x0);
}

void CGlow::LegacyFirstEnd(IMatRenderContext* pRenderContext)
{
	pRenderContext->SetStencilEnable(false);

	End();
}

void CGlow::LegacySecondBegin(IMatRenderContext* pRenderContext, int w, int h)
{
	LegacyBegin();

	pRenderContext->PushRenderTargetAndViewport();
	pRenderContext->SetRenderTarget(m_pLegacyBuffer1);
	pRenderContext->Viewport(0, 0, w, h);
	pRenderContext->ClearColor4ub(0, 0, 0, 0);
	pRenderContext->ClearBuffers(true, false, false);
}

void CGlow::LegacySecondEnd(const Glow_t& tGlow, IMatRenderContext* pRenderContext, int w, int h)
{
	pRenderContext->PopRenderTargetAndViewport();

	const bool bBlur = tGlow.Blur > 0.f && m_pLegacyBlurX && m_pLegacyBlurY && m_pLegacyBuffer2;
	if (bBlur)
	{
		if (auto pBloomAmount = m_pLegacyBlurY->FindVar("$bloomamount", nullptr))
			pBloomAmount->SetFloatValue(tGlow.Blur);

		pRenderContext->PushRenderTargetAndViewport();
		{
			pRenderContext->Viewport(0, 0, w, h);
			pRenderContext->SetRenderTarget(m_pLegacyBuffer2);
			pRenderContext->DrawScreenSpaceRectangle(m_pLegacyBlurX, 0, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
			pRenderContext->SetRenderTarget(m_pLegacyBuffer1);
			pRenderContext->DrawScreenSpaceRectangle(m_pLegacyBlurY, 0, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
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
		int iSide = (tGlow.Stencil + 1) / 2;
		pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, -iSide, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, 0, -iSide, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, iSide, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, 0, iSide, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		if (int iCorner = tGlow.Stencil / 2)
		{
			pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, -iCorner, -iCorner, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
			pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, iCorner, iCorner, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
			pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, iCorner, -iCorner, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
			pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, -iCorner, iCorner, w, h, 0.f, 0.f, w - 1, h - 1, w, h);
		}
	}
	if (bBlur)
		pRenderContext->DrawScreenSpaceRectangle(m_pLegacyHalo, 0, 0, w, h, 0.f, 0.f, w - 1, h - 1, w, h);

	pRenderContext->SetStencilEnable(false);

	End();
}

void CGlow::FirstBegin(IMatRenderContext* pRenderContext)
{
	Begin();

	pRenderContext->OverrideDepthEnable(true, false);
	pRenderContext->OverrideColorWriteEnable(true, false);
	pRenderContext->OverrideAlphaWriteEnable(true, false);
	pRenderContext->SetStencilEnable(true);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilReferenceValue(0);
	pRenderContext->SetStencilWriteMask(0x80);
	pRenderContext->SetStencilTestMask(0x80);
	pRenderContext->PerformFullScreenStencilOperation();
	pRenderContext->SetStencilReferenceValue(0x80);
}
void CGlow::FirstEnd(IMatRenderContext* pRenderContext)
{
	pRenderContext->SetStencilEnable(false);
	pRenderContext->OverrideDepthEnable(false, false);
	pRenderContext->OverrideColorWriteEnable(false, false);
	pRenderContext->OverrideAlphaWriteEnable(false, false);

	End();
}

bool CGlow::GetDimensions(IMatRenderContext* pRenderContext, int& w, int& h)
{
	if (!pRenderContext || !m_pMatGlowColor || !m_pMatGlowVisible || !m_pRenderBuffer1
		|| !m_pRenderBuffer2 || !m_pRenderBuffer3 || !m_pMatHaloAddToScreen)
		return false;
	if (m_pMatGlowColor->IsErrorMaterial() || m_pMatGlowVisible->IsErrorMaterial()
		|| m_pMatHaloAddToScreen->IsErrorMaterial() || m_pRenderBuffer1->IsError()
		|| m_pRenderBuffer2->IsError() || m_pRenderBuffer3->IsError())
		return false;
	int x, y;
	pRenderContext->GetViewport(x, y, w, h);
	return w > 0 && h > 0 && w <= m_pRenderBuffer1->GetActualWidth()
		&& h <= m_pRenderBuffer1->GetActualHeight();
}

void CGlow::SecondBegin(const Glow_t& tGlow, IMatRenderContext* pRenderContext, int w, int h)
{
	Begin();
	m_bD3DPass = false;
#if defined(_WIN32) && !defined(TEXTMODE)
	if (I::MaterialSystem->GetRenderBackend() == RENDER_BACKEND_D3D9 && I::DirectXDevice)
	{
		auto pDevice = I::DirectXDevice;
		QueueRenderCall(pRenderContext, [pDevice] { s_D3DGlow.Initialize(pDevice); });
		m_bD3DPass = s_D3DGlow.IsReady();
		if (m_bD3DPass)
			m_bD3DPass = QueueRenderCall(pRenderContext, [w, h] { s_D3DGlow.Begin(w, h); });
	}
#endif

	pRenderContext->PushRenderTargetAndViewport();
	pRenderContext->SetRenderTarget(m_pRenderBuffer1);
	pRenderContext->Viewport(0, 0, w, h);
	pRenderContext->ClearColor4ub(0, 0, 0, 0);
	pRenderContext->ClearBuffers(true, false, false);
	pRenderContext->OverrideAlphaWriteEnable(true, true);
	if (tGlow.Depth == 1)
		I::ModelRender->ForcedMaterialOverride(m_pMatGlowVisible);
}

void CGlow::BeginVisibleMask(IMatRenderContext* pRenderContext)
{
	pRenderContext->OverrideColorWriteEnable(true, false);
	pRenderContext->OverrideAlphaWriteEnable(false, false);
	pRenderContext->OverrideAlphaWriteEnable(true, false);
	pRenderContext->SetStencilEnable(true);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilReferenceValue(0);
	pRenderContext->SetStencilWriteMask(0x40);
	pRenderContext->SetStencilTestMask(0x40);
	pRenderContext->PerformFullScreenStencilOperation();
	pRenderContext->SetStencilReferenceValue(0x40);
	I::ModelRender->ForcedMaterialOverride(m_pMatGlowVisible);
}

void CGlow::EndVisibleMask(IMatRenderContext* pRenderContext)
{
	pRenderContext->OverrideColorWriteEnable(false, false);
	pRenderContext->OverrideAlphaWriteEnable(false, false);
	pRenderContext->OverrideAlphaWriteEnable(true, true);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_EQUAL);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilReferenceValue(0);
	pRenderContext->SetStencilWriteMask(0);
	I::ModelRender->ForcedMaterialOverride(m_pMatGlowColor);
}

void CGlow::SecondEnd(Glow_t tGlow, IMatRenderContext* pRenderContext, int w, int h)
{
	tGlow = NormalizeGlow(tGlow);
	pRenderContext->SetStencilEnable(false);
	pRenderContext->OverrideDepthEnable(false, false);
	pRenderContext->OverrideAlphaWriteEnable(false, false);
#if defined(_WIN32) && !defined(TEXTMODE)
	if (m_bD3DPass && QueueRenderCall(pRenderContext, [tGlow] { s_D3DGlow.Render(tGlow); }))
	{
		pRenderContext->PopRenderTargetAndViewport();
		End();
		return;
	}
#endif
	pRenderContext->PopRenderTargetAndViewport();
	I::RenderView->SetColorModulation(1.f, 1.f, 1.f);
	I::RenderView->SetBlend(1.f);

	const int nTextureW = m_pRenderBuffer1->GetActualWidth();
	const int nTextureH = m_pRenderBuffer1->GetActualHeight();
	int nBlurW = w, nBlurH = h;
	const bool bBlur = tGlow.Blur > 0.f && tGlow.Bloom > 0.f
		&& m_pMatBlurX && m_pMatBlurY && m_pMatDownsample && m_pMatBloomAdd;
	if (bBlur)
	{
		pRenderContext->OverrideAlphaWriteEnable(true, true);
		const float flScale = std::clamp(tGlow.Blur / 3.2307692308f, 1.f, 5.f);
		nBlurW = (std::max)(1, int(w / flScale));
		nBlurH = (std::max)(1, int(h / flScale));
		bool bFound = false;
		auto pBloomAmount = m_pMatBlurY->FindVar("$bloomamount", &bFound, false);
		if (bFound && pBloomAmount)
			pBloomAmount->SetFloatValue(1.f);
		pRenderContext->PushRenderTargetAndViewport();
		pRenderContext->Viewport(0, 0, nBlurW, nBlurH);
		pRenderContext->SetRenderTarget(m_pRenderBuffer3);
		pRenderContext->ClearBuffers(true, false, false);
		pRenderContext->DrawScreenSpaceRectangle(m_pMatDownsample, 0, 0, nBlurW, nBlurH,
			0.f, 0.f, w - 1.f, h - 1.f, nTextureW, nTextureH);
		pRenderContext->SetRenderTarget(m_pRenderBuffer2);
		pRenderContext->ClearBuffers(true, false, false);
		pRenderContext->DrawScreenSpaceRectangle(m_pMatBlurX, 0, 0, nBlurW, nBlurH,
			0.f, 0.f, nBlurW - 1.f, nBlurH - 1.f, nTextureW, nTextureH);
		pRenderContext->SetRenderTarget(m_pRenderBuffer3);
		pRenderContext->ClearBuffers(true, false, false);
		pRenderContext->DrawScreenSpaceRectangle(m_pMatBlurY, 0, 0, nBlurW, nBlurH,
			0.f, 0.f, nBlurW - 1.f, nBlurH - 1.f, nTextureW, nTextureH);
		pRenderContext->PopRenderTargetAndViewport();
		pRenderContext->OverrideAlphaWriteEnable(false, false);
	}

	pRenderContext->SetStencilEnable(true);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_EQUAL);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilReferenceValue(0);
	pRenderContext->SetStencilWriteMask(0x0);
	pRenderContext->SetStencilTestMask(0x80);

	auto fDraw = [&](IMaterial* pMaterial, int x, int y, int nSourceW, int nSourceH, float weight, float tint)
	{
		if (!pMaterial)
			return;
		const float flAlpha = tGlow.Opacity * weight * (1.f + (tGlow.OuterColor.a / 255.f - 1.f) * tint);
		const float flBrightness = tGlow.Intensity * (tGlow.Blend == 1 ? 1.f : flAlpha);
		pMaterial->ColorModulate(
			flBrightness * (1.f + (tGlow.OuterColor.r / 255.f - 1.f) * tint),
			flBrightness * (1.f + (tGlow.OuterColor.g / 255.f - 1.f) * tint),
			flBrightness * (1.f + (tGlow.OuterColor.b / 255.f - 1.f) * tint));
		pMaterial->AlphaModulate(tGlow.Blend == 1 ? std::clamp(flAlpha, 0.f, 1.f) : 1.f);
		pRenderContext->DrawScreenSpaceRectangle(pMaterial, x, y, w, h, 0.f, 0.f,
			nSourceW - 1.f, nSourceH - 1.f, nTextureW, nTextureH);
		pMaterial->ColorModulate(1.f, 1.f, 1.f);
		pMaterial->AlphaModulate(1.f);
	};
	auto pOutline = tGlow.Blend == 1 && m_pMatHaloAlpha ? m_pMatHaloAlpha : m_pMatHaloAddToScreen;
	auto pBloom = tGlow.Blend == 1 && m_pMatBloomAlpha ? m_pMatBloomAlpha : m_pMatBloomAdd;
	if (bBlur)
		fDraw(pBloom, 0, 0, nBlurW, nBlurH, tGlow.Bloom, tGlow.CustomOuter ? 1.f : 0.f);
	const int nLayers = (std::min)(tGlow.Layers, (std::max)(tGlow.Stencil, 1));
	for (int i = nLayers - 1; i >= 0 && tGlow.Stencil; --i)
	{
		const int nRadius = (tGlow.Stencil * (i + 1) + nLayers - 1) / nLayers;
		const float flWeight = std::pow(1.f - float(i) / nLayers, tGlow.Falloff) / 8.f;
		const float flTint = tGlow.CustomOuter ? (nLayers > 1 ? float(i) / (nLayers - 1) : 1.f) : 0.f;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				if (x || y)
					fDraw(pOutline, x * nRadius, y * nRadius, w, h, flWeight, flTint);
			}
		}
	}

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
	if (m_mEntities.empty() || !LegacyReady())
		return;
	if (std::ranges::none_of(m_mEntities, [](const auto& tEntry) { return !tEntry.first.Style && !tEntry.second.empty(); }))
		return;
	const auto tContext = GetGlowContext();
	auto pRenderContext = tContext.get();
	if (!pRenderContext)
		return;

	LegacyFirstBegin(pRenderContext);
	for (auto& [tGlow, vInfo] : m_mEntities)
	{
		if (tGlow.Style)
			continue;
		for (auto& tInfo : vInfo)
		{
			m_iFlags = tInfo.m_iFlags;
			DrawModel(tInfo.m_pEntity);
			m_iFlags = false;
		}
	}
	LegacyFirstEnd(pRenderContext);
}

void CGlow::RenderLegacy(IMatRenderContext* pRenderContext)
{
	if (std::ranges::none_of(m_mEntities, [](const auto& tEntry) { return !tEntry.first.Style && !tEntry.second.empty(); }))
		return;

	if (LegacyReady())
	{
		const int w = H::Draw.m_nScreenW, h = H::Draw.m_nScreenH;
		for (auto& [tGlow, vInfo] : m_mEntities)
		{
			if (tGlow.Style || vInfo.empty())
				continue;
			LegacySecondBegin(pRenderContext, w, h);
			for (auto& tInfo : vInfo)
			{
				I::RenderView->SetColorModulation(tInfo.m_tColor);
				I::RenderView->SetBlend(tInfo.m_tColor.a / 255.f);

				m_iFlags = tInfo.m_iFlags;
				DrawModel(tInfo.m_pEntity);
				m_iFlags = false;
			}
			LegacySecondEnd(tGlow, pRenderContext, w, h);
		}
		return;
	}

	if (!m_pLegacyGlowColor)
		return;
	LegacyBegin();
	for (auto& [tGlow, vInfo] : m_mEntities)
	{
		if (tGlow.Style)
			continue;
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

void CGlow::MarkEnhanced(IMatRenderContext* pRenderContext)
{
	FirstBegin(pRenderContext);
	for (auto& [tGlow, vInfo] : m_mEntities)
	{
		if (!tGlow.Style)
			continue;
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
	if (m_mEntities.empty())
		return;
	const auto tContext = GetGlowContext();
	auto pRenderContext = tContext.get();
	if (!pRenderContext)
		return;

	RenderLegacy(pRenderContext);

	if (std::ranges::none_of(m_mEntities, [](const auto& tEntry) { return tEntry.first.Style && !tEntry.second.empty(); }))
		return;
	int w, h;
	if (!GetDimensions(pRenderContext, w, h))
		return;

	MarkEnhanced(pRenderContext);
	for (auto& [tSettings, vInfo] : m_mEntities)
	{
		const auto tGlow = NormalizeGlow(tSettings);
		if (!tGlow.Style || vInfo.empty() || tGlow.Intensity <= 0.f || tGlow.Opacity <= 0.f)
			continue;
		SecondBegin(tGlow, pRenderContext, w, h);
		auto fDraw = [&](bool bMask)
		{
			for (auto& tInfo : vInfo)
			{
				I::RenderView->SetColorModulation(tInfo.m_tColor);
				I::RenderView->SetBlend(bMask ? (std::min)(tInfo.m_tColor.a / 255.f, 254.f / 255.f) : 254.f / 255.f);
				m_iFlags = tInfo.m_iFlags;
				DrawModel(tInfo.m_pEntity);
				m_iFlags = false;
			}
		};
		if (tGlow.Depth == 2)
		{
			BeginVisibleMask(pRenderContext);
			fDraw(false);
			EndVisibleMask(pRenderContext);
		}
		fDraw(true);
		SecondEnd(tGlow, pRenderContext, w, h);
	}
	ClearGlowStencil(pRenderContext);
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

	const auto tContext = GetGlowContext();
	auto pRenderContext = tContext.get();
	if (!pRenderContext)
		return;

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(rcx->IsValid() ? TargetsEnum::ViewmodelHands : TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tGlow())
		return;

	static auto CBaseAnimating_InternalDrawModel = U::Hooks.Get("CBaseAnimating_InternalDrawModel");

	if (!pGroup->m_tGlow.Style)
	{
		if (LegacyReady())
		{
			const int nW = H::Draw.m_nScreenW, nH = H::Draw.m_nScreenH;

			pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
			LegacyFirstBegin(pRenderContext);
			CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
			LegacyFirstEnd(pRenderContext);
			LegacySecondBegin(pRenderContext, nW, nH);
			I::RenderView->SetColorModulation(pGroup->m_tColor);
			I::RenderView->SetBlend(pGroup->m_tColor.a / 255.f);
			CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
			LegacySecondEnd(pGroup->m_tGlow, pRenderContext, nW, nH);
			pRenderContext->CullMode(G::FlipViewmodels ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);
		}
		else if (m_pLegacyGlowColor)
		{
			LegacyBegin();
			I::RenderView->SetColorModulation(pGroup->m_tColor);
			I::RenderView->SetBlend(pGroup->m_tColor.a / 255.f);
			CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
			End();
		}
		return;
	}

	int w, h;
	if (!GetDimensions(pRenderContext, w, h))
		return;

	const auto tGlow = NormalizeGlow(pGroup->m_tGlow);
	if (tGlow.Intensity <= 0.f || tGlow.Opacity <= 0.f)
		return;
	m_bRendering = true;
	pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
	FirstBegin(pRenderContext);
	CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
	FirstEnd(pRenderContext);
	SecondBegin(tGlow, pRenderContext, w, h);
	if (tGlow.Depth == 2)
	{
		BeginVisibleMask(pRenderContext);
		I::RenderView->SetBlend(254.f / 255.f);
		CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
		EndVisibleMask(pRenderContext);
	}
	I::RenderView->SetColorModulation(pGroup->m_tColor);
	I::RenderView->SetBlend((std::min)(pGroup->m_tColor.a / 255.f, 254.f / 255.f));
	CBaseAnimating_InternalDrawModel->As<InternalDrawModelFn>()(rcx, flags);
	SecondEnd(tGlow, pRenderContext, w, h);
	m_bRendering = false;
	ClearGlowStencil(pRenderContext);
	pRenderContext->CullMode(G::FlipViewmodels ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);
}
void CGlow::RenderViewmodel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	if (!F::Groups.GroupsActive())
		return;

	const auto tContext = GetGlowContext();
	auto pRenderContext = tContext.get();
	if (!pRenderContext)
		return;

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tGlow())
		return;

	static auto IVModelRender_DrawModelExecute = U::Hooks.Get("IVModelRender_DrawModelExecute");

	if (!pGroup->m_tGlow.Style)
	{
		if (LegacyReady())
		{
			const int nW = H::Draw.m_nScreenW, nH = H::Draw.m_nScreenH;

			LegacyFirstBegin(pRenderContext);
			IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
			LegacyFirstEnd(pRenderContext);
			LegacySecondBegin(pRenderContext, nW, nH);
			I::RenderView->SetColorModulation(pGroup->m_tColor);
			I::RenderView->SetBlend(pGroup->m_tColor.a / 255.f);
			IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
			LegacySecondEnd(pGroup->m_tGlow, pRenderContext, nW, nH);
		}
		else if (m_pLegacyGlowColor)
		{
			LegacyBegin();
			I::RenderView->SetColorModulation(pGroup->m_tColor);
			I::RenderView->SetBlend(pGroup->m_tColor.a / 255.f);
			IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
			End();
		}
		return;
	}

	int w, h;
	if (!GetDimensions(pRenderContext, w, h))
		return;

	const auto tGlow = NormalizeGlow(pGroup->m_tGlow);
	if (tGlow.Intensity <= 0.f || tGlow.Opacity <= 0.f)
		return;
	m_bRendering = true;
	FirstBegin(pRenderContext);
	IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
	FirstEnd(pRenderContext);
	SecondBegin(tGlow, pRenderContext, w, h);
	if (tGlow.Depth == 2)
	{
		BeginVisibleMask(pRenderContext);
		I::RenderView->SetBlend(254.f / 255.f);
		IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
		EndVisibleMask(pRenderContext);
	}
	I::RenderView->SetColorModulation(pGroup->m_tColor);
	I::RenderView->SetBlend((std::min)(pGroup->m_tColor.a / 255.f, 254.f / 255.f));
	IVModelRender_DrawModelExecute->As<DrawModelExecuteFn>()(pModelRender, pState, pInfo, pBoneToWorld);
	SecondEnd(tGlow, pRenderContext, w, h);
	m_bRendering = false;
	ClearGlowStencil(pRenderContext);
}



void CGlow::Initialize()
{
	SDK::CInitTimingScope tInit("Glow.Initialize");
	int nWidth, nHeight; I::MatSystemSurface->GetScreenSize(nWidth, nHeight);
	const bool bDx9Shaders = SDK::SupportsDX9Shaders();

	if (!m_pLegacyGlowColor)
	{
		auto pMaterial = I::MaterialSystem->FindMaterial("dev/glow_color", TEXTURE_GROUP_OTHER);
		if (pMaterial && !pMaterial->IsErrorMaterial())
		{
			m_pLegacyGlowColor = pMaterial;
			m_pLegacyGlowColor->IncrementReferenceCount();
			F::Materials.m_mMatList[m_pLegacyGlowColor];
		}
	}

	if (!bDx9Shaders)
		return;

	InitializeLegacy(nWidth, nHeight);

	if (!m_pMatGlowColor)
		m_pMatGlowColor = F::Materials.create_from_vmt("UniboxGlowColor",
			"\"UnlitGeneric\" { \"$basetexture\" \"white\" \"$model\" \"1\" \"$ignorez\" \"1\" "
			"\"$linearwrite\" \"1\" \"$translucent\" \"1\" }");

	if (!m_pMatGlowVisible)
		m_pMatGlowVisible = F::Materials.create_from_vmt("UniboxGlowVisible",
			"\"UnlitGeneric\" { \"$basetexture\" \"white\" \"$model\" \"1\" \"$ignorez\" \"0\" "
			"\"$linearwrite\" \"1\" \"$translucent\" \"1\" }");

	auto fCreateBuffer = [&](const char* sName)
	{
		auto pNew = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
			sName,
			nWidth, nHeight,
			RT_SIZE_FULL_FRAME_BUFFER,
			IMAGE_FORMAT_BGRA8888,
			MATERIAL_RT_DEPTH_SHARED,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_EIGHTBITALPHA,
			0
		);
		return pNew;
	};
	if (!m_pRenderBuffer1)
		m_pRenderBuffer1 = fCreateBuffer("UniboxGlowMask");
	if (!m_pRenderBuffer2)
		m_pRenderBuffer2 = fCreateBuffer("UniboxGlowBlur");
	if (!m_pRenderBuffer3)
		m_pRenderBuffer3 = fCreateBuffer("UniboxGlowBloom");

	auto fComposite = [&](IMaterial*& pMaterial, const char* sName, const char* sTexture, bool bAlpha)
	{
		if (!pMaterial)
			pMaterial = F::Materials.create_from_vmt(sName,
				std::format("\"UnlitGeneric\" {{ \"$basetexture\" \"{}\" \"$ignorez\" \"1\" "
					"\"$linearread_basetexture\" \"1\" \"$linearwrite\" \"1\" \"${}\" \"1\" }}",
					sTexture, bAlpha ? "translucent" : "additive"));
	};
	fComposite(m_pMatHaloAddToScreen, "UniboxGlowOutlineAdd", "UniboxGlowMask", false);
	fComposite(m_pMatHaloAlpha, "UniboxGlowOutlineAlpha", "UniboxGlowMask", true);
	fComposite(m_pMatBloomAdd, "UniboxGlowBloomAdd", "UniboxGlowBloom", false);
	fComposite(m_pMatBloomAlpha, "UniboxGlowBloomAlpha", "UniboxGlowBloom", true);

	if (!m_pMatDownsample)
		m_pMatDownsample = F::Materials.create_from_vmt("UniboxGlowDownsample",
			"\"UnlitGeneric\" { \"$basetexture\" \"UniboxGlowMask\" \"$ignorez\" \"1\" "
			"\"$linearread_basetexture\" \"1\" \"$linearwrite\" \"1\" \"$translucent\" \"1\" }");

	if (!m_pMatBlurX)
	{
		m_pMatBlurX = F::Materials.create_from_vmt("UniboxGlowBlurX",
			"\"BlurFilterX\""
			"\n{"
			"\n\t$basetexture \"UniboxGlowBloom\""
			"\n}");
	}

	if (!m_pMatBlurY)
	{
		m_pMatBlurY = F::Materials.create_from_vmt("UniboxGlowBlurY",
			"\"BlurFilterY\""
			"\n{"
			"\n\t$basetexture \"UniboxGlowBlur\""
			"\n}");
	}
}

void CGlow::InitializeLegacy(int nWidth, int nHeight)
{
	auto fCreateBuffer = [&](const char* sName)
	{
		return I::MaterialSystem->CreateNamedRenderTargetTextureEx(
			sName,
			nWidth, nHeight,
			RT_SIZE_LITERAL,
			IMAGE_FORMAT_RGB888,
			MATERIAL_RT_DEPTH_SHARED,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_EIGHTBITALPHA,
			CREATERENDERTARGETFLAGS_HDR
		);
	};
	if (!m_pLegacyBuffer1)
		m_pLegacyBuffer1 = fCreateBuffer("UniboxLegacyGlow1");
	if (!m_pLegacyBuffer2)
		m_pLegacyBuffer2 = fCreateBuffer("UniboxLegacyGlow2");

	if (!m_pLegacyHalo)
		m_pLegacyHalo = F::Materials.create_from_vmt("UniboxLegacyHalo",
			"\"UnlitGeneric\""
			"\n{"
			"\n\t$basetexture \"UniboxLegacyGlow1\""
			"\n\t$additive \"1\""
			"\n}");

	if (!m_pLegacyBlurX)
		m_pLegacyBlurX = F::Materials.create_from_vmt("UniboxLegacyBlurX",
			"\"BlurFilterX\""
			"\n{"
			"\n\t$basetexture \"UniboxLegacyGlow1\""
			"\n}");

	if (!m_pLegacyBlurY)
		m_pLegacyBlurY = F::Materials.create_from_vmt("UniboxLegacyBlurY",
			"\"BlurFilterY\""
			"\n{"
			"\n\t$basetexture \"UniboxLegacyGlow2\""
			"\n}");
}

void CGlow::Unload()
{
	m_mEntities.clear();
	m_iFlags = 0;
	m_bRendering = false;
	m_bD3DPass = false;
#if defined(_WIN32) && !defined(TEXTMODE)
	const auto tContext = GetGlowContext();
	QueueRenderCall(tContext.get(), [] { s_D3DGlow.OnDeviceLost(); });
#endif
	auto fReleaseMaterial = [](IMaterial*& pMaterial)
	{
		if (pMaterial)
		{
			F::Materials.m_mMatList.erase(pMaterial);
			pMaterial->DecrementReferenceCount();
			pMaterial->DeleteIfUnreferenced();
			pMaterial = nullptr;
		}
	};
	fReleaseMaterial(m_pMatGlowColor);
	fReleaseMaterial(m_pMatGlowVisible);
	fReleaseMaterial(m_pMatBlurX);
	fReleaseMaterial(m_pMatBlurY);
	fReleaseMaterial(m_pMatHaloAddToScreen);
	fReleaseMaterial(m_pMatHaloAlpha);
	fReleaseMaterial(m_pMatBloomAdd);
	fReleaseMaterial(m_pMatBloomAlpha);
	fReleaseMaterial(m_pMatDownsample);
	fReleaseMaterial(m_pLegacyHalo);
	fReleaseMaterial(m_pLegacyBlurX);
	fReleaseMaterial(m_pLegacyBlurY);
	if (m_pLegacyGlowColor)
	{
		F::Materials.m_mMatList.erase(m_pLegacyGlowColor);
		m_pLegacyGlowColor->DecrementReferenceCount();
		m_pLegacyGlowColor = nullptr;
	}
	auto fReleaseTexture = [](ITexture*& pTexture)
	{
		if (pTexture)
		{
			pTexture->DecrementReferenceCount();
			pTexture->DeleteIfUnreferenced();
			pTexture = nullptr;
		}
	};
	fReleaseTexture(m_pRenderBuffer1);
	fReleaseTexture(m_pRenderBuffer2);
	fReleaseTexture(m_pRenderBuffer3);
	fReleaseTexture(m_pLegacyBuffer1);
	fReleaseTexture(m_pLegacyBuffer2);
}

void CGlow::OnDeviceLost()
{
#if defined(_WIN32) && !defined(TEXTMODE)
	s_D3DGlow.OnDeviceLost();
#endif
}
