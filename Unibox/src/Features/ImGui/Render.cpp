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
#include "../Visuals/Visuals.h"
#include "../Aimbot/Aimbot.h"
#include "../Misc/Radio/Radio.h"
#include "Overlay/CaptureOverlay.h"

void CRender::Render(IDirect3DDevice9* pDevice)
{
	if (m_pDevice && pDevice != m_pDevice && m_bLoaded)
	{
		F::CaptureOverlay.ReleaseDeviceObjects();
		ImGui_ImplDX9_Shutdown();
		ImGui_ImplDX9_Init(pDevice);
	}
	m_pDevice = pDevice;

	static std::once_flag tFlag; std::call_once(tFlag, [&]
	{
		Initialize(pDevice);
	});

	LoadColors();
	{
		static float flStaticScale = Vars::Menu::Scale.Value;
		float flOldScale = flStaticScale;
		float flNewScale = flStaticScale = Vars::Menu::Scale.Value;
		static std::string sStaticFont = Vars::Menu::CustomFont.Value;
		const bool bFontChanged = sStaticFont != Vars::Menu::CustomFont.Value;
		sStaticFont = Vars::Menu::CustomFont.Value;
		if (flNewScale != flOldScale || bFontChanged)
			Reload();
	}

	DWORD dwOldRGB; pDevice->GetRenderState(D3DRS_SRGBWRITEENABLE, &dwOldRGB);
	pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, false);
	DWORD dwOldSRGBRead = TRUE;
	pDevice->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &dwOldSRGBRead);
	pDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	F::Radio.OnFrame();

	const bool bAntiObs = SDK::AntiObs();
	const bool bPrivate = bAntiObs && F::CaptureOverlay.Prepare(pDevice);
	const bool bUnsupported = bAntiObs && !bPrivate && F::CaptureOverlay.Unsupported();
	const bool bDrawGame = !bAntiObs || bUnsupported;
	const bool bTransient = bAntiObs && !bPrivate && !bUnsupported;

	F::Menu.m_bInputReady = !bAntiObs || bPrivate || bUnsupported;

	F::Menu.Render();

	if (I::EngineClient->IsInGame() && !bTransient && (bPrivate || bDrawGame && !SDK::TakingScreenshot()))
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

		ImGui::PopFont();
	}
	if (!bPrivate)
		F::CaptureOverlay.Hide();

	ImGui::CloseStalePopups();
	ImGui::EndFrame();
	ImGui::Render();

	if (bPrivate)
		F::CaptureOverlay.Present(pDevice, ImGui::GetDrawData());
	else if (bDrawGame)
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, dwOldRGB);
	pDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, dwOldSRGBRead);
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
	if (static bool bLoaded = false; !bLoaded)
		bLoaded = true;
	else
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
	//io.IniFilename = nullptr;
	io.LogFilename = nullptr;

	LoadFonts();
	LoadStyle();
	m_bLoaded = true;
}

void CRender::Reload()
{
	m_bLoaded = false;

	LoadFonts();
	LoadStyle();

	m_bLoaded = true;
}
#endif
