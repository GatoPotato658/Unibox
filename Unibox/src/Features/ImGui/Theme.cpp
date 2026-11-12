#ifndef TEXTMODE
#include "Theme.h"

#include "Fonts/Rubik/Rubik.h"
#include "Fonts/CascadiaMono/CascadiaMono.h"
#include "Fonts/MaterialDesign/MaterialIcons.h"
#include <algorithm>
#include <windows.h>

static ImVec4 Tint(const ImVec4& tColor, float flAlpha)
{
	return { tColor.x, tColor.y, tColor.z, std::clamp(flAlpha, 0.f, 1.f) };
}

static ImVec4 Mix(const ImVec4& tFrom, const ImVec4& tTo, float flT)
{
	return { tFrom.x + (tTo.x - tFrom.x) * flT, tFrom.y + (tTo.y - tFrom.y) * flT, tFrom.z + (tTo.z - tFrom.z) * flT, tFrom.w };
}

static float Luminance(const ImVec4& tColor)
{
	return 0.299f * tColor.x + 0.587f * tColor.y + 0.114f * tColor.z;
}

void CMenuTheme::LoadColors(const ThemeInput_t& tInput)
{
	const ImVec4 tBlack = { 0.f, 0.f, 0.f, 1.f };
	const ImVec4 tAccent = tInput.m_tAccent, tActive = tInput.m_tActive;
	const float flOpacity = std::clamp(tInput.m_flOpacity, 0.f, 1.f);

	Accent = tAccent;
	AccentDim = Tint(tAccent, 0.18f);
	AccentSoft = Tint(tAccent, 0.4f);
	OnAccent = Luminance(tAccent) > 0.55f ? ImVec4(0.04f, 0.05f, 0.06f, 1.f) : ImVec4(0.98f, 0.98f, 0.99f, 1.f);
	Background1 = Tint(tInput.m_tBackground, tInput.m_tBackground.w * flOpacity);
	Background0 = Tint(tBlack, 0.42f);
	Sidebar = Tint(tBlack, 0.26f);
	Background0p5 = Tint(tBlack, 0.3f);
	Background1p5 = Tint(tActive, 0.07f);
	Background1p5L = Tint(tActive, 0.11f);
	Background2 = Tint(tActive, 0.08f);
	Track = Tint(tActive, 0.12f);
	Popup = Tint(Mix(tInput.m_tBackground, tBlack, 0.2f), 0.99f);
	Field = Tint(tActive, 0.07f);
	FieldHover = Tint(tActive, 0.12f);
	FieldText = tActive;
	Outline = Tint(tBlack, 0.55f);
	Danger = Mix({ 1.f, 0.36f, 0.38f, 1.f }, tActive, 0.12f);
	Inactive = tInput.m_tInactive;
	Active = tActive;

	ImVec4* pColors = ImGui::GetStyle().Colors;
	pColors[ImGuiCol_Border] = Background2;
	pColors[ImGuiCol_BorderShadow] = {};
	pColors[ImGuiCol_Button] = {};
	pColors[ImGuiCol_ButtonHovered] = {};
	pColors[ImGuiCol_ButtonActive] = {};
	pColors[ImGuiCol_FrameBg] = Background1p5;
	pColors[ImGuiCol_FrameBgHovered] = Background1p5L;
	pColors[ImGuiCol_FrameBgActive] = Background1p5L;
	pColors[ImGuiCol_CheckMark] = Accent;
	pColors[ImGuiCol_Header] = {};
	pColors[ImGuiCol_HeaderHovered] = Background1p5;
	pColors[ImGuiCol_HeaderActive] = Background1p5L;
	pColors[ImGuiCol_ModalWindowDimBg] = { 0.f, 0.f, 0.f, 0.45f };
	pColors[ImGuiCol_NavHighlight] = {};
	pColors[ImGuiCol_PopupBg] = Popup;
	pColors[ImGuiCol_ResizeGrip] = {};
	pColors[ImGuiCol_ResizeGripActive] = {};
	pColors[ImGuiCol_ResizeGripHovered] = {};
	pColors[ImGuiCol_ScrollbarBg] = {};
	pColors[ImGuiCol_ScrollbarGrab] = Track;
	pColors[ImGuiCol_ScrollbarGrabHovered] = Tint(tActive, 0.3f);
	pColors[ImGuiCol_ScrollbarGrabActive] = Accent;
	pColors[ImGuiCol_SliderGrab] = Accent;
	pColors[ImGuiCol_SliderGrabActive] = Accent;
	pColors[ImGuiCol_Separator] = Background2;
	pColors[ImGuiCol_SeparatorHovered] = Accent;
	pColors[ImGuiCol_SeparatorActive] = Accent;
	pColors[ImGuiCol_Text] = Active;
	pColors[ImGuiCol_TextDisabled] = Inactive;
	pColors[ImGuiCol_TextSelectedBg] = AccentSoft;
	pColors[ImGuiCol_WindowBg] = {};
}

static void MergeFallback(ImFontAtlas* pAtlas, float flSize)
{
	static const char* s_vFallbacks[] = { R"(C:\Windows\Fonts\segoeui.ttf)", R"(C:\Windows\Fonts\arial.ttf)", R"(C:\Windows\Fonts\tahoma.ttf)" };
	for (const char* sPath : s_vFallbacks)
	{
		if (GetFileAttributesA(sPath) == INVALID_FILE_ATTRIBUTES)
			continue;

		ImFontConfig tConfig;
		tConfig.MergeMode = true;
		tConfig.OversampleH = 2;
		tConfig.Flags |= ImFontFlags_NoLoadError;
		pAtlas->AddFontFromFileTTF(sPath, flSize, &tConfig);
		return;
	}
}

static ImFont* AddRubik(ImFontAtlas* pAtlas, const unsigned int* pData, unsigned int uSize, float flSize)
{
	ImFontConfig tConfig;
	tConfig.OversampleH = 2;
	tConfig.FontDataOwnedByAtlas = false;
	ImFont* pFont = pAtlas->AddFontFromMemoryTTF(const_cast<unsigned int*>(pData), int(uSize), flSize, &tConfig);
	MergeFallback(pAtlas, flSize);
	return pFont;
}

static ImFont* AddCustom(ImFontAtlas* pAtlas, const std::string& sPath, float flSize)
{
	const DWORD dwAttributes = GetFileAttributesA(sPath.c_str());
	if (dwAttributes == INVALID_FILE_ATTRIBUTES || dwAttributes & FILE_ATTRIBUTE_DIRECTORY)
		return nullptr;

	ImFontConfig tConfig;
	tConfig.OversampleH = 2;
	tConfig.Flags |= ImFontFlags_NoLoadError;
	ImFont* pFont = pAtlas->AddFontFromFileTTF(sPath.c_str(), flSize, &tConfig);
	if (pFont)
		MergeFallback(pAtlas, flSize);
	return pFont;
}

static ImFont* AddMenuFont(ImFontAtlas* pAtlas, const std::string& sCustomFont, const unsigned int* pData, unsigned int uSize, float flSize)
{
	if (!sCustomFont.empty())
	{
		if (ImFont* pFont = AddCustom(pAtlas, sCustomFont, flSize))
			return pFont;
	}
	return AddRubik(pAtlas, pData, uSize, flSize);
}

void CMenuTheme::LoadFonts(ImFontAtlas* pAtlas, float flScale, const std::string& sCustomFont)
{
	FontSmall = AddMenuFont(pAtlas, sCustomFont, Rubik_Regular_data, Rubik_Regular_size, 11.5f * flScale);
	FontRegular = AddMenuFont(pAtlas, sCustomFont, Rubik_Medium_data, Rubik_Medium_size, 13.f * flScale);
	FontBold = AddMenuFont(pAtlas, sCustomFont, Rubik_SemiBold_data, Rubik_SemiBold_size, 13.5f * flScale);
	FontLarge = AddMenuFont(pAtlas, sCustomFont, Rubik_SemiBold_data, Rubik_SemiBold_size, 16.f * flScale);

	ImFontConfig tMonoConfig;
	tMonoConfig.OversampleH = 2;
	FontMono = pAtlas->AddFontFromMemoryCompressedTTF(CascadiaMono_compressed_data, CascadiaMono_compressed_size, 15.f * flScale, &tMonoConfig);

	ImFontConfig tIconConfig;
	tIconConfig.PixelSnapH = true;
	IconFont = pAtlas->AddFontFromMemoryCompressedTTF(MaterialIcons_compressed_data, MaterialIcons_compressed_size, 16.f * flScale, &tIconConfig);

	pAtlas->Build();
	ImGui::GetIO().FontDefault = FontRegular;
}

void CMenuTheme::LoadStyle(float flScale)
{
	auto& tStyle = ImGui::GetStyle();
	tStyle.ButtonTextAlign = { 0.5f, 0.5f };
	tStyle.CellPadding = { 6.f * flScale, 0.f };
	tStyle.ChildBorderSize = 0.f;
	tStyle.ChildRounding = 4.f * flScale;
	tStyle.FrameBorderSize = 0.f;
	tStyle.FramePadding = { 0.f, 0.f };
	tStyle.FrameRounding = 4.f * flScale;
	tStyle.GrabRounding = 2.f * flScale;
	tStyle.ItemInnerSpacing = { 0.f, 0.f };
	tStyle.ItemSpacing = { 8.f * flScale, 8.f * flScale };
	tStyle.PopupBorderSize = 0.f;
	tStyle.PopupRounding = 4.f * flScale;
	tStyle.ScrollbarSize = 4.f * flScale;
	tStyle.ScrollbarRounding = 2.f * flScale;
	tStyle.WindowBorderSize = 0.f;
	tStyle.WindowPadding = { 0.f, 0.f };
	tStyle.WindowRounding = 5.f * flScale;
}
#endif
