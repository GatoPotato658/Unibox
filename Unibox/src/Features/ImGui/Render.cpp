#ifndef TEXTMODE
#include "Render.h"

#include "../../Hooks/Direct3DDevice9.h"
#include <ImGui/imgui_impl_win32.h>
#include "Fonts/MaterialDesign/IconDefinitions.h"
#include "Menu/Menu.h"
#include "Menu/Components.h"
#include "../CritHack/CritHack.h"
#include "../Ticks/Ticks.h"
#include "../Backtrack/Backtrack.h"
#include "../Visuals/PlayerConditions/PlayerConditions.h"
#include "../NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Visuals/SpectatorList/SpectatorList.h"
#include "../NavBot/NavRender.h"
#include "../Aimbot/AutoHeal/AutoHeal.h"
#include "../Visuals/ESP/ESP.h"
#include "../Visuals/Radar/Radar.h"
#include "../Visuals/Visuals.h"
#include "../Visuals/Hitmarker/Hitmarker.h"
#include "../Aimbot/Aimbot.h"
#include "../Misc/Radio/Radio.h"
#include "Overlay/CaptureOverlay.h"

void CRender::Render(IDirect3DDevice9* pDevice)
{
	std::lock_guard tLock(m_tFrameMutex);
	const auto eStage = m_eStartupStage.load();
	if (G::Unload || eStage == StartupStage::Config)
		return;

	if (m_pDevice && pDevice != m_pDevice && m_bLoaded)
	{
		InvalidateDeviceObjects(true);
		F::CaptureOverlay.ReleaseDeviceObjects();
		ImGui_ImplDX9_Shutdown();
		ImGui_ImplDX9_Init(pDevice);
	}
	m_pDevice = pDevice;

	if (!m_bLoaded)
		Initialize(pDevice);

	LoadColors();
	const char* sStartupStatus = nullptr;
	std::string sIconProgress;
	if (eStage == StartupStage::Features)
		sStartupStatus = "Starting features...";
	else if (!m_bStartupFrame)
		sStartupStatus = "Preparing menu fonts...";
	else if (!FontRegular)
	{
		LoadFonts();
		LoadStyle();
		PrecacheESPIcons();
		sStartupStatus = "Preparing backpack catalogue...";
	}
	else
	{
		static float flScale = Vars::Menu::Scale.Value;
		static std::string sFont = Vars::Menu::CustomFont.Value;
		if (flScale != Vars::Menu::Scale.Value || sFont != Vars::Menu::CustomFont.Value)
		{
			flScale = Vars::Menu::Scale.Value;
			sFont = Vars::Menu::CustomFont.Value;
			Reload();
		}

		const auto [uPrepared, uTotal] = F::Menu.PrecacheAssets();
		if (!m_bStartupComplete)
		{
			using Clock = std::chrono::steady_clock;
			if (m_tCatalogueWait == Clock::time_point{})
				m_tCatalogueWait = Clock::now();

			if (uTotal && uPrepared < uTotal)
			{
				sIconProgress = std::format("Caching backpack icons... {}/{}", uPrepared, uTotal);
				sStartupStatus = sIconProgress.c_str();
			}
			else if (!uTotal && Clock::now() - m_tCatalogueWait < std::chrono::seconds(3))
				sStartupStatus = "Waiting for backpack catalogue...";
			else
			{
				m_bStartupComplete = true;
				for (auto& pBlur : m_aStartupBlur)
					pBlur.Reset();
				m_pStartupResolve.Reset();
			}
		}
	}

	DWORD dwOldRGB = FALSE, dwOldSRGBRead = FALSE;
	const bool bRestoreRGB = SUCCEEDED(pDevice->GetRenderState(D3DRS_SRGBWRITEENABLE, &dwOldRGB));
	const bool bRestoreSRGBRead = SUCCEEDED(pDevice->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &dwOldSRGBRead));
	if (bRestoreRGB)
		pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	if (bRestoreSRGBRead)
		pDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	const bool bAntiObs = SDK::AntiObs();
	const bool bPrivate = bAntiObs && F::CaptureOverlay.Prepare(pDevice);
	const bool bUnsupported = bAntiObs && !bPrivate && F::CaptureOverlay.Unsupported();
	const bool bDrawGame = !bAntiObs || bUnsupported;
	const bool bTransient = bAntiObs && !bPrivate && !bUnsupported;

	const bool bDrawVisuals = !bTransient && (bPrivate || bDrawGame && !SDK::TakingScreenshot());
	F::Menu.m_bInputReady = !sStartupStatus && !bTransient;
	if (sStartupStatus)
	{
		if (bDrawVisuals)
			DrawStartup(sStartupStatus);
		m_bStartupFrame = true;
	}
	else
		DrawContent(bDrawVisuals);

	if (!bPrivate)
		F::CaptureOverlay.Hide();

	ImGui::CloseStalePopups();
	ImGui::EndFrame();
	ImGui::Render();

	if (bPrivate)
		F::CaptureOverlay.Present(pDevice, ImGui::GetDrawData());
	else if (bDrawGame)
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	if (bRestoreRGB)
		pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, dwOldRGB);
	if (bRestoreSRGBRead)
		pDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, dwOldSRGBRead);
}

void CRender::DrawStartup(const char* sStatus)
{
	const ImVec2 vSize = ImGui::GetIO().DisplaySize;
	if (vSize.x <= 0.f || vSize.y <= 0.f)
		return;

	auto* pDrawList = ImGui::GetBackgroundDrawList();
	Microsoft::WRL::ComPtr<IDirect3DSurface9> pSource;
	D3DSURFACE_DESC tSource{};
	if (SUCCEEDED(m_pDevice->GetRenderTarget(0, &pSource)) && SUCCEEDED(pSource->GetDesc(&tSource)))
	{
		if (m_tStartupSource.Width != tSource.Width || m_tStartupSource.Height != tSource.Height
			|| m_tStartupSource.Format != tSource.Format || m_tStartupSource.MultiSampleType != tSource.MultiSampleType)
		{
			for (auto& pBlur : m_aStartupBlur)
				pBlur.Reset();
			m_pStartupResolve.Reset();
			m_tStartupSource = tSource;
			for (int i = 0; i < BLUR_LEVELS; i++)
			{
				const UINT uWidth = std::max(1u, tSource.Width >> (i + 1)), uHeight = std::max(1u, tSource.Height >> (i + 1));
				if (FAILED(m_pDevice->CreateTexture(uWidth, uHeight, 1, D3DUSAGE_RENDERTARGET, tSource.Format,
					D3DPOOL_DEFAULT, &m_aStartupBlur[i], nullptr)))
					m_aStartupBlur[i].Reset();
			}
			if (m_aStartupBlur[0] && tSource.MultiSampleType != D3DMULTISAMPLE_NONE)
				m_pDevice->CreateRenderTarget(tSource.Width, tSource.Height, tSource.Format, D3DMULTISAMPLE_NONE,
					0, FALSE, &m_pStartupResolve, nullptr);
		}

		if (m_pStartupResolve && SUCCEEDED(m_pDevice->StretchRect(pSource.Get(), nullptr,
			m_pStartupResolve.Get(), nullptr, D3DTEXF_NONE)))
			pSource = m_pStartupResolve;

		IDirect3DTexture9* pBlurred = nullptr;
		for (int i = 0; i < BLUR_LEVELS && m_aStartupBlur[i]; i++)
		{
			Microsoft::WRL::ComPtr<IDirect3DSurface9> pTarget;
			if (FAILED(m_aStartupBlur[i]->GetSurfaceLevel(0, &pTarget))
				|| FAILED(m_pDevice->StretchRect(pSource.Get(), nullptr, pTarget.Get(), nullptr, D3DTEXF_LINEAR)))
				break;
			pBlurred = m_aStartupBlur[i].Get();
			pSource = pTarget;
		}
		if (pBlurred)
		{
			pDrawList->AddCallback([](const ImDrawList*, const ImDrawCmd*)
			{
				F::Render.GetDevice()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
			}, nullptr);
			pDrawList->AddImage(reinterpret_cast<ImTextureID>(pBlurred), { 0.f, 0.f }, vSize);
			pDrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		}
	}

	pDrawList->AddRectFilled({ 0.f, 0.f }, vSize, IM_COL32(8, 9, 11, 160));
	const ImVec2 vCenter = { vSize.x * 0.5f, vSize.y * 0.5f };
	const float flScale = H::Draw.Scale();
	const float flRadius = 9.f * flScale;
	const float flStroke = std::max(1.f, 1.5f * flScale);
	const float flAngle = float(std::fmod(ImGui::GetTime() * 5.0, 2.0 * IM_PI));
	pDrawList->AddCircle(vCenter, flRadius, IM_COL32(255, 255, 255, 55), 32, flStroke);
	pDrawList->PathArcTo(vCenter, flRadius, flAngle, flAngle + IM_PI * 0.85f, 16);
	pDrawList->PathStroke(Accent, 0, flStroke);

	ImFont* pFont = FontSmall ? FontSmall : ImGui::GetFont();
	const float flSize = FontSmall ? FontSmall->LegacySize : 12.f * flScale;
	const ImVec2 vText = pFont->CalcTextSizeA(flSize, FLT_MAX, 0.f, sStatus);
	pDrawList->AddText(pFont, flSize, { vCenter.x - vText.x * 0.5f, vCenter.y + 20.f * flScale },
		IM_COL32(240, 240, 240, 255), sStatus);
}

void CRender::DrawContent(bool bDrawVisuals)
{
	F::Radio.OnFrame();
	F::Menu.Render();

	const bool bInGame = I::EngineClient->IsInGame();
	if (bDrawVisuals)
		F::Radar.Draw(bInGame);

	if (bInGame && bDrawVisuals)
	{
		ImDrawList* pBackground = ImGui::GetBackgroundDrawList();

		F::SpectatorList.Draw();

		ImGui::PushFont(F::Render.FontRegular, F::Render.FontRegular->LegacySize / Vars::Menu::Scale.Value * Vars::Menu::IndicatorScale.Value);

		F::CritHack.Draw();
		F::Ticks.Draw();
#ifdef DEBUG_VACCINATOR
		F::AutoHeal.Draw(H::Entities.GetLocal());
#endif
		F::NoSpreadHitscan.Draw();
		F::PlayerConditions.Draw();
		F::Backtrack.Draw();
		F::NavRender.DrawIndicators();
		F::Radio.Draw();

		F::ESP.Draw(pBackground);
		F::Aimbot.Draw(pBackground);
		F::Visuals.DrawBestAimPos(pBackground);
		F::Hitmarker.Draw(pBackground);

		ImGui::PopFont();
	}
}

void CRender::LoadColors()
{
	const Color_t tAccent = Vars::Menu::Theme::Accent.Value;
	const Color_t tBackground = Vars::Menu::Theme::Background.Value;
	const Color_t tActive = Vars::Menu::Theme::Active.Value;
	const Color_t tInactive = Vars::Menu::Theme::Inactive.Value;
	const int iOpacity = Vars::Menu::Theme::Opacity.Value;
	static Color_t s_tLastAccent = {};
	static Color_t s_tLastBackground = {};
	static Color_t s_tLastActive = {};
	static Color_t s_tLastInactive = {};
	static int s_iLastOpacity = 0;
	static bool s_bLoaded = false;
	if (s_bLoaded && tAccent == s_tLastAccent && tBackground == s_tLastBackground &&
		tActive == s_tLastActive && tInactive == s_tLastInactive && iOpacity == s_iLastOpacity)
		return;

	s_bLoaded = true;
	s_tLastAccent = tAccent;
	s_tLastBackground = tBackground;
	s_tLastActive = tActive;
	s_tLastInactive = tInactive;
	s_iLastOpacity = iOpacity;
	CMenuTheme::LoadColors({
		ImGui::ColorByteToVec(tAccent),
		ImGui::ColorByteToVec(tBackground),
		ImGui::ColorByteToVec(tActive),
		ImGui::ColorByteToVec(tInactive),
		iOpacity / 100.f
	});
}

void CRender::LoadFonts()
{
	SDK::CInitTimingScope tFonts("ImGui.LoadFonts");

	auto& io = ImGui::GetIO();
	io.FontDefault = nullptr;
	io.Fonts->Clear();

	CMenuTheme::LoadFonts(io.Fonts, H::Draw.Scale(), Vars::Menu::CustomFont.Value);
	io.ConfigDebugHighlightIdConflicts = false;
}

void CRender::LoadStyle()
{
	CMenuTheme::LoadStyle(H::Draw.Scale());
}

void CRender::Initialize(IDirect3DDevice9* pDevice)
{
	SDK::CInitTimingScope tInit("ImGui.Initialize");

	ImGui::CreateContext();
	ImGui_ImplWin32_Init(WndProc::hwWindow);
	ImGui_ImplDX9_Init(pDevice);

	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;

	m_bLoaded = true;
}

void CRender::InvalidateDeviceObjects(bool bDeviceChanged)
{
	for (auto& pBlur : m_aStartupBlur)
		pBlur.Reset();
	m_pStartupResolve.Reset();
	m_tStartupSource = {};
	F::Menu.InvalidateItemIcons(bDeviceChanged);
}

void CRender::Unload()
{
	std::lock_guard tLock(m_tFrameMutex);
	if (!m_bLoaded)
		return;

	m_bLoaded = false;
	InvalidateDeviceObjects(true);
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	m_pDevice = nullptr;
}

void CRender::Reload()
{
	m_bLoaded = false;

	LoadFonts();
	LoadStyle();

	m_bLoaded = true;
}
#endif
