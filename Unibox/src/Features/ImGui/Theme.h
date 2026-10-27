#pragma once
#include <ImGui/imgui.h>
#include <string>

struct ThemeInput_t
{
	ImVec4 m_tAccent;
	ImVec4 m_tBackground;
	ImVec4 m_tActive;
	ImVec4 m_tInactive;
	float m_flOpacity = 1.f;
};

class CMenuTheme
{
public:
	void LoadColors(const ThemeInput_t& tInput);
	void LoadFonts(ImFontAtlas* pAtlas, float flScale, const std::string& sCustomFont = {});
	void LoadStyle(float flScale);

	ImColor Accent = {};
	ImColor AccentDim = {};
	ImColor AccentSoft = {};
	ImColor OnAccent = {};
	ImColor Background0 = {};
	ImColor Sidebar = {};
	ImColor Background0p5 = {};
	ImColor Background1 = {};
	ImColor Background1p5 = {};
	ImColor Background1p5L = {};
	ImColor Background2 = {};
	ImColor Track = {};
	ImColor Popup = {};
	ImColor Danger = {};
	ImColor Inactive = {};
	ImColor Active = {};

	ImFont* FontSmall = nullptr;
	ImFont* FontRegular = nullptr;
	ImFont* FontBold = nullptr;
	ImFont* FontLarge = nullptr;
	ImFont* FontMono = nullptr;
	ImFont* IconFont = nullptr;
};
