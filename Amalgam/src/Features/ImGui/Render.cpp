#ifndef TEXTMODE
#include "Render.h"

#include <array>

#include "../../Hooks/Direct3DDevice9.h"
#include <ImGui/imgui_impl_win32.h>
#include "Fonts/MaterialDesign/MaterialIcons.h"
#include "Fonts/MaterialDesign/IconDefinitions.h"
#include "Fonts/CascadiaMono/CascadiaMono.h"
#include "Fonts/Roboto/RobotoMedium.h"
#include "Fonts/Roboto/RobotoBlack.h"
#include "Menu/Menu.h"
#include "Menu/Components.h"
#include "../CritHack/CritHack.h"
#include "../Ticks/Ticks.h"
#include "../Backtrack/Backtrack.h"
#include "../Visuals/PlayerConditions/PlayerConditions.h"
#include "../NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Visuals/SpectatorList/SpectatorList.h"
#include "../NavBot/NavBotCore.h"
#include "../Aimbot/AutoHeal/AutoHeal.h"

namespace
{
	template <size_t t_size>
	ImFont* LoadFontWithFallback(ImFontAtlas* pFontAtlas, const std::array<const char*, t_size>& vFontPaths, float flSizePixels, ImFontConfig tFontConfig)
	{
		for (const char* sFontPath : vFontPaths)
		{
			if (ImFont* pFont = pFontAtlas->AddFontFromFileTTF(sFontPath, flSizePixels, &tFontConfig))
				return pFont;
		}

		ImFontConfig tFallbackConfig = tFontConfig;
		tFallbackConfig.SizePixels = flSizePixels;
		return pFontAtlas->AddFontDefault(&tFallbackConfig);
	}
}

void CRender::Render(IDirect3DDevice9* pDevice)
{
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
		if (flNewScale != flOldScale)
			Reload();
	}

	DWORD dwOldRGB; pDevice->GetRenderState(D3DRS_SRGBWRITEENABLE, &dwOldRGB);
	pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, false);
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	F::Menu.Render();
	if (I::EngineClient->IsInGame() && !SDK::CleanScreenshot())
	{
		CTFPlayer* pLocal = H::Entities.GetLocal();
		F::CritHack.Draw(pLocal);
		F::Ticks.Draw(pLocal);
#ifdef DEBUG_VACCINATOR
		F::AutoHeal.Draw(pLocal);
#endif
		F::NoSpreadHitscan.Draw(pLocal);
		F::PlayerConditions.Draw(pLocal);
		F::Backtrack.Draw(pLocal);
		F::SpectatorList.Draw(pLocal);
		F::NavBotCore.Draw(pLocal);
	}

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, dwOldRGB);
}

void CRender::LoadColors()
{
	using namespace ImGui;

	Accent = ColorByteToFloat(Vars::Menu::Theme::Accent.Value);
	Background0 = ColorByteToFloat(Vars::Menu::Theme::Background.Value);
	const Color_t tSurface = { 72, 73, 127, Vars::Menu::Theme::Background.Value.a };
	Background0p5 = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp(tSurface, 0.16f, LerpEnum::NoAlpha));
	Background1 = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp(tSurface, 0.28f, LerpEnum::NoAlpha));
	Background1p5 = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp(tSurface, 0.4f, LerpEnum::NoAlpha));
	Background1p5L = { Background1p5.Value.x * 1.1f, Background1p5.Value.y * 1.1f, Background1p5.Value.z * 1.1f, Background1p5.Value.w };
	Background2 = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp(tSurface, 0.52f, LerpEnum::NoAlpha));
	Inactive = ColorByteToFloat(Vars::Menu::Theme::Inactive.Value);
	Active = ColorByteToFloat(Vars::Menu::Theme::Active.Value);

	ImVec4* colors = GetStyle().Colors;
	colors[ImGuiCol_Border] = Background2;
	colors[ImGuiCol_Button] = {};
	colors[ImGuiCol_ButtonHovered] = {};
	colors[ImGuiCol_ButtonActive] = {};
	colors[ImGuiCol_FrameBg] = Background1p5;
	colors[ImGuiCol_FrameBgHovered] = Background1p5L;
	colors[ImGuiCol_FrameBgActive] = Background1p5;
	colors[ImGuiCol_Header] = {};
	colors[ImGuiCol_HeaderHovered] = { Background1p5L.Value.x * 1.1f, Background1p5L.Value.y * 1.1f, Background1p5L.Value.z * 1.1f, Background1p5.Value.w }; // divd by 1.1
	colors[ImGuiCol_HeaderActive] = Background1p5;
	colors[ImGuiCol_ModalWindowDimBg] = { Background0.Value.x, Background0.Value.y, Background0.Value.z, 0.4f };
	colors[ImGuiCol_PopupBg] = Background1p5L;
	colors[ImGuiCol_ResizeGrip] = {};
	colors[ImGuiCol_ResizeGripActive] = {};
	colors[ImGuiCol_ResizeGripHovered] = {};
	colors[ImGuiCol_ScrollbarBg] = {};
	colors[ImGuiCol_Text] = Active;
	colors[ImGuiCol_WindowBg] = {};
}

void CRender::LoadFonts()
{
	using namespace ImGui;

	auto& io = GetIO();

	if (static bool bLoaded = false; !bLoaded)
		bLoaded = true;
	else
		io.Fonts->Clear();

	ImFontConfig tFontConfig;
	tFontConfig.OversampleH = 2;
	tFontConfig.Flags |= ImFontFlags_NoLoadError;
#ifndef AMALGAM_CUSTOM_FONTS
#ifdef _WIN32
	constexpr std::array<const char*, 5> vRegularFontPaths =
	{
		R"(C:\Windows\Fonts\verdana.ttf)",
		R"(C:\Windows\Fonts\segoeui.ttf)",
		R"(C:\Windows\Fonts\arial.ttf)",
		R"(C:\Windows\Fonts\tahoma.ttf)",
		R"(C:\Windows\Fonts\calibri.ttf)"
	};
	constexpr std::array<const char*, 5> vBoldFontPaths =
	{
		R"(C:\Windows\Fonts\verdanab.ttf)",
		R"(C:\Windows\Fonts\segoeuib.ttf)",
		R"(C:\Windows\Fonts\arialbd.ttf)",
		R"(C:\Windows\Fonts\tahomabd.ttf)",
		R"(C:\Windows\Fonts\calibrib.ttf)"
	};
	constexpr std::array<const char*, 5> vMonoFontPaths =
	{
		R"(C:\Windows\Fonts\consola.ttf)",
		R"(C:\Windows\Fonts\cascadiamono.ttf)",
		R"(C:\Windows\Fonts\lucon.ttf)",
		R"(C:\Windows\Fonts\cour.ttf)",
		R"(C:\Windows\Fonts\consolab.ttf)"
	};
#else
	constexpr std::array<const char*, 5> vRegularFontPaths =
	{
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
		"/usr/share/fonts/truetype/freefont/FreeSans.ttf",
		"/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf"
	};
	constexpr std::array<const char*, 5> vBoldFontPaths =
	{
		"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
		"/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
		"/usr/share/fonts/opentype/noto/NotoSans-Bold.ttf"
	};
	constexpr std::array<const char*, 5> vMonoFontPaths =
	{
		"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
		"/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
		"/usr/share/fonts/truetype/freefont/FreeMono.ttf",
		"/usr/share/fonts/opentype/noto/NotoSansMono-Regular.ttf"
	};
#endif
	FontSmall = LoadFontWithFallback(io.Fonts, vRegularFontPaths, H::Draw.Scale(11), tFontConfig);
	FontRegular = LoadFontWithFallback(io.Fonts, vRegularFontPaths, H::Draw.Scale(13), tFontConfig);
	FontBold = LoadFontWithFallback(io.Fonts, vBoldFontPaths, H::Draw.Scale(13), tFontConfig);
	FontLarge = LoadFontWithFallback(io.Fonts, vRegularFontPaths, H::Draw.Scale(14), tFontConfig);
	FontMono = LoadFontWithFallback(io.Fonts, vMonoFontPaths, H::Draw.Scale(16), tFontConfig);
#else
	FontSmall = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, H::Draw.Scale(12), &tFontConfig);
	FontRegular = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, H::Draw.Scale(13), &tFontConfig);
	FontBold = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoBlack_compressed_data, RobotoBlack_compressed_size, H::Draw.Scale(13), &tFontConfig);
	FontLarge = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, H::Draw.Scale(15), &tFontConfig);
	FontMono = io.Fonts->AddFontFromMemoryCompressedTTF(CascadiaMono_compressed_data, CascadiaMono_compressed_size, H::Draw.Scale(15), &tFontConfig);
#endif

	ImFontConfig tIconConfig;
	tIconConfig.PixelSnapH = true;
	IconFont = io.Fonts->AddFontFromMemoryCompressedTTF(MaterialIcons_compressed_data, MaterialIcons_compressed_size, H::Draw.Scale(16), &tIconConfig);

	io.Fonts->Build();
	io.FontDefault = FontRegular;
	io.ConfigDebugHighlightIdConflicts = false;
}

void CRender::LoadStyle()
{
	using namespace ImGui;

	auto& style = GetStyle();
	style.ButtonTextAlign = { 0.5f, 0.5f };
	style.CellPadding = { H::Draw.Scale(4), 0 };
	style.ChildBorderSize = 0.f;
	style.ChildRounding = H::Draw.Scale(4);
	style.FrameBorderSize = 0.f;
	style.FramePadding = { 0, 0 };
	style.FrameRounding = H::Draw.Scale(4);
	style.ItemInnerSpacing = { 0, 0 };
	style.ItemSpacing = { H::Draw.Scale(8), H::Draw.Scale(8) };
	style.PopupBorderSize = 0.f;
	style.PopupRounding = H::Draw.Scale(4);
	style.ScrollbarSize = H::Draw.Scale(4);
	style.ScrollbarRounding = 99.f;
	style.WindowBorderSize = 0.f;
	style.WindowPadding = { 0, 0 };
	style.WindowRounding = H::Draw.Scale(4);
}

void CRender::Initialize(IDirect3DDevice9* pDevice)
{
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
