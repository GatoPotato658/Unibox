#include "CameraWindow.h"

#include "../../Visuals/Materials/Materials.h"

using CViewRenderRenderViewFn = void(__fastcall*)(void*, const CViewSetup&, ClearFlags_t, RenderViewInfo_t);

void CCameraWindow::DrawChannel(IMaterial* pMaterial, ITexture* pTexture, WindowBox_t& tWindowBox)
{
	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	pRenderContext->DrawScreenSpaceRectangle(
		pMaterial,
		tWindowBox.x - tWindowBox.w / 2, tWindowBox.y, tWindowBox.w, tWindowBox.h,
		0, 0, tWindowBox.w, tWindowBox.h,
		pTexture->GetActualWidth(), pTexture->GetActualHeight(),
		nullptr, 1, 1
	);
	pRenderContext->Release();
}

// Draws cameras to the screen
void CCameraWindow::Draw()
{
	if (!I::EngineClient->IsInGame())
		return;

	if (m_pCameraMaterial && m_bShouldDraw)
		DrawChannel(m_pCameraMaterial, m_pCameraTexture, Vars::Visuals::Simulation::ProjectileWindow.Value);

	if (m_pSpyMaterial && m_bShouldDrawSpy)
		DrawChannel(m_pSpyMaterial, m_pSpyTexture, Vars::Visuals::Simulation::SpyWindow.Value);
}

// Renders another view onto a texture
void CCameraWindow::RenderView(void* rcx, const CViewSetup& pViewSetup)
{
	m_bDrawing = true;

	if (m_bShouldDraw && m_pCameraTexture)
		RenderChannel(rcx, pViewSetup, m_pCameraTexture, Vars::Visuals::Simulation::ProjectileWindow.Value, m_vCameraOrigin, m_vCameraAngles);

	if (m_bShouldDrawSpy && m_pSpyTexture)
		RenderChannel(rcx, pViewSetup, m_pSpyTexture, Vars::Visuals::Simulation::SpyWindow.Value, m_vSpyOrigin, m_vSpyAngles, m_pSpyTarget);

	m_bDrawing = false;
}

void CCameraWindow::RenderChannel(void* rcx, const CViewSetup& pViewSetup, ITexture* pTexture, WindowBox_t& tWindowBox, Vec3 vOrigin, Vec3 vAngles, CBaseEntity* pHide)
{
	CViewSetup tViewSetup = pViewSetup;
	tViewSetup.x = 0;
	tViewSetup.y = 0;

	tViewSetup.origin = vOrigin;
	tViewSetup.angles = vAngles;

	tViewSetup.width = tWindowBox.w + 1;
	tViewSetup.height = tWindowBox.h + 1;
	tViewSetup.m_flAspectRatio = float(tViewSetup.width) / float(tViewSetup.height);
	tViewSetup.fov = 90;

	std::vector<CBaseEntity*> vHidden = {};
	if (pHide && !(pHide->m_fEffects() & EF_NODRAW))
	{
		pHide->m_fEffects() |= EF_NODRAW;
		vHidden.push_back(pHide);

		auto& vWearables = pHide->As<CBasePlayer>()->m_hMyWearables();
		for (int i = 0; i < vWearables.Count(); i++)
		{
			auto pWearableBase = vWearables[i].Get();
			if (pWearableBase)
			{
				auto pWearable = pWearableBase->As<CBaseEntity>();
				if (pWearable && !(pWearable->m_fEffects() & EF_NODRAW))
				{
					pWearable->m_fEffects() |= EF_NODRAW;
					vHidden.push_back(pWearable);
				}
			}
		}
		for (auto& hWeapon : pHide->As<CBaseCombatCharacter>()->m_hMyWeapons())
		{
			auto pWeaponBase = hWeapon.Get();
			if (pWeaponBase)
			{
				auto pWeapon = pWeaponBase->As<CBaseEntity>();
				if (pWeapon && !(pWeapon->m_fEffects() & EF_NODRAW))
				{
					pWeapon->m_fEffects() |= EF_NODRAW;
					vHidden.push_back(pWeapon);
				}
			}
		}
	}

	RenderCustomView(rcx, tViewSetup, pTexture);

	for (auto pEntity : vHidden)
		pEntity->m_fEffects() &= ~EF_NODRAW;
}

void CCameraWindow::RenderCustomView(void* rcx, const CViewSetup& pViewSetup, ITexture* pTexture)
{
	auto pRenderContext = I::MaterialSystem->GetRenderContext();

	pRenderContext->PushRenderTargetAndViewport();
	pRenderContext->SetRenderTarget(pTexture);

	static auto CViewRender_RenderView = U::Hooks.Get("CViewRender_RenderView");
	CViewRender_RenderView->As<CViewRenderRenderViewFn>()(
		rcx, pViewSetup, static_cast<ClearFlags_t>(VIEW_CLEAR_COLOR | VIEW_CLEAR_DEPTH), RENDERVIEW_UNSPECIFIED);

	pRenderContext->PopRenderTargetAndViewport();
	pRenderContext->Release();
}

void CCameraWindow::Initialize()
{
	SDK::CInitTimingScope tInit("CameraWindow.Initialize");

	if (!m_pCameraMaterial)
	{
		m_pCameraMaterial = F::Materials.create_from_vmt("CameraMaterial",
			"\"UnlitGeneric\""
			"\n{"
			"\n\t$basetexture \"m_pCameraTexture\""
			"\n}");
	}

	if (!m_pSpyMaterial)
	{
		m_pSpyMaterial = F::Materials.create_from_vmt("SpyCameraMaterial",
			"\"UnlitGeneric\""
			"\n{"
			"\n\t$basetexture \"m_pSpyTexture\""
			"\n}");
	}

	if (SDK::SupportsDX9Shaders())
	{
		if (!m_pCameraTexture)
		{
			m_pCameraTexture = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
				"m_pCameraTexture",
				1,
				1,
				RT_SIZE_FULL_FRAME_BUFFER,
				IMAGE_FORMAT_RGB888,
				MATERIAL_RT_DEPTH_SHARED,
				TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
				CREATERENDERTARGETFLAGS_HDR
			);
		}

		if (!m_pSpyTexture)
		{
			m_pSpyTexture = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
				"m_pSpyTexture",
				1,
				1,
				RT_SIZE_FULL_FRAME_BUFFER,
				IMAGE_FORMAT_RGB888,
				MATERIAL_RT_DEPTH_SHARED,
				TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
				CREATERENDERTARGETFLAGS_HDR
			);
		}
	}
}

void CCameraWindow::Unload()
{
	if (m_pCameraMaterial)
	{
		m_pCameraMaterial->DecrementReferenceCount();
		m_pCameraMaterial->DeleteIfUnreferenced();
		m_pCameraMaterial = nullptr;
	}

	if (m_pCameraTexture)
	{
		m_pCameraTexture->DecrementReferenceCount();
		m_pCameraTexture->DeleteIfUnreferenced();
		m_pCameraTexture = nullptr;
	}

	if (m_pSpyMaterial)
	{
		m_pSpyMaterial->DecrementReferenceCount();
		m_pSpyMaterial->DeleteIfUnreferenced();
		m_pSpyMaterial = nullptr;
	}

	if (m_pSpyTexture)
	{
		m_pSpyTexture->DecrementReferenceCount();
		m_pSpyTexture->DeleteIfUnreferenced();
		m_pSpyTexture = nullptr;
	}
}
