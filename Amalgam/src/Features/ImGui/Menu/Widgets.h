#pragma once
#include "WidgetEnv.h"
#include "../Fonts/MaterialDesign/IconDefinitions.h"
#include <ImGui/imgui_internal.h>
#include <ImGui/imgui_stdlib.h>
#include <charconv>
#include <deque>
#include <functional>
#include <numeric>
#include <tuple>

Enum(FText, None = 0, Middle = 1 << 0, Right = 1 << 1, SameLine = 1 << 2);
Enum(FButton, None = 0, Left = 1 << 0, Right = 1 << 1, Fit = 1 << 2, SameLine = 1 << 3, Primary = 1 << 4, Danger = 1 << 5);
Enum(FKeybind, None = 0, AllowNone = 1 << 5);
Enum(FToggle, None = 0, Left = 1 << 0, Right = 1 << 1, PlainColor = 1 << 2);
Enum(FSlider, None = 0, Left = 1 << 0, Right = 1 << 1, Clamp = 1 << 2, Min = 1 << 3, Max = 1 << 4, Precision = 1 << 5, NoAutoUpdate = 1 << 6);
Enum(FDropdown, None = 0, Left = 1 << 0, Right = 1 << 1, Multi = 1 << 2, Modifiable = 1 << 3, NoSanitization = 1 << 4);
Enum(FSDropdown, None = 0, Custom = 1 << 2, AutoUpdate = 1 << 3);
Enum(FMenuItem, None = 0, Checked = 1 << 0, TintLabel = 1 << 1);
Enum(FColorPicker, None = 0, Left = 1 << 0, Right = 1 << 1, Full = 1 << 2, SameLine = 1 << 3, Tooltip = 1 << 4, NoTooltip = 1 << 5, RetainPosition = 1 << 6, HoverContents = 1 << 7, RemoveVisuals = 1 << 8);

Enum(Widget, Invalid, FToggle, FSlider, FISlider = FSlider, FFSlider, FIRSlider, FFRSlider, FDropdown, FSDropdown, FMDropdown, FColorPicker, FGColorPicker, FKeybind);

static inline bool    operator==(const ImVec2& lhs, const ImVec2& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }
static inline bool    operator!=(const ImVec2& lhs, const ImVec2& rhs) { return lhs.x != rhs.x || lhs.y != rhs.y; }
static inline ImVec2  operator*(const ImVec2& lhs, const float rhs) { return ImVec2(lhs.x * rhs, lhs.y * rhs); }
static inline ImVec2  operator/(const ImVec2& lhs, const float rhs) { return ImVec2(lhs.x / rhs, lhs.y / rhs); }
static inline ImVec2  operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y); }
static inline ImVec2  operator-(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y); }
static inline ImVec2  operator*(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x * rhs.x, lhs.y * rhs.y); }
static inline ImVec2  operator/(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x / rhs.x, lhs.y / rhs.y); }
static inline ImVec2  operator-(const ImVec2& lhs) { return ImVec2(-lhs.x, -lhs.y); }
static inline ImVec2& operator*=(ImVec2& lhs, const float rhs) { lhs.x *= rhs; lhs.y *= rhs; return lhs; }
static inline ImVec2& operator/=(ImVec2& lhs, const float rhs) { lhs.x /= rhs; lhs.y /= rhs; return lhs; }
static inline ImVec2& operator+=(ImVec2& lhs, const ImVec2& rhs) { lhs.x += rhs.x; lhs.y += rhs.y; return lhs; }
static inline ImVec2& operator-=(ImVec2& lhs, const ImVec2& rhs) { lhs.x -= rhs.x; lhs.y -= rhs.y; return lhs; }
static inline ImVec2& operator*=(ImVec2& lhs, const ImVec2& rhs) { lhs.x *= rhs.x; lhs.y *= rhs.y; return lhs; }
static inline ImVec2& operator/=(ImVec2& lhs, const ImVec2& rhs) { lhs.x /= rhs.x; lhs.y /= rhs.y; return lhs; }

namespace ImGui
{
	inline std::unordered_map<uint32_t, int> ActiveMap = {};

	inline bool Disabled = false, Transparent = false;
	inline int CurrentBind = DEFAULT_BIND;
	inline bool Hovered = false;

	inline std::vector<bool> DisabledVec = {}, TransparentVec = {};
	inline void PushDisabled(bool bDisabled)
	{
		DisabledVec.push_back(Disabled = bDisabled);
	}
	inline void PopDisabled(int count = 1)
	{
		int iSize = int(DisabledVec.size());
		if (iSize < count)
		{
			IM_ASSERT_USER_ERROR(0, "Calling PopDisabled() too many times: stack underflow.");
			count = iSize;
		}
		for (int i = 0; i < count; i++)
			DisabledVec.pop_back();
		Disabled = !DisabledVec.empty() ? DisabledVec.back() : false;
	}
	inline void PushTransparent(bool bTransparent, bool bPushAlpha = false)
	{
		TransparentVec.push_back(Transparent = bTransparent);

		if (bPushAlpha)
			PushStyleVar(ImGuiStyleVar_Alpha, !bTransparent ? 1.f : 0.5f);
	}
	inline void PopTransparent(int count = 1, int iPopAlpha = false)
	{
		int iSize = int(TransparentVec.size());
		if (iSize < count)
		{
			IM_ASSERT_USER_ERROR(0, "Calling PopTransparent() too many times: stack underflow.");
			count = iSize;
		}
		for (int i = 0; i < count; i++)
			TransparentVec.pop_back();
		Transparent = !TransparentVec.empty() ? TransparentVec.back() : false;

		if (iPopAlpha)
			PopStyleVar(iPopAlpha);
	}

	inline ImVec2 GetDrawPos()
	{
		return GetWindowPos() - GetCurrentWindow()->Scroll;
	}
	struct Row_t
	{
		ImVec2 m_vPos;
		ImVec2 m_vSize;
	};
	inline std::vector<Row_t> vRowSizesRegular = {}, vRowSizesPopup = {};
	inline auto pRowSizes = &vRowSizesRegular;
	inline void AddRowSize(ImVec2 vPos, ImVec2 vSize)
	{
		pRowSizes = GetCurrentWindow()->Flags & ImGuiWindowFlags_Popup ? &vRowSizesPopup : &vRowSizesRegular;

		vPos += GetDrawPos();
		if (!pRowSizes->empty() && pRowSizes->back().m_vPos.y != vPos.y)
			pRowSizes->clear();
		pRowSizes->emplace_back(vPos, vSize);
	}
	inline float GetRowPos(bool bDrawPos = false)
	{
		if (!pRowSizes->empty())
			return pRowSizes->front().m_vPos.y - (!bDrawPos ? GetDrawPos().y : 0.f);
		else
			return GetCursorPosY() + (bDrawPos ? GetDrawPos().y : 0.f);
	}
	inline float GetRowSize(float flDefault = 0.f)
	{
		pRowSizes = GetCurrentWindow()->Flags & ImGuiWindowFlags_Popup ? &vRowSizesPopup : &vRowSizesRegular;

		float flMax = 0.f;
		for (auto& [_, vSize] : *pRowSizes)
			flMax = std::max(flMax, vSize.y);
		return flMax;
	}

	inline ImU32 ColorByteToInt(Color_t tColor)
	{
		return tColor.r << IM_COL32_R_SHIFT | tColor.g << IM_COL32_G_SHIFT | tColor.b << IM_COL32_B_SHIFT | tColor.a << IM_COL32_A_SHIFT;
	}

	inline Color_t ColorIntToByte(ImU32 tColor)
	{
		return { byte(tColor >> IM_COL32_R_SHIFT), byte(tColor >> IM_COL32_G_SHIFT), byte(tColor >> IM_COL32_B_SHIFT), byte(tColor >> IM_COL32_A_SHIFT) };
	}

	inline ImVec4 ColorByteToVec(Color_t tColor)
	{
		return { tColor.r / 255.f, tColor.g / 255.f, tColor.b / 255.f, tColor.a / 255.f };
	}

	inline Color_t ColorVecToByte(ImVec4 tColor)
	{
		return { byte(tColor.x * 255 + 0.5f), byte(tColor.y * 255 + 0.5f), byte(tColor.z * 255 + 0.5f), byte(tColor.w * 255 + 0.5f) };
	}

	inline ImColor ColorByteToFloat(Color_t tColor)
	{
		return { tColor.r / 255.f, tColor.g / 255.f, tColor.b / 255.f, tColor.a / 255.f };
	}

	inline Color_t ColorFloatToByte(ImColor tColor)
	{
		return { byte(tColor.Value.x * 255), byte(tColor.Value.y * 255), byte(tColor.Value.z * 255), byte(tColor.Value.w * 255) };
	}

	inline void DebugDummy(ImVec2 vSize)
	{
		Dummy(vSize);
	}
	inline bool IsMouseWithin(float x, float y, float w, float h)
	{
		ImVec2 vMouse = GetMousePos();
		return x <= vMouse.x && vMouse.x < x + w
			&& y <= vMouse.y && vMouse.y < y + h;
	}

	inline std::string StripDoubleHash(const char* sText)
	{
		std::string sBegin = sText, sEnd = FindRenderedTextEnd(sText);
		return sBegin.replace(sBegin.end() - sEnd.size(), sBegin.end(), "");
	}

	inline float Anim(ImGuiID uID, int iSalt, float flTarget, float flSpeed = 14.f)
	{
		static std::unordered_map<uint64_t, float> mAnim = {};

		auto [it, bNew] = mAnim.try_emplace((uint64_t(uID) << 8) | uint64_t(iSalt & 0xFF), flTarget);
		float& flValue = it->second;
		if (!bNew)
		{
			flValue = ImLerp(flValue, flTarget, std::clamp(GetIO().DeltaTime * flSpeed, 0.f, 1.f));
			if (fabsf(flValue - flTarget) < 0.003f)
				flValue = flTarget;
		}
		return flValue;
	}

	inline float Reveal(ImGuiID uID, int iSalt, bool bVisible, float flSpeed = 14.f)
	{
		static std::unordered_map<uint64_t, std::pair<bool, float>> mReveal = {};

		auto [it, bNew] = mReveal.try_emplace((uint64_t(uID) << 8) | uint64_t(iSalt & 0xFF), std::pair{ false, 0.f });
		auto& [bWasVisible, flValue] = it->second;
		if (bVisible && !bWasVisible)
			flValue = 0.f;
		bWasVisible = bVisible;
		flValue = ImLerp(flValue, bVisible ? 1.f : 0.f, std::clamp(GetIO().DeltaTime * flSpeed, 0.f, 1.f));
		if (fabsf(flValue - (bVisible ? 1.f : 0.f)) < 0.003f)
			flValue = bVisible ? 1.f : 0.f;
		return flValue;
	}

	inline ImU32 FadeColor(const ImVec4& tColor, float flMul = 1.f)
	{
		return GetColorU32(ImVec4(tColor.x, tColor.y, tColor.z, tColor.w * flMul));
	}
	inline ImU32 FadeColor(const ImColor& tColor, float flMul = 1.f)
	{
		return FadeColor(tColor.Value, flMul);
	}
	inline ImVec4 MixColor(const ImColor& tFrom, const ImColor& tTo, float flT)
	{
		return ImLerp(tFrom.Value, tTo.Value, flT);
	}
	inline ImU32 LabelColor(float flEmphasis)
	{
		return FadeColor(MixColor(F::Render.Inactive, F::Render.Active, 0.84f + 0.16f * flEmphasis));
	}

	inline bool IsDividerEntry(const char* sEntry)
	{
		return sEntry[0] == '#' && !strcmp(sEntry, "##Divider");
	}

	inline void RenderBackground(ImU32 uBackground, float flRounding = H::Draw.Scale(6))
	{
		ImVec2 vSize = GetWindowSize();
		ImVec2 vDrawPos = GetDrawPos();
		GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + vSize, uBackground, flRounding);
	}
	inline void RenderTitledBackground(float flTitleHeight, const char* sTitle = nullptr, bool bSolid = false)
	{
		const ImVec2 vSize = GetWindowSize();
		const ImVec2 vDrawPos = GetDrawPos();
		ImDrawList* pDrawList = GetWindowDrawList();
		const float flRounding = H::Draw.Scale(6);

		if (bSolid)
			pDrawList->AddRectFilled(vDrawPos, vDrawPos + vSize, F::Render.Background1, flRounding);
		pDrawList->AddRectFilled(vDrawPos, vDrawPos + vSize, F::Render.Background0p5, flRounding);
		if (sTitle)
		{
			ImFont* pFont = F::Render.FontBold;
			pDrawList->AddText(pFont, pFont->LegacySize, vDrawPos + ImVec2(H::Draw.Scale(12), floorf((flTitleHeight - pFont->LegacySize) / 2) + H::Draw.Scale(1)), FadeColor(F::Render.Active), sTitle);
		}
	}
	inline void RenderOverlayWindow(float flHeaderHeight, const char* sIcon, const char* sTitle, const char* sSide = nullptr)
	{
		const ImVec2 vSize = GetWindowSize();
		const ImVec2 vDrawPos = GetDrawPos();
		ImDrawList* pDrawList = GetWindowDrawList();
		const float flRounding = H::Draw.Scale(8);

		pDrawList->AddRectFilled(vDrawPos, vDrawPos + vSize, F::Render.Background1, flRounding);
		if (flHeaderHeight <= 0.f)
			return;

		pDrawList->AddRectFilled(vDrawPos, vDrawPos + ImVec2(vSize.x, flHeaderHeight), F::Render.Background0, flRounding, ImDrawFlags_RoundCornersTop);
		const float flIcon = H::Draw.Scale(15);
		pDrawList->AddText(F::Render.IconFont, flIcon, vDrawPos + ImVec2(H::Draw.Scale(12), floorf((flHeaderHeight - flIcon) / 2)), FadeColor(F::Render.Accent), sIcon);
		ImFont* pBold = F::Render.FontBold;
		pDrawList->AddText(pBold, pBold->LegacySize, vDrawPos + ImVec2(H::Draw.Scale(34), floorf((flHeaderHeight - pBold->LegacySize) / 2)), FadeColor(F::Render.Active), sTitle);
		if (sSide)
		{
			ImFont* pSmall = F::Render.FontSmall;
			const float flWidth = pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, sSide).x;
			pDrawList->AddText(pSmall, pSmall->LegacySize, vDrawPos + ImVec2(vSize.x - H::Draw.Scale(12) - flWidth, floorf((flHeaderHeight - pSmall->LegacySize) / 2)), FadeColor(F::Render.Inactive), sSide);
		}
	}
	inline float OverlayChipWidth(const char* sText)
	{
		ImFont* pSmall = F::Render.FontSmall;
		return pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, sText).x + H::Draw.Scale(16);
	}
	inline float DrawOverlayChip(ImDrawList* pDrawList, float flRight, float flCenterY, const char* sText, bool bOn, const ImVec4& tOn, float flAlpha = 1.f, float flWidth = 0.f)
	{
		ImFont* pSmall = F::Render.FontSmall;
		const float flHeight = H::Draw.Scale(18);
		if (!flWidth)
			flWidth = OverlayChipWidth(sText);
		const ImVec2 vMin = { floorf(flRight - flWidth), floorf(flCenterY - flHeight / 2) }, vMax = vMin + ImVec2(flWidth, flHeight);
		pDrawList->AddRectFilled(vMin, vMax, bOn ? FadeColor(tOn, 0.2f * flAlpha) : FadeColor(F::Render.Background1p5, flAlpha), H::Draw.Scale(4));
		pDrawList->AddText(pSmall, pSmall->LegacySize, { vMin.x + H::Draw.Scale(8), floorf(flCenterY - pSmall->LegacySize / 2) }, FadeColor(bOn ? ImLerp(tOn, F::Render.Active.Value, 0.3f) : F::Render.Inactive.Value, flAlpha), sText);
		return vMin.x;
	}

	inline void Divider(float flPrePadding = H::Draw.Scale(8), float flPostPadding = H::Draw.Scale(8), float flInset = 0.f, ImU32 uBorder = F::Render.Background2)
	{
		const float flPad = GetStyle().WindowPadding.x + flInset;
		const ImVec2 vDrawPos = GetDrawPos() + ImVec2(0, GetCursorPosY());
		GetWindowDrawList()->AddRectFilled(vDrawPos + ImVec2(flPad, flPrePadding), vDrawPos + ImVec2(GetWindowWidth() - flPad, flPrePadding + H::Draw.Scale()), uBorder);
		DebugDummy({ 0, flPrePadding + H::Draw.Scale() + flPostPadding });
	}
	inline void Subheader(const char* sLabel)
	{
		ImFont* pFont = F::Render.FontSmall;
		const ImVec2 vDrawPos = GetDrawPos() + ImVec2(GetStyle().WindowPadding.x, GetCursorPosY() + H::Draw.Scale(14));
		GetWindowDrawList()->AddText(pFont, pFont->LegacySize, vDrawPos, FadeColor(F::Render.Inactive), StripDoubleHash(sLabel).c_str());
		DebugDummy({ 0, H::Draw.Scale(34) });
	}

	inline ImVec2 FCalcTextSize(const char* sText, ImFont* pFont = nullptr)
	{
		if (pFont) PushFont(pFont);
		auto vTextSize = CalcTextSize(sText);
		if (pFont) PopFont();
		return vTextSize;
	}

	inline const char* FormatText(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);

		const char* sText;
		ImFormatStringToTempBufferV(&sText, nullptr, fmt, args);
		va_end(args);

		return sText;
	}

	inline std::string TruncateText(const std::string& sText, int iPixels, ImFont* pFont = nullptr, const char* sEnd = "...", int iMin = 0, int* pLength = nullptr)
	{
		if (sText.empty() || FCalcTextSize(sText.c_str(), pFont).x < iPixels)
			return sText;

		std::string sTruncated = "";
		int i = 0; while (i < iMin || i < sText.length() && FCalcTextSize(std::format("{}{}{}", sTruncated, sText[i], sEnd).c_str(), pFont).x < iPixels)
			sTruncated += sText[i++];
		if (i)
			sTruncated += sEnd;
		if (pLength)
			*pLength = i;

		return sTruncated;
	}

	inline std::vector<std::string> WrapText(const std::string& sText, std::vector<int> vPixels, ImFont* pFont = nullptr)
	{
		if (vPixels.empty())
		{
			IM_ASSERT_USER_ERROR(0, "WrapText() vPixels empty.");
			return {};
		}

		if (sText.empty() || sText.find("\n") == std::string::npos && FCalcTextSize(sText.c_str(), pFont).x < vPixels.front())
			return { sText };

		std::vector<std::string> vWords = { "" };
		for (char iChar : sText)
		{
			if (iChar == ' ' || iChar == '\n' || iChar == '\t')
				vWords.push_back(std::string(1, iChar));
			else
				vWords.back().push_back(iChar);
		}

		std::vector<std::string> vWrapped = { "" };
		int iWord = 0; for (auto& sWord : vWords)
		{
		begin:
			if (sWord.empty())
				continue;

			int iPixels = vPixels[std::min(vWrapped.size(), vPixels.size()) - 1];
			bool bForce = sWord[0] == '\n';
			if (!bForce && FCalcTextSize(std::format("{}{}", vWrapped.back(), sWord).c_str(), pFont).x < iPixels)
			{
				vWrapped.back() += sWord;
				iWord++;
			}
			else
			{
				if (sWord[0] == ' ' || sWord[0] == '\t' || bForce)
					sWord.erase(0, 1);

				if (!iWord && !bForce)
				{
					int i = 0; vWrapped.back() += TruncateText(sWord, iPixels, pFont, "-", 1, &i);
					sWord = sWord.substr(i, sWord.length() - i);
				}
				if (!sWord.empty() || bForce)
				{
					vWrapped.push_back(""); iWord = 0;
					goto begin;
				}
			}
		}

		return vWrapped;
	}
	inline std::vector<std::string> WrapText(const std::string& sText, int iPixels, ImFont* pFont = nullptr)
	{
		return WrapText(sText, std::vector<int>({ iPixels }), pFont);
	}
	inline std::string ListPreview(const std::vector<std::string>& vNames, float flWidth, const char* sNone)
	{
		if (vNames.empty())
			return sNone;

		std::string sJoined = vNames.front();
		for (size_t i = 1; i < vNames.size(); i++)
			sJoined += ", " + vNames[i];
		if (CalcTextSize(sJoined.c_str()).x <= flWidth)
			return sJoined;

		const std::string sMore = std::format(" +{}", vNames.size() - 1);
		if (CalcTextSize((vNames.front() + sMore).c_str()).x <= flWidth)
			return vNames.front() + sMore;
		return std::format("{} selected", vNames.size());
	}

	inline void FNote(const char* sText)
	{
		ImFont* pFont = F::Render.FontSmall;
		const float flPad = GetStyle().WindowPadding.x, flWidth = GetWindowWidth() - flPad * 2;
		const auto vLines = WrapText(sText, int(flWidth), pFont);
		const ImVec2 vPos = GetDrawPos() + ImVec2(flPad, GetCursorPosY());
		const float flLine = pFont->LegacySize + H::Draw.Scale(3);
		const ImU32 uColor = FadeColor(MixColor(F::Render.Inactive, F::Render.Active, 0.25f), Transparent || Disabled ? 0.5f : 1.f);
		for (size_t i = 0; i < vLines.size(); i++)
			GetWindowDrawList()->AddText(pFont, pFont->LegacySize, vPos + ImVec2(0, H::Draw.Scale(2) + flLine * i), uColor, vLines[i].c_str());
		DebugDummy({ flWidth, flLine * vLines.size() + H::Draw.Scale(6) });
	}

	inline void FTooltip(const char* sTooltip, bool bCondition = IsItemHovered(), float flWrapWidth = 300, ImFont* pFont = F::Render.FontSmall, ImVec2* pPosOverride = nullptr, ImVec2* pSizeOverride = nullptr)
	{
		if (!bCondition)
			return;

		PushFont(pFont);

		ImDrawList* pDrawList = GetForegroundDrawList();
		const ImVec2 vAnchor = pPosOverride ? *pPosOverride : !pRowSizes->empty() ? pRowSizes->back().m_vPos : ImVec2((GetItemRectMin().x + GetItemRectMax().x) / 2, GetItemRectMax().y);
		const ImVec2 vAnchorSize = pSizeOverride ? *pSizeOverride : !pRowSizes->empty() ? pRowSizes->back().m_vSize : ImVec2();

		const auto vWraps = WrapText(sTooltip, H::Draw.Scale(flWrapWidth));
		const float flPadX = H::Draw.Scale(10), flPadY = H::Draw.Scale(7), flGap = H::Draw.Scale(6);
		const float flLine = GetTextLineHeight() + H::Draw.Scale(3);
		ImVec2 vBox = { 0.f, flLine * vWraps.size() - H::Draw.Scale(3) };
		for (auto& sText : vWraps)
			vBox.x = std::max(vBox.x, CalcTextSize(sText.c_str()).x);
		vBox += ImVec2(flPadX * 2, flPadY * 2);

		const ImVec2 vDisplay = GetIO().DisplaySize;
		ImVec2 vPos = { vAnchor.x + vAnchorSize.x / 2 - vBox.x / 2, vAnchor.y + vAnchorSize.y + flGap };
		if (vPos.y + vBox.y > vDisplay.y - H::Draw.Scale(4))
			vPos.y = vAnchor.y - vBox.y - flGap;
		vPos.x = std::clamp(vPos.x, H::Draw.Scale(4), std::max(H::Draw.Scale(4), vDisplay.x - vBox.x - H::Draw.Scale(4)));
		vPos = { floorf(vPos.x), floorf(vPos.y) };

		pDrawList->AddRectFilled(vPos, vPos + vBox, F::Render.Popup, H::Draw.Scale(5));
		for (size_t i = 0; i < vWraps.size(); i++)
			pDrawList->AddText(vPos + ImVec2(flPadX, flPadY + flLine * i), F::Render.Active, vWraps[i].c_str());

		PopFont();
	}

	inline void FText(const char* sText, ImVec2 vOffset = {}, int iFlags = FTextEnum::None, ImFont* pFont = nullptr)
	{
		const char* sDisplay = sText ? sText : "";

		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		if (pFont)
			PushFont(pFont);

		if (iFlags & FTextEnum::SameLine)
			SameLine();
		if (iFlags & (FTextEnum::Middle | FTextEnum::Right))
		{
			float flWindowWidth = GetWindowWidth();
			float flTextWidth = CalcTextSize(sDisplay).x;
			if (iFlags & FTextEnum::Middle)
				SetCursorPosX((flWindowWidth - flTextWidth) * 0.5f);
			else if (iFlags & FTextEnum::Right)
				SetCursorPosX(flWindowWidth - flTextWidth - GetStyle().WindowPadding.x);
		}
		SetCursorPos(GetCursorPos() + vOffset * H::Draw.Scale());
		TextUnformatted(sDisplay);

		if (pFont)
			PopFont();

		if (Transparent || Disabled)
			PopStyleVar();
	}

	inline bool FInputText(const char* sLabel, std::string& sText, float flWidth = H::Draw.Scale(150), ImGuiInputTextFlags iFlags = ImGuiInputTextFlags_None, ImGuiInputTextCallback fCallback = nullptr)
	{
		const std::string sDisplay = StripDoubleHash(sLabel);
		const float flHeight = H::Draw.Scale(28);
		PushStyleVar(ImGuiStyleVar_FramePadding, { H::Draw.Scale(10), floorf((flHeight - GetFontSize()) / 2) });
		PushStyleVar(ImGuiStyleVar_FrameRounding, H::Draw.Scale(4));
		PushStyleColor(ImGuiCol_FrameBg, F::Render.Background1p5.Value);
		PushStyleColor(ImGuiCol_FrameBgHovered, F::Render.Background1p5L.Value);
		PushStyleColor(ImGuiCol_FrameBgActive, F::Render.Background1p5L.Value);
		PushStyleColor(ImGuiCol_Text, F::Render.Active.Value);
		PushItemWidth(flWidth);

		bool bReturn = InputText(std::format("##{}", sLabel).c_str(), &sText, iFlags | ImGuiInputTextFlags_NoKeyboardNavigate, fCallback);
		const bool bHovered = IsItemHovered();
		if (sText.empty() && !IsItemActive())
			GetWindowDrawList()->AddText(GetItemRectMin() + GetStyle().FramePadding, FadeColor(F::Render.Inactive), sDisplay.c_str());
		if (!Disabled && bHovered)
			SetMouseCursor(ImGuiMouseCursor_TextInput);

		PopItemWidth();
		PopStyleColor(4);
		PopStyleVar(2);

		return bReturn;
	}

	inline void IconImage(const char* sIcon, ImVec4 tColor = { 1, 1, 1, -1 })
	{
		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		if (tColor.w >= 0.f)
			PushStyleColor(ImGuiCol_Text, tColor);
		PushFont(F::Render.IconFont);

		TextUnformatted(sIcon);

		PopFont();
		if (tColor.w >= 0.f)
			PopStyleColor();

		if (Transparent || Disabled)
			PopStyleVar();
	}

	inline bool IconButton(const char* sIcon, float flSize = H::Draw.Scale(24), ImVec4 tColor = { 1, 1, 1, -1 }, bool* pHovered = nullptr)
	{
		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		ImVec2 vOriginalPos = GetCursorPos();
		const ImVec2 vDrawPos = GetDrawPos() + vOriginalPos;
		const std::string sLabel = std::format("{}##{}:{}", sIcon, vOriginalPos.x, vOriginalPos.y);
		const ImGuiID uID = GetID(sLabel.c_str());
		const bool bWithin = !Disabled && IsWindowHovered() && IsMouseWithin(vDrawPos.x, vDrawPos.y, flSize, flSize);
		const float flHover = Anim(uID, 0, bWithin ? 1.f : 0.f);
		if (flHover > 0.01f)
			GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + ImVec2(flSize, flSize), FadeColor(F::Render.Active, (IsMouseDown(ImGuiMouseButton_Left) && bWithin ? 0.12f : 0.06f) * flHover), H::Draw.Scale(4));

		PushStyleColor(ImGuiCol_Text, tColor.w >= 0.f ? tColor : MixColor(F::Render.Inactive, F::Render.Active, flHover));
		PushFont(F::Render.IconFont);

		bool bReturn = Button(sLabel.c_str(), { flSize, flSize });

		if (!Disabled && IsItemHovered())
			SetMouseCursor(ImGuiMouseCursor_Hand);
		if (pHovered)
			*pHovered = IsItemHovered();
		PopFont();
		PopStyleColor();

		if (Transparent || Disabled)
			PopStyleVar();

		return Disabled ? false : bReturn;
	}

	inline bool FIconButton(const char* sIcon, const char* sTooltip, float flSize = H::Draw.Scale(28), ImVec4 tColor = { 1, 1, 1, -1 })
	{
		bool bHovered = false;
		const bool bReturn = IconButton(sIcon, flSize, tColor, &bHovered);
		ImVec2 vAnchor = { (GetItemRectMin().x + GetItemRectMax().x) / 2, GetItemRectMax().y }, vAnchorSize = {};
		FTooltip(sTooltip, bHovered && sTooltip, 300, F::Render.FontSmall, &vAnchor, &vAnchorSize);
		return bReturn;
	}

	struct RowInfo_t
	{
		ImVec2 m_vMin = {};
		ImVec2 m_vMax = {};
		float m_flAccessoryX = 0.f;
		float m_flLineHeight = 0.f;
	};
	inline RowInfo_t LastRow = {};
	inline ImVec2 TakeAccessory(float flWidth, float flHeight)
	{
		const ImVec2 vPos = { floorf(LastRow.m_flAccessoryX - flWidth), floorf(LastRow.m_vMin.y + (LastRow.m_flLineHeight - flHeight) / 2) };
		LastRow.m_flAccessoryX = vPos.x - H::Draw.Scale(6);
		return vPos;
	}

	inline float RowWidth(int iFlags)
	{
		const float flPad = GetStyle().WindowPadding.x;
		float flWidth = GetWindowWidth();
		if (iFlags & (1 << 0 | 1 << 1))
		{
			flWidth = floorf(flWidth / 2 - flPad * 1.5f);
			if (iFlags & 1 << 1)
				SameLine(flWidth + flPad * 2);
		}
		else
			flWidth -= flPad * 2;
		return flWidth;
	}
	inline std::vector<std::string> RowLabel(const char* sLabel, float flWidth)
	{
		auto vLines = WrapText(StripDoubleHash(sLabel), int(flWidth));
		if (vLines.size() > 2)
			vLines.resize(2);
		return vLines;
	}
	inline void DrawRowLabel(ImDrawList* pDrawList, ImVec2 vMin, float flHeight, const std::vector<std::string>& vLines, ImU32 uColor)
	{
		const float flLine = H::Draw.Scale(16);
		const float flBlock = flLine * (vLines.size() - 1) + GetFontSize();
		const float flY = floorf(vMin.y + (flHeight - flBlock) / 2);
		for (size_t i = 0; i < vLines.size(); i++)
			pDrawList->AddText({ vMin.x, flY + flLine * i }, uColor, vLines[i].c_str());
	}
	inline void EndRow(ImVec2 vOriginalPos, ImVec2 vSize)
	{
		SetCursorPos(vOriginalPos);
		AddRowSize(vOriginalPos, vSize);
		DebugDummy({ vSize.x, GetRowSize(vSize.y) });
	}

	inline void CloseStalePopups()
	{
		ImGuiContext& g = *GImGui;
		for (int i = 0; i < g.OpenPopupStack.Size; i++)
		{
			const ImGuiPopupData& tPopup = g.OpenPopupStack[i];
			if (tPopup.OpenFrameCount < g.FrameCount && (!tPopup.Window || !tPopup.Window->Active))
			{
				ClosePopupToLevel(i, false);
				break;
			}
		}
	}

	inline bool FBeginPopup(const char* sLabel, float flOffset = 0.f, ImGuiWindowFlags iFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)
	{
		bool bReturn = BeginPopup(sLabel, iFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

		if (bReturn)
		{
			RenderBackground(F::Render.Popup);

			if (flOffset)
				SetCursorPosY(GetCursorPosY() + H::Draw.Scale(flOffset));
		}

		return bReturn;
	}

	inline bool FBeginPopupModal(const char* sLabel, float flOffset = 0.f, bool* pOpen = nullptr, ImGuiWindowFlags iFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)
	{
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(16), H::Draw.Scale(14) });
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), 0 });
		bool bReturn = BeginPopupModal(sLabel, pOpen, iFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

		if (bReturn)
		{
			RenderBackground(F::Render.Popup, H::Draw.Scale(8));

			if (flOffset)
				SetCursorPosY(GetCursorPosY() + H::Draw.Scale(flOffset));
		}
		else
			PopStyleVar(2);

		return bReturn;
	}
	inline void FEndPopupModal()
	{
		EndPopup();
		PopStyleVar(2);
	}
	inline void FModalHeader(const char* sTitle, const char* sText = nullptr)
	{
		PushFont(F::Render.FontLarge);
		TextUnformatted(sTitle);
		PopFont();
		if (sText)
		{
			Dummy({ 0, H::Draw.Scale(6) });
			PushStyleColor(ImGuiCol_Text, MixColor(F::Render.Inactive, F::Render.Active, 0.45f));
			PushTextWrapPos(GetWindowWidth() - GetStyle().WindowPadding.x);
			TextUnformatted(sText);
			PopTextWrapPos();
			PopStyleColor();
		}
		Dummy({ 0, H::Draw.Scale(14) });
	}

	inline bool FPopupButton(const char* sLabel, ImVec2 vOffset = {}, float flOffset = 0.f, int iFlags = FButtonEnum::SameLine, const char* sIcon = nullptr)
	{
		const ImVec2 vOriginalPos = GetCursorPos();
		const float flSize = H::Draw.Scale(22);
		ImVec2 vIdealPosition, vIdealSize;
		if (iFlags & FButtonEnum::SameLine)
		{
			SetCursorScreenPos(TakeAccessory(flSize, flSize) + vOffset * H::Draw.Scale());
			vIdealPosition = { GetDrawPos().x, LastRow.m_vMax.y + H::Draw.Scale(4) };
		}
		else
		{
			SetCursorPos(vOriginalPos + vOffset * H::Draw.Scale());
			vIdealPosition = GetDrawPos() + ImVec2(0, GetCursorPosY() + flSize + H::Draw.Scale(4));
		}
		vIdealSize = { GetWindowWidth(), 0 };

		if (!sIcon)
			sIcon = ICON_MD_TUNE;
		const bool bOpen = IsPopupOpen(sLabel);
		if (IconButton(sIcon, flSize, bOpen ? F::Render.Accent.Value : ImVec4(1, 1, 1, -1)))
			OpenPopup(sLabel);
		if (iFlags & FButtonEnum::SameLine)
			SetCursorPos(vOriginalPos);

		SetNextWindowPos(vIdealPosition, ImGuiCond_Appearing);
		SetNextWindowSize(vIdealSize, ImGuiCond_Appearing);
		return FBeginPopup(sLabel, flOffset);
	}

	inline bool FBeginContextMenu(const char* sLabel)
	{
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(5), H::Draw.Scale(5) });
		SetNextWindowSizeConstraints({ H::Draw.Scale(170), 0 }, { FLT_MAX, FLT_MAX });
		const bool bReturn = FBeginPopup(sLabel, 0.f, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
		PopStyleVar();
		if (bReturn)
			PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
		return bReturn;
	}
	inline void FEndContextMenu()
	{
		PopStyleVar();
		EndPopup();
	}
	inline void FMenuCaption(const char* sLabel)
	{
		ImFont* pFont = F::Render.FontSmall;
		const ImVec2 vPos = GetCursorScreenPos();
		const float flHeight = H::Draw.Scale(22);
		GetWindowDrawList()->AddText(pFont, pFont->LegacySize, { vPos.x + H::Draw.Scale(9), floorf(vPos.y + flHeight - pFont->LegacySize - H::Draw.Scale(3)) }, FadeColor(F::Render.Inactive), sLabel);
		Dummy({ pFont->CalcTextSizeA(pFont->LegacySize, FLT_MAX, 0.f, sLabel).x + H::Draw.Scale(18), flHeight });
	}
	inline void FMenuSeparator()
	{
		const ImVec2 vPos = GetCursorScreenPos();
		const float flHeight = H::Draw.Scale(9);
		GetWindowDrawList()->AddRectFilled({ vPos.x + H::Draw.Scale(4), vPos.y + floorf(flHeight / 2) }, { vPos.x + GetContentRegionAvail().x - H::Draw.Scale(4), vPos.y + floorf(flHeight / 2) + H::Draw.Scale() }, FadeColor(F::Render.Background2));
		Dummy({ 0, flHeight });
	}
	inline void DrawMenuRow(ImDrawList* pDrawList, ImVec2 vMin, ImVec2 vMax, const char* sLabel, const char* sIcon, ImVec4 tColor, int iFlags, float flHover, const char* sTrailing = nullptr)
	{
		const float flText = sIcon ? H::Draw.Scale(34) : H::Draw.Scale(10);
		const float flAlpha = Disabled || Transparent ? 0.5f : 1.f;
		const bool bTinted = tColor.w > 0.f;
		if (flHover > 0.01f)
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Active, 0.06f * flHover), H::Draw.Scale(4));
		if (sIcon)
		{
			ImFont* pIcon = F::Render.IconFont;
			const ImVec2 vIcon = pIcon->CalcTextSizeA(pIcon->LegacySize, FLT_MAX, 0.f, sIcon);
			const ImVec4 tIcon = bTinted ? tColor : MixColor(F::Render.Inactive, F::Render.Active, flHover * 0.6f);
			pDrawList->AddText(pIcon, pIcon->LegacySize, { floorf(vMin.x + H::Draw.Scale(10)), floorf((vMin.y + vMax.y - vIcon.y) / 2) }, FadeColor(tIcon, flAlpha), sIcon);
		}
		const ImVec4 tText = bTinted && iFlags & FMenuItemEnum::TintLabel ? tColor : F::Render.Active.Value;
		pDrawList->AddText({ vMin.x + flText, floorf((vMin.y + vMax.y - GetTextLineHeight()) / 2) }, FadeColor(tText, flAlpha), sLabel);
		if (sTrailing)
		{
			ImFont* pIcon = F::Render.IconFont;
			const ImVec4 tTrailing = iFlags & FMenuItemEnum::Checked ? F::Render.Accent.Value : F::Render.Inactive.Value;
			pDrawList->AddText(pIcon, pIcon->LegacySize, { vMax.x - H::Draw.Scale(26), floorf((vMin.y + vMax.y - pIcon->LegacySize) / 2) }, FadeColor(tTrailing, flAlpha), sTrailing);
		}
	}
	inline bool FMenuItem(const char* sLabel, const char* sIcon = nullptr, ImVec4 tColor = {}, int iFlags = FMenuItemEnum::None)
	{
		const std::string sDisplay = StripDoubleHash(sLabel);
		const bool bChecked = iFlags & FMenuItemEnum::Checked;
		const float flHeight = H::Draw.Scale(28), flText = sIcon ? H::Draw.Scale(34) : H::Draw.Scale(10);
		const float flIdeal = flText + CalcTextSize(sDisplay.c_str()).x + H::Draw.Scale(bChecked ? 36 : 14);

		PushStyleColor(ImGuiCol_Header, ImVec4());
		PushStyleColor(ImGuiCol_HeaderHovered, ImVec4());
		PushStyleColor(ImGuiCol_HeaderActive, ImVec4());
		bool bReturn = Selectable(std::format("##{}", sLabel).c_str(), false, ImGuiSelectableFlags_SpanAvailWidth, { flIdeal, flHeight });
		PopStyleColor(3);

		const bool bHovered = IsItemHovered() && !Disabled;
		if (bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);
		DrawMenuRow(GetWindowDrawList(), GetItemRectMin(), GetItemRectMax(), sDisplay.c_str(), sIcon, tColor, iFlags, Anim(GetItemID(), 0, bHovered ? 1.f : 0.f, 24.f), bChecked ? ICON_MD_CHECK : nullptr);

		return Disabled ? false : bReturn;
	}
	inline bool FBeginMenu(const char* sLabel, const char* sIcon = nullptr, bool* pClicked = nullptr)
	{
		ImGuiWindow* pWindow = GetCurrentWindow();
		const std::string sDisplay = StripDoubleHash(sLabel), sID = std::format("##{}", sLabel);
		const float flHeight = H::Draw.Scale(28), flText = sIcon ? H::Draw.Scale(34) : H::Draw.Scale(10);
		const float flIdeal = flText + CalcTextSize(sDisplay.c_str()).x + H::Draw.Scale(40);
		const ImVec2 vMin = GetCursorScreenPos(), vMax = { vMin.x + std::max(GetContentRegionAvail().x, flIdeal), vMin.y + flHeight };

		const ImGuiID uID = GetID(sID.c_str());
		const bool bOpen = IsPopupOpen(uID, ImGuiPopupFlags_None);
		const bool bHovered = !Disabled && IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && IsMouseHoveringRect(vMin, vMax);
		if (pClicked)
			*pClicked = bHovered && IsMouseClicked(ImGuiMouseButton_Left);
		if (bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);
		DrawMenuRow(GetWindowDrawList(), vMin, vMax, sDisplay.c_str(), sIcon, {}, FMenuItemEnum::None, Anim(uID, 0, bHovered || bOpen ? 1.f : 0.f, 24.f), ICON_MD_KEYBOARD_ARROW_RIGHT);
		Dummy({ flIdeal, 0 });

		const float flSpacing = flHeight - GetTextLineHeight();
		SetCursorScreenPos({ vMin.x, vMin.y + IM_TRUNC(flSpacing / 2) });
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, flSpacing });
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(5), H::Draw.Scale(5) });
		PushStyleColor(ImGuiCol_Text, ImVec4());
		PushStyleColor(ImGuiCol_Header, ImVec4());
		PushStyleColor(ImGuiCol_HeaderHovered, ImVec4());
		PushStyleColor(ImGuiCol_HeaderActive, ImVec4());
		PushStyleColor(ImGuiCol_PopupBg, ImVec4());
		SetNextWindowSizeConstraints({ H::Draw.Scale(150), 0 }, { FLT_MAX, FLT_MAX });
		const bool bReturn = BeginMenu(sID.c_str(), !Disabled, false);
		PopStyleColor(5);
		PopStyleVar(2);
		pWindow->DC.CursorPos.y = vMin.y + flHeight;
		pWindow->DC.CursorMaxPos.y = std::max(pWindow->DC.CursorMaxPos.y, vMin.y + flHeight);

		if (bReturn)
		{
			GetCurrentWindow()->Flags |= ImGuiWindowFlags_NoScrollbar;
			RenderBackground(F::Render.Popup);
		}
		return bReturn;
	}

	inline void DrawCheckbox(ImDrawList* pDrawList, ImVec2 vMin, float flOn, float flHover, bool bPlain = false)
	{
		const float flSize = H::Draw.Scale(14), flRounding = H::Draw.Scale(3.5f);
		const ImVec2 vMax = vMin + ImVec2(flSize, flSize);
		const ImVec4 tOn = bPlain ? F::Render.Active.Value : F::Render.Accent.Value;

		if (flOn < 1.f)
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(MixColor(F::Render.Track, F::Render.Background1p5L, flHover), 1.f - flOn), flRounding);
		if (flOn > 0.f)
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(tOn, flOn), flRounding);
		if (flOn > 0.01f)
		{
			pDrawList->PathClear();
			pDrawList->PathLineTo(vMin + ImVec2(H::Draw.Scale(3.5f), H::Draw.Scale(7.3f)));
			pDrawList->PathLineTo(vMin + ImVec2(H::Draw.Scale(6.f), H::Draw.Scale(9.8f)));
			pDrawList->PathLineTo(vMin + ImVec2(H::Draw.Scale(10.7f), H::Draw.Scale(4.4f)));
			pDrawList->PathStroke(FadeColor(bPlain ? ImVec4(0.04f, 0.05f, 0.06f, 1.f) : F::Render.OnAccent.Value, flOn), ImDrawFlags_None, H::Draw.Scale(1.7f));
		}
	}

	inline void FCheckboxIcon(const ImVec2& vPos, bool bActive)
	{
		DrawCheckbox(GetWindowDrawList(), vPos, bActive ? 1.f : 0.f, 0.f);
	}

	inline std::unordered_map<uint32_t, float> mLastHeights = {};
	inline std::vector<uint32_t> vStoredLabels = {};
	inline float GetSectionHeight(const char* sLabel, float flDefault = 0.f)
	{
		auto it = mLastHeights.find(FNV1A::Hash32(sLabel));
		return it != mLastHeights.end() ? it->second : flDefault;
	}
	inline bool Section(const char* sLabel, float flPaddingMod = 0.f, float flMinHeight = 28.f, bool bForceHeight = false, bool bAutoHeight = false)
	{
		uint32_t uHash = FNV1A::Hash32(sLabel);
		const bool bTitled = sLabel[0] != '#';
		vStoredLabels.push_back(uHash);

		if (!bForceHeight && !bAutoHeight && mLastHeights.contains(uHash) && mLastHeights[uHash] > flMinHeight)
			flMinHeight = mLastHeights[uHash];
		PushStyleVar(ImGuiStyleVar_CellPadding, { 0, 0 });
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(14), H::Draw.Scale(10) });

		const ImGuiChildFlags iChildFlags = bAutoHeight ? ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize : ImGuiChildFlags_AlwaysUseWindowPadding;
		bool bReturn = BeginChild(sLabel, { GetColumnWidth(), bAutoHeight ? 0.f : flMinHeight }, iChildFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (bReturn)
		{
			if (bTitled)
				RenderTitledBackground(H::Draw.Scale(34), StripDoubleHash(sLabel).c_str());
			else
				RenderBackground(F::Render.Background0p5);
		}

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), 0 });
		if (bTitled)
			DebugDummy({ 0, H::Draw.Scale(26) });
		if (flPaddingMod)
			SetCursorPosY(GetCursorPosY() + H::Draw.Scale(flPaddingMod));

		return bReturn;
	}
	inline void EndSection()
	{
		uint32_t uHash = vStoredLabels.back();
		vStoredLabels.pop_back();
		float flHeight = GetItemRectMax().y - GetWindowPos().y;
		if (flHeight > 0.f)
			mLastHeights[uHash] = flHeight + GetStyle().WindowPadding.y;

		PopStyleVar();
		EndChild();
		PopStyleVar(2);
	}

	inline bool PageTable(const char* sID, int iColumns = 2, ImGuiTableFlags iFlags = ImGuiTableFlags_None)
	{
		const int iFit = std::clamp(int(GetContentRegionAvail().x / H::Draw.Scale(300)), 1, iColumns);
		return BeginTable(sID, iFit, iFlags);
	}

	inline void FLabel(const char* sLabel, int iFlags = FToggleEnum::None)
	{
		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		const float flWidth = RowWidth(iFlags);
		const ImVec2 vOriginalPos = GetCursorPos(), vRowMin = GetDrawPos() + vOriginalPos;
		const auto vLines = RowLabel(sLabel, flWidth - H::Draw.Scale(60));
		const ImVec2 vSize = { flWidth, H::Draw.Scale(32 + 16 * (int(vLines.size()) - 1)) };

		DrawRowLabel(GetWindowDrawList(), vRowMin, vSize.y, vLines, LabelColor(0.f));
		LastRow = { vRowMin, vRowMin + vSize, vRowMin.x + flWidth, vSize.y };
		EndRow(vOriginalPos, vSize);

		if (Transparent || Disabled)
			PopStyleVar();
	}

	inline bool FButton(const char* sLabel, int iFlags = FButtonEnum::None, ImVec2 vSize = { 0, 30 }, int iSizeOffset = 0, ImFont* pFont = nullptr, bool* pHovered = nullptr)
	{
		if (pFont)
			PushFont(pFont);
		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		const float flPad = GetStyle().WindowPadding.x;
		if (!vSize.x)
		{
			vSize.x = GetWindowWidth();
			if (iFlags & (FButtonEnum::Left | FButtonEnum::Right))
				vSize.x = floorf(vSize.x / 2 - flPad * 1.5f);
			else if (iFlags & FButtonEnum::Fit)
				vSize.x = CalcTextSize(StripDoubleHash(sLabel).c_str()).x + H::Draw.Scale(28);
			else
				vSize.x -= flPad * 2;
			if (iFlags & FButtonEnum::SameLine)
				SameLine();
			else if (iFlags & FButtonEnum::Right)
				SameLine(vSize.x + flPad * 2);
		}
		else
		{
			vSize.x = H::Draw.Scale(vSize.x);
			if (iFlags & FButtonEnum::SameLine)
				SameLine();
		}
		vSize.x += H::Draw.Scale(iSizeOffset); vSize.y = H::Draw.Scale(vSize.y);

		const ImVec2 vOriginalPos = GetCursorPos();
		const float flMargin = H::Draw.Scale(3);
		SetCursorPos(vOriginalPos + ImVec2(0, flMargin));
		bool bReturn = InvisibleButton(sLabel, vSize);
		const bool bHovered = IsItemHovered(), bHeld = IsItemActive();
		if (pHovered)
			*pHovered = bHovered;
		if (!Disabled && bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);

		const ImVec2 vMin = GetItemRectMin(), vMax = GetItemRectMax();
		const ImGuiID uID = GetItemID();
		const float flHover = Anim(uID, 0, bHovered && !Disabled ? 1.f : 0.f), flPress = Anim(uID, 1, bHeld && !Disabled ? 1.f : 0.f, 30.f);
		ImDrawList* pDrawList = GetWindowDrawList();
		ImVec4 tFill, tText;
		if (iFlags & FButtonEnum::Primary)
		{
			tFill = ImLerp(F::Render.Accent.Value, ImLerp(F::Render.Accent.Value, F::Render.Active.Value, 0.16f), flHover);
			tFill.w = F::Render.Accent.Value.w * (1.f - 0.15f * flPress);
			tText = F::Render.OnAccent;
		}
		else if (iFlags & FButtonEnum::Danger)
		{
			tFill = F::Render.Danger;
			tFill.w = 0.14f + 0.08f * flHover + 0.06f * flPress;
			tText = F::Render.Danger;
		}
		else
		{
			tFill = ImLerp(MixColor(F::Render.Background1p5, F::Render.Background1p5L, flHover), F::Render.Track.Value, flPress);
			tText = F::Render.Active;
		}
		pDrawList->AddRectFilled(vMin, vMax, FadeColor(tFill), H::Draw.Scale(4));
		const std::string sText = StripDoubleHash(sLabel);
		const ImVec2 vText = CalcTextSize(sText.c_str());
		pDrawList->AddText({ floorf((vMin.x + vMax.x - vText.x) / 2), floorf((vMin.y + vMax.y - vText.y) / 2) }, FadeColor(tText), sText.c_str());

		EndRow(vOriginalPos, vSize + ImVec2(0, flMargin * 2));

		if (Transparent || Disabled)
			PopStyleVar();
		if (pFont)
			PopFont();

		return Disabled ? false : bReturn;
	}

	inline bool FConfirmModal(const char* sID, const char* sTitle, const char* sText, const char* sConfirm = "Delete", bool bDanger = true)
	{
		bool bReturn = false;
		SetNextWindowSize({ H::Draw.Scale(320), 0 });
		if (FBeginPopupModal(sID))
		{
			FModalHeader(sTitle, sText);
			if (FButton("Cancel", FButtonEnum::Left) || IsKeyPressed(ImGuiKey_Escape))
				CloseCurrentPopup();
			if (FButton(sConfirm, FButtonEnum::Right | (bDanger ? FButtonEnum::Danger : FButtonEnum::Primary)) || IsKeyPressed(ImGuiKey_Enter))
			{
				bReturn = true;
				CloseCurrentPopup();
			}
			FEndPopupModal();
		}
		return bReturn;
	}

	inline void FEmptyState(const char* sText, float flTop)
	{
		SetCursorPos({ floorf((GetWindowWidth() - CalcTextSize(sText).x) / 2), flTop + H::Draw.Scale(24) });
		TextColored(F::Render.Inactive.Value, sText);
		SetCursorPosY(flTop + H::Draw.Scale(64));
		DebugDummy({});
	}

	inline void FSkeletonRect(ImDrawList* pDrawList, ImVec2 vMin, ImVec2 vMax, float flRounding = H::Draw.Scale(4), float flAlpha = 1.f)
	{
		if (!pDrawList || vMax.x <= vMin.x || vMax.y <= vMin.y || flAlpha <= 0.f)
			return;

		flAlpha *= Transparent || Disabled ? 0.5f : 1.f;
		const float flWidth = vMax.x - vMin.x;
		const float flPhase = fmodf(float(GetTime()) * 0.8f, 1.f);
		const float flBand = std::max(flWidth * 0.7f, H::Draw.Scale(24));
		const float flBandX = vMin.x - flBand + (flWidth + flBand) * flPhase;
		const ImU32 uEdge = FadeColor(F::Render.Active, 0.f);
		const ImU32 uCenter = FadeColor(F::Render.Active, 0.055f * flAlpha);

		pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Background1p5L, flAlpha), flRounding);
		pDrawList->PushClipRect(vMin, vMax, true);
		{
			const float flMiddle = flBandX + flBand / 2.f;
			pDrawList->AddRectFilledMultiColor({ flBandX, vMin.y }, { flMiddle, vMax.y }, uEdge, uCenter, uCenter, uEdge);
			pDrawList->AddRectFilledMultiColor({ flMiddle, vMin.y }, { flBandX + flBand, vMax.y }, uCenter, uEdge, uEdge, uCenter);
		}
		pDrawList->PopClipRect();
	}

	inline void FSkeletonLine(ImDrawList* pDrawList, ImVec2 vMin, float flWidth, float flHeight = H::Draw.Scale(9), float flAlpha = 1.f)
	{
		FSkeletonRect(pDrawList, vMin, vMin + ImVec2(flWidth, flHeight), flHeight / 2.f, flAlpha);
	}

	inline void FSkeletonRows(float flTop, int iCount, float flHeight = H::Draw.Scale(30), float flIndent = 0.f)
	{
		const float flWidth = GetWindowWidth() - GetStyle().WindowPadding.x * 2 - flIndent;
		ImDrawList* pDrawList = GetWindowDrawList();
		for (int i = 0; i < iCount; i++)
		{
			const ImVec2 vMin = GetDrawPos() + ImVec2(GetStyle().WindowPadding.x + flIndent, flTop + i * (flHeight + H::Draw.Scale(2)));
			const ImVec2 vMax = vMin + ImVec2(flWidth, flHeight);
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Background1p5, 0.55f), H::Draw.Scale(5));
			FSkeletonRect(pDrawList, vMin + ImVec2(H::Draw.Scale(8), floorf((flHeight - H::Draw.Scale(16)) / 2)), vMin + ImVec2(H::Draw.Scale(24), floorf((flHeight + H::Draw.Scale(16)) / 2)), H::Draw.Scale(4));
			FSkeletonLine(pDrawList, vMin + ImVec2(H::Draw.Scale(32), floorf((flHeight - H::Draw.Scale(8)) / 2)), std::min(H::Draw.Scale(150), flWidth * 0.4f), H::Draw.Scale(8));
			FSkeletonLine(pDrawList, vMin + ImVec2(flWidth - H::Draw.Scale(54), floorf((flHeight - H::Draw.Scale(8)) / 2)), H::Draw.Scale(40), H::Draw.Scale(8));
		}
		SetCursorPosY(flTop + iCount * (flHeight + H::Draw.Scale(2)));
		DebugDummy({});
	}

	inline void FSkeletonCards(float flTop, int iCount = 6)
	{
		const float flPad = GetStyle().WindowPadding.x, flWidth = GetWindowWidth() - flPad * 2;
		const float flGap = H::Draw.Scale(8), flHeight = H::Draw.Scale(52);
		const int iColumns = std::clamp(int((flWidth + flGap) / H::Draw.Scale(250)), 1, 3);
		const float flCardWidth = floorf((flWidth - flGap * (iColumns - 1)) / iColumns);
		const int iRows = (iCount + iColumns - 1) / iColumns;
		ImDrawList* pDrawList = GetWindowDrawList();

		for (int i = 0; i < iCount; i++)
		{
			const ImVec2 vMin = GetDrawPos() + ImVec2(flPad + (i % iColumns) * (flCardWidth + flGap), flTop + (i / iColumns) * (flHeight + flGap));
			const ImVec2 vMax = vMin + ImVec2(flCardWidth, flHeight);
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Background1p5, 0.55f), H::Draw.Scale(6));
			FSkeletonRect(pDrawList, vMin + ImVec2(H::Draw.Scale(10), H::Draw.Scale(10)), vMin + ImVec2(H::Draw.Scale(42), H::Draw.Scale(42)), H::Draw.Scale(5));
			FSkeletonLine(pDrawList, vMin + ImVec2(H::Draw.Scale(52), H::Draw.Scale(15)), std::min(flCardWidth * 0.48f, H::Draw.Scale(120)), H::Draw.Scale(9));
			FSkeletonLine(pDrawList, vMin + ImVec2(H::Draw.Scale(52), H::Draw.Scale(30)), std::min(flCardWidth * 0.66f, H::Draw.Scale(160)), H::Draw.Scale(7));
		}

		SetCursorPosY(flTop + iRows * (flHeight + flGap) - flGap);
		DebugDummy({});
	}

	inline void FSkeletonGrid(float flTop, float flHeight, int iColumns, float flCellWidth, float flIconHeight, int iCount = 8)
	{
		if (iColumns <= 0 || iCount <= 0)
			return;

		const float flSpacing = GetStyle().ItemSpacing.x;
		const float flCellHeight = H::Draw.Scale(6) + flIconHeight + H::Draw.Scale(21);
		const int iRows = (iCount + iColumns - 1) / iColumns;
		ImDrawList* pDrawList = GetWindowDrawList();
		const ImVec2 vOrigin = GetCursorScreenPos();
		for (int i = 0; i < iCount; i++)
		{
			const ImVec2 vMin = vOrigin + ImVec2((i % iColumns) * (flCellWidth + flSpacing), (i / iColumns) * (flCellHeight + flSpacing));
			const ImVec2 vMax = vMin + ImVec2(flCellWidth, flCellHeight);
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Background1p5, 0.55f), H::Draw.Scale(5));
			FSkeletonRect(pDrawList, vMin + ImVec2(H::Draw.Scale(7), H::Draw.Scale(6)), vMin + ImVec2(flCellWidth - H::Draw.Scale(7), H::Draw.Scale(6) + flIconHeight), H::Draw.Scale(4));
			FSkeletonLine(pDrawList, vMin + ImVec2(H::Draw.Scale(12), H::Draw.Scale(6) + flIconHeight + H::Draw.Scale(7)), flCellWidth - H::Draw.Scale(24), H::Draw.Scale(8));
		}

		SetCursorPosY(flTop + iRows * (flCellHeight + flSpacing) - flSpacing);
		DebugDummy({ 0, std::min(GetContentRegionAvail().y, std::max(0.f, flHeight - (iRows * (flCellHeight + flSpacing) - flSpacing))) });
	}

	inline void FSkeletonList(float flTop, int iRows = 3)
	{
		FSkeletonRows(flTop + H::Draw.Scale(2), iRows, H::Draw.Scale(30), 0.f);
	}

	inline void FToolbarSearch(std::string& sSearch, int iButtons)
	{
		const float flWidth = GetWindowWidth() - GetStyle().WindowPadding.x * 2;
		FInputText("Search", sSearch, flWidth - (H::Draw.Scale(28) + H::Draw.Scale(4)) * iButtons);
	}
	inline bool FToolbarButton(const char* sIcon, const char* sTooltip)
	{
		SameLine(0, H::Draw.Scale(4));
		return FIconButton(sIcon, sTooltip, H::Draw.Scale(28));
	}

	inline void FMenuNote(const char* sText)
	{
		ImFont* pFont = F::Render.FontSmall;
		const ImVec2 vPos = GetCursorScreenPos();
		const float flHeight = H::Draw.Scale(20);
		GetWindowDrawList()->AddText(pFont, pFont->LegacySize, { vPos.x + H::Draw.Scale(10), floorf(vPos.y + (flHeight - pFont->LegacySize) / 2) }, FadeColor(MixColor(F::Render.Inactive, F::Render.Active, 0.3f)), sText);
		Dummy({ pFont->CalcTextSizeA(pFont->LegacySize, FLT_MAX, 0.f, sText).x + H::Draw.Scale(20), flHeight });
	}

	struct ProfileCard_t
	{
		bool m_bRemove = false;
		bool m_bMenu = false;
	};
	inline ProfileCard_t FProfileCard(ImTextureID pAvatar, const std::string& sName, const std::string& sDetail, const ImVec4& tColor, const std::string& sBadge, const char* sRemove, ImVec2 vPos, ImVec2 vSize, bool bLoading = false)
	{
		ProfileCard_t tReturn = {};
		const ImVec2 vMin = GetDrawPos() + vPos, vMax = vMin + vSize;
		ImDrawList* pDrawList = GetWindowDrawList();

		SetCursorPos(vPos);
		Dummy(vSize);
		const bool bWithin = IsWindowHovered() && IsMouseHoveringRect(vMin, vMax);
		const float flHover = Anim(GetID("##Card"), 0, bWithin || IsPopupOpen("##Menu") ? 1.f : 0.f);
		pDrawList->AddRectFilled(vMin, vMax, FadeColor(MixColor(F::Render.Background1p5, F::Render.Background1p5L, flHover)), H::Draw.Scale(6));

		const float flAvatar = H::Draw.Scale(32), flInset = floorf((vSize.y - flAvatar) / 2);
		const ImVec2 vAvatarMin = vMin + ImVec2(flInset, flInset), vAvatarMax = vAvatarMin + ImVec2(flAvatar, flAvatar);
		const float flAvatarFade = Reveal(GetID("##Avatar"), 0, pAvatar != static_cast<ImTextureID>(0), 12.f);
		if (bLoading || flAvatarFade < 0.999f)
			FSkeletonRect(pDrawList, vAvatarMin, vAvatarMax, H::Draw.Scale(5), 1.f - flAvatarFade);
		if (pAvatar)
			pDrawList->AddImageRounded(pAvatar, vAvatarMin, vAvatarMax, { 0, 0 }, { 1, 1 }, FadeColor(ImVec4(1, 1, 1, 1), flAvatarFade), H::Draw.Scale(5));
		else if (!bLoading)
		{
			pDrawList->AddRectFilled(vAvatarMin, vAvatarMax, FadeColor(tColor, 0.16f), H::Draw.Scale(5));
			char sInitial[2] = { '?', '\0' };
			for (char cChar : sName)
			{
				if (std::isalpha(static_cast<unsigned char>(cChar)))
				{
					sInitial[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(cChar)));
					break;
				}
			}
			ImFont* pFont = F::Render.FontBold;
			const ImVec2 vText = pFont->CalcTextSizeA(pFont->LegacySize, FLT_MAX, 0.f, sInitial);
			pDrawList->AddText(pFont, pFont->LegacySize, { floorf(vAvatarMin.x + (flAvatar - vText.x) / 2), floorf(vAvatarMin.y + (flAvatar - vText.y) / 2) }, FadeColor(tColor), sInitial);
		}

		ImFont* pSmall = F::Render.FontSmall;
		const float flTextX = vAvatarMax.x + H::Draw.Scale(10);
		const float flTextWidth = vMax.x - H::Draw.Scale(44) - flTextX;
		const float flLine2 = floorf(vMax.y - flInset - pSmall->LegacySize);
		pDrawList->AddText({ flTextX, floorf(vMin.y + flInset - H::Draw.Scale(1)) }, FadeColor(F::Render.Active), TruncateText(sName, int(flTextWidth)).c_str());
		const float flRoleIcon = H::Draw.Scale(12);
		pDrawList->AddText(F::Render.IconFont, flRoleIcon, { flTextX - H::Draw.Scale(1), floorf(flLine2 + (pSmall->LegacySize - flRoleIcon) / 2) + H::Draw.Scale(1) }, FadeColor(tColor), ICON_MD_LABEL);
		pDrawList->AddText(pSmall, pSmall->LegacySize, { flTextX + H::Draw.Scale(15), flLine2 }, FadeColor(MixColor(F::Render.Inactive, F::Render.Active, 0.4f)), TruncateText(sDetail, int(flTextWidth - H::Draw.Scale(15)), pSmall).c_str());

		if (bWithin)
		{
			SetCursorPos(vPos + ImVec2(vSize.x - H::Draw.Scale(36), floorf((vSize.y - H::Draw.Scale(26)) / 2)));
			tReturn.m_bRemove = FIconButton(ICON_MD_DELETE, sRemove, H::Draw.Scale(26));
		}
		else if (!sBadge.empty())
		{
			const ImVec2 vBadge = pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, sBadge.c_str());
			const ImVec2 vPillSize = { std::max(vBadge.x + H::Draw.Scale(12), H::Draw.Scale(22)), H::Draw.Scale(18) };
			const ImVec2 vPillMin = { floorf(vMax.x - H::Draw.Scale(12) - vPillSize.x), floorf((vMin.y + vMax.y - vPillSize.y) / 2) };
			pDrawList->AddRectFilled(vPillMin, vPillMin + vPillSize, FadeColor(tColor, 0.14f), vPillSize.y / 2);
			pDrawList->AddText(pSmall, pSmall->LegacySize, { floorf(vPillMin.x + (vPillSize.x - vBadge.x) / 2), floorf(vPillMin.y + (vPillSize.y - vBadge.y) / 2) }, FadeColor(tColor), sBadge.c_str());
		}

		tReturn.m_bMenu = bWithin && IsMouseReleased(ImGuiMouseButton_Right);
		return tReturn;
	}

	inline void DrawColorChip(ImDrawList* pDrawList, ImVec2 vMin, ImVec2 vMax, const ImVec4& tColor, const std::string& sName, bool bSelected, bool bEnabled, float flHover, float flNameWidth)
	{
		const float flRounding = H::Draw.Scale(6);
		const float flTint = (bEnabled ? 0.1f : 0.04f) + 0.06f * flHover + (bSelected ? 0.12f : 0.f);
		const ImVec4 tOpaque = { tColor.x, tColor.y, tColor.z, 1.f };
		pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Background1p5), flRounding);
		pDrawList->AddRectFilled(vMin, vMax, FadeColor(tOpaque, flTint), flRounding);
		const ImVec4 tText = bSelected ? F::Render.Active.Value : bEnabled ? ImLerp(tOpaque, F::Render.Active.Value, 0.55f) : MixColor(F::Render.Inactive, F::Render.Active, 0.35f);
		pDrawList->AddText({ vMin.x + H::Draw.Scale(12), floorf((vMin.y + vMax.y - GetFontSize()) / 2) }, FadeColor(tText), TruncateText(sName, int(flNameWidth)).c_str());
	}

	template <class T, class Fn>
	inline void FCardGrid(const std::vector<T>& vEntries, float flTop, Fn&& fDraw)
	{
		const float flPad = GetStyle().WindowPadding.x, flWidth = GetWindowWidth() - flPad * 2;
		const float flGap = H::Draw.Scale(8), flHeight = H::Draw.Scale(52);
		const int iColumns = std::clamp(int((flWidth + flGap) / H::Draw.Scale(250)), 1, 3);
		const float flCardWidth = floorf((flWidth - flGap * (iColumns - 1)) / iColumns);
		const float flStep = flHeight + flGap;
		const int iRows = int((vEntries.size() + iColumns - 1) / iColumns);

		const ImRect& tClip = GetCurrentWindow()->ClipRect;
		const float flOrigin = GetDrawPos().y + flTop;
		const int iFirst = std::max(int(floorf((tClip.Min.y - flOrigin) / flStep)) - 1, 0);
		const int iLast = std::min(int(ceilf((tClip.Max.y - flOrigin) / flStep)) + 1, iRows - 1);
		bool bStop = false;
		for (int y = iFirst; y <= iLast && !bStop; y++)
		{
			for (int x = 0; x < iColumns && !bStop; x++)
			{
				const size_t i = size_t(y) * iColumns + x;
				if (i < vEntries.size())
					bStop = fDraw(vEntries[i], ImVec2(flPad + (flCardWidth + flGap) * x, flTop + flStep * y), ImVec2(flCardWidth, flHeight));
			}
		}
		SetCursorPosY(flTop + flStep * iRows - flGap);
		DebugDummy({});
	}

	struct ListRow_t
	{
		ImVec2 m_vPos;
		ImVec2 m_vMin;
		ImVec2 m_vMax;
		bool m_bClicked;
		bool m_bWithin;
		bool m_bMenu;
		float m_flHover;
	};
	inline ListRow_t FBeginListRow(const char* sID, bool bSelected, float flIndent = 0.f)
	{
		ListRow_t tRow = {};
		const float flWidth = GetWindowWidth() - GetStyle().WindowPadding.x * 2 - flIndent, flHeight = H::Draw.Scale(30);
		tRow.m_vPos = GetCursorPos() + ImVec2(flIndent, 0);
		tRow.m_vMin = GetDrawPos() + tRow.m_vPos;
		tRow.m_vMax = tRow.m_vMin + ImVec2(flWidth, flHeight);

		SetCursorPos(tRow.m_vPos);
		SetNextItemAllowOverlap();
		tRow.m_bClicked = InvisibleButton(sID, { flWidth, flHeight });
		tRow.m_bWithin = IsWindowHovered() && IsMouseHoveringRect(tRow.m_vMin, tRow.m_vMax);
		tRow.m_bMenu = tRow.m_bWithin && IsMouseReleased(ImGuiMouseButton_Right);
		tRow.m_flHover = Anim(GetItemID(), 0, tRow.m_bWithin || IsPopupOpen("##Menu") ? 1.f : 0.f);
		if (tRow.m_bWithin)
			SetMouseCursor(ImGuiMouseCursor_Hand);

		ImDrawList* pDrawList = GetWindowDrawList();
		if (bSelected)
			pDrawList->AddRectFilled(tRow.m_vMin, tRow.m_vMax, FadeColor(F::Render.AccentDim), H::Draw.Scale(5));
		else if (tRow.m_flHover > 0.01f)
			pDrawList->AddRectFilled(tRow.m_vMin, tRow.m_vMax, FadeColor(F::Render.Background1p5, tRow.m_flHover), H::Draw.Scale(5));
		return tRow;
	}
	inline bool FListRowIcon(const ListRow_t& tRow, int iSlot, const char* sIcon, const char* sTooltip, ImVec4 tColor = { 1, 1, 1, -1 })
	{
		SetCursorPos(tRow.m_vPos + ImVec2(tRow.m_vMax.x - tRow.m_vMin.x - H::Draw.Scale(28) * (iSlot + 1) - H::Draw.Scale(2), floorf((tRow.m_vMax.y - tRow.m_vMin.y - H::Draw.Scale(26)) / 2)));
		return FIconButton(sIcon, sTooltip, H::Draw.Scale(26), tColor);
	}
	inline void FEndListRow(const ListRow_t& tRow)
	{
		SetCursorPos({ GetStyle().WindowPadding.x, tRow.m_vPos.y + tRow.m_vMax.y - tRow.m_vMin.y + H::Draw.Scale(2) });
	}
	inline bool FCreateRow(std::string& sName, const char* sPlaceholder, bool* pFolder)
	{
		const float flButton = H::Draw.Scale(28), flGap = H::Draw.Scale(4);
		const bool bEnter = FInputText(sPlaceholder, sName, GetWindowWidth() - GetStyle().WindowPadding.x * 2 - (flButton + flGap) * 2, ImGuiInputTextFlags_EnterReturnsTrue);
		bool bCreate = false;
		SameLine(0, flGap);
		PushDisabled(sName.empty());
		{
			bCreate = FIconButton(ICON_MD_ADD, "Create", flButton);
		}
		PopDisabled();
		SameLine(0, flGap);
		*pFolder = FIconButton(ICON_MD_FOLDER_OPEN, "Open folder", flButton);
		DebugDummy({ 0, H::Draw.Scale(8) });
		return !sName.empty() && (bCreate || bEnter);
	}

	inline bool FToggle(const char* sLabel, bool* pVar, int iFlags = FToggleEnum::None, bool* pHovered = nullptr)
	{
		const uint32_t uHash = FNV1A::Hash32Const(sLabel);

		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		const float flWidth = RowWidth(iFlags);
		const ImVec2 vOriginalPos = GetCursorPos(), vRowMin = GetDrawPos() + vOriginalPos;
		const float flBox = H::Draw.Scale(14);
		const auto vLines = RowLabel(sLabel, flWidth - flBox - H::Draw.Scale(12));
		const ImVec2 vSize = { flWidth, H::Draw.Scale(32 + 16 * (int(vLines.size()) - 1)) };

		PushID(sLabel);
		SetNextItemAllowOverlap();
		bool bReturn = InvisibleButton("##Toggle", vSize);
		PopID();
		const bool bHovered = IsItemHovered();
		if (pHovered)
			*pHovered = bHovered;

		if (Disabled)
			bReturn = false;
		else if (bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);
		if (bReturn)
			*pVar = !*pVar;

		const float flOn = Anim(uHash, 0, *pVar ? 1.f : 0.f, 18.f);
		const float flHover = Anim(uHash, 1, bHovered && !Disabled ? 1.f : 0.f);

		ImDrawList* pDrawList = GetWindowDrawList();
		DrawCheckbox(pDrawList, vRowMin + ImVec2(flWidth - flBox, floorf((vSize.y - flBox) / 2)), flOn, flHover, iFlags & FToggleEnum::PlainColor);
		DrawRowLabel(pDrawList, vRowMin, vSize.y, vLines, LabelColor(std::max(flOn, flHover)));

		LastRow = { vRowMin, vRowMin + vSize, vRowMin.x + flWidth - flBox - H::Draw.Scale(10), vSize.y };
		EndRow(vOriginalPos, vSize);

		if (Transparent || Disabled)
			PopStyleVar();

		return bReturn;
	}

	inline bool FToggle(const char* sLabel, int* pVar, int iBit, int iFlags = FToggleEnum::None, bool* pHovered = nullptr)
	{
		bool bActive = *pVar & iBit;
		if (FToggle(sLabel, &bActive, iFlags, pHovered))
		{
			if (bActive)
				*pVar |= iBit;
			else
				*pVar &= ~iBit;
			return true;
		}
		return false;
	}

	inline void DrawSliderTrack(ImDrawList* pDrawList, ImVec2 vDrawPos, ImVec2 vMins, ImVec2 vMaxs, float flPos1, float flPos2, bool bRange, float flHover, bool bDragging, bool bPlain = false)
	{
		const ImVec4 tFillColor = bPlain ? F::Render.Active.Value : F::Render.Accent.Value;
		const float flRounding = (vMaxs.y - vMins.y) / 2;
		const float flCenterY = (vMins.y + vMaxs.y) / 2;

		pDrawList->AddRectFilled(vDrawPos + vMins, vDrawPos + vMaxs, FadeColor(F::Render.Track), flRounding);
		const float flFillMin = bRange ? flPos1 : vMins.x, flFillMax = bRange ? flPos2 : flPos1;
		if (flFillMax - flFillMin > 0.5f)
			pDrawList->AddRectFilled(vDrawPos + ImVec2(flFillMin, vMins.y), vDrawPos + ImVec2(flFillMax, vMaxs.y), FadeColor(tFillColor), flRounding);

		const ImU32 uKnob = FadeColor(F::Render.Active);
		const float flRadius = H::Draw.Scale(4.5f + 1.f * std::max(flHover, bDragging ? 1.f : 0.f));
		pDrawList->AddCircleFilled(vDrawPos + ImVec2(flPos1, flCenterY), flRadius, uKnob, 20);
		if (bRange)
			pDrawList->AddCircleFilled(vDrawPos + ImVec2(flPos2, flCenterY), flRadius, uKnob, 20);
	}

	inline float StepValue(float flValue, int iDir, float flStep, float flMin, float flMax)
	{
		const float flNew = flValue + flStep * iDir;
		if (iDir < 0 && flNew < flMin && flValue >= flMin)
			return flMin;
		if (iDir > 0 && flNew > flMax && flValue <= flMax)
			return flMax;
		return flNew;
	}

	inline std::string IntFormat(const char* fmt)
	{
		std::string sFormat = fmt;
		for (const char* sFrom : { "%d", "%i" })
		{
			for (auto iFind = sFormat.find(sFrom); iFind != std::string::npos; iFind = sFormat.find(sFrom))
				sFormat.replace(iFind, 2, "%g");
		}
		return sFormat;
	}

	struct FieldLayout_t
	{
		ImVec2 m_vRowSize = {};
		ImVec2 m_vFieldPos = {};
		ImVec2 m_vFieldSize = {};
		float m_flLineHeight = 0.f;
		bool m_bTitle = false;
		bool m_bStacked = false;
	};
	inline FieldLayout_t FieldLayout(const char* sLabel, float flWidth, float flReserve = 0.f, float flFieldWidth = 0.f, float flFieldHeight = H::Draw.Scale(26))
	{
		FieldLayout_t tLayout = {};
		tLayout.m_bTitle = sLabel[0] != '#';
		if (!tLayout.m_bTitle)
		{
			tLayout.m_vFieldPos = { 0, H::Draw.Scale(3) };
			tLayout.m_vFieldSize = { flWidth - flReserve, flFieldHeight };
			tLayout.m_vRowSize = { flWidth, flFieldHeight + H::Draw.Scale(6) };
			tLayout.m_flLineHeight = tLayout.m_vRowSize.y;
			return tLayout;
		}

		const float flLabel = CalcTextSize(StripDoubleHash(sLabel).c_str()).x, flGap = H::Draw.Scale(14);
		float flField = flFieldWidth ? std::min(flFieldWidth, flWidth * 0.62f) : floorf(flWidth * 0.5f);
		if (flLabel + flGap + flReserve > flWidth - flField && !flFieldWidth)
			flField = flWidth - flLabel - flGap - flReserve;
		const float flMinField = flFieldWidth ? flField : std::max(H::Draw.Scale(118), flWidth * 0.34f);
		if (flField >= flMinField && flLabel + flGap + flReserve <= flWidth - flField)
		{
			tLayout.m_vRowSize = { flWidth, flFieldHeight + H::Draw.Scale(6) };
			tLayout.m_vFieldPos = { flWidth - flField, H::Draw.Scale(3) };
			tLayout.m_vFieldSize = { flField, flFieldHeight };
			tLayout.m_flLineHeight = tLayout.m_vRowSize.y;
			return tLayout;
		}

		tLayout.m_bStacked = true;
		tLayout.m_flLineHeight = H::Draw.Scale(22);
		tLayout.m_vFieldPos = { 0, tLayout.m_flLineHeight };
		tLayout.m_vFieldSize = { flWidth - flReserve, flFieldHeight };
		tLayout.m_vRowSize = { flWidth, tLayout.m_flLineHeight + flFieldHeight + H::Draw.Scale(4) };
		return tLayout;
	}
	inline bool FSliderCore(const char* sLabel, float* pVar1, float* pVar2, float flMin, float flMax, float flStep, const char* fmt, int iFlags, bool* pHovered, bool* pToggle, int iToggleFlags)
	{
		const uint32_t uHash = FNV1A::Hash32Const(sLabel);
		ImDrawList* pDrawList = GetWindowDrawList();
		const float flOriginal1 = *pVar1, flOriginal2 = pVar2 ? *pVar2 : 0.f;
		const bool bOriginalToggle = pToggle && *pToggle;

		static std::unordered_map<uint32_t, std::pair<float, float>> mStaticVars = {};
		if (!ActiveMap[uHash])
			mStaticVars[uHash] = { flOriginal1, flOriginal2 };
		float& flSVar1 = mStaticVars[uHash].first, &flSVar2 = mStaticVars[uHash].second;
		const bool bDragging = ActiveMap[uHash];

		PushID(sLabel);
		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		const float flWidth = RowWidth(iFlags);
		const ImVec2 vOriginalPos = GetCursorPos(), vRowMin = GetDrawPos() + vOriginalPos;
		const float flBox = H::Draw.Scale(14);
		const float flValueRight = pToggle ? flWidth - flBox - H::Draw.Scale(10) : flWidth;

		const FieldLayout_t tLayout = FieldLayout(sLabel, flWidth);
		const bool bInline = !tLayout.m_bStacked;
		const float flFieldX = tLayout.m_vFieldPos.x;

		static std::unordered_map<uint32_t, float> mEntryWidth = {};
		float& flEntryWidth = mEntryWidth[uHash];
		float flReserve = flEntryWidth;
		if (bInline)
		{
			const std::string sMin = pVar2 ? FormatText(fmt, flMin, flMin) : FormatText(fmt, flMin);
			const std::string sMax = pVar2 ? FormatText(fmt, flMax, flMax) : FormatText(fmt, flMax);
			flReserve = std::max({ flReserve, CalcTextSize(sMin.c_str()).x, CalcTextSize(sMax.c_str()).x, H::Draw.Scale(pVar2 ? 64 : 36) });
		}
		const auto vLines = bInline ? RowLabel(sLabel, std::max(flFieldX - H::Draw.Scale(14), 1.f)) : RowLabel(sLabel, flValueRight - flEntryWidth - H::Draw.Scale(pVar2 ? 12 : 34));
		const float flLabelHeight = bInline ? tLayout.m_vRowSize.y : H::Draw.Scale(22 + 16 * (int(vLines.size()) - 1));
		const ImVec2 vSize = bInline ? tLayout.m_vRowSize : ImVec2(flWidth, flLabelHeight + H::Draw.Scale(20));

		const bool bRowHovered = !Disabled && IsWindowHovered() && IsMouseWithin(vRowMin.x, vRowMin.y, vSize.x, vSize.y);
		const float flHover = Anim(uHash, 0, bRowHovered || bDragging ? 1.f : 0.f);

		float flOn = 1.f;
		bool bToggleHovered = false;
		if (pToggle)
		{
			const ImVec2 vBoxMin = vRowMin + ImVec2(flWidth - flBox, floorf((flLabelHeight - flBox) / 2));
			bToggleHovered = !Disabled && IsWindowHovered() && IsMouseWithin(vBoxMin.x - H::Draw.Scale(4), vBoxMin.y - H::Draw.Scale(4), flBox + H::Draw.Scale(8), flBox + H::Draw.Scale(8));
			if (bToggleHovered)
			{
				SetMouseCursor(ImGuiMouseCursor_Hand);
				if (IsMouseClicked(ImGuiMouseButton_Left))
					*pToggle = !*pToggle;
			}
			flOn = Anim(uHash, 4, *pToggle ? 1.f : 0.f, 18.f);
			DrawCheckbox(pDrawList, vBoxMin, flOn, bToggleHovered ? 1.f : 0.f, iToggleFlags & FToggleEnum::PlainColor);
		}
		const bool bSliderOff = pToggle && !*pToggle;
		if (bSliderOff)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		DrawRowLabel(pDrawList, vRowMin, flLabelHeight, vLines, LabelColor(pToggle ? std::max(flOn, flHover) : flHover));

		{
			const uint32_t uHash2 = FNV1A::Hash32Const(std::format("{}## Text", sLabel).c_str());

			static std::string sText, sInput;
			if (!ActiveMap[uHash2])
				sText = pVar2 ? FormatText(fmt, flSVar1, flSVar2) : FormatText(fmt, flSVar1);
			else
			{
				SetCursorPos({ -1000, vOriginalPos.y });
				SetKeyboardFocusHere();
				InputText("##SliderText", &sInput, ImGuiInputTextFlags_CharsDecimal);
				sText = sInput;

				const bool bEnter = U::KeyHandler.Pressed(VK_RETURN);
				if (bEnter)
				{
					try
					{
						const bool bVar1 = ActiveMap[uHash2] == 1;
						float& flVar = bVar1 ? flSVar1 : flSVar2;

						flVar = sText.length() ? std::stof(sText) : 0.f;
						if (pVar2)
						{
							if (bVar1)
								flVar = std::min(flVar, *pVar2 - flStep);
							else
								flVar = std::max(flVar, *pVar1 + flStep);
						}
						if (!(iFlags & FSliderEnum::Precision))
							flVar = flVar - fnmodf(flVar - flStep / 2, flStep) + flStep / 2;
						if (iFlags & FSliderEnum::Clamp)
							flVar = std::clamp(flVar, flMin, flMax);
						else if (iFlags & FSliderEnum::Min)
							flVar = std::max(flVar, flMin);
						else if (iFlags & FSliderEnum::Max)
							flVar = std::min(flVar, flMax);

						*pVar1 = flSVar1;
						if (pVar2)
							*pVar2 = flSVar2;
					}
					catch (...) {}
				}
				if (bEnter || IsMouseClicked(ImGuiMouseButton_Left) || U::KeyHandler.Pressed(VK_ESCAPE))
					ActiveMap[uHash2] = false;
			}

			const float flSteps = !pVar2 ? Anim(uHash, 5, bRowHovered && !bDragging && !ActiveMap[uHash2] ? 1.f : 0.f, 18.f) : 0.f;
			const float flTextWidth = CalcTextSize(sText.c_str()).x;
			flEntryWidth = flTextWidth;
			const float flTextX = floorf(vRowMin.x + flValueRight - flTextWidth - H::Draw.Scale(16) * flSteps);
			const float flTextY = floorf(vRowMin.y + (flLabelHeight - GetFontSize()) / 2);
			const bool bTextHovered = !Disabled && IsWindowHovered() && IsMouseWithin(flTextX - H::Draw.Scale(2), flTextY - H::Draw.Scale(2), flTextWidth + H::Draw.Scale(4), GetFontSize() + H::Draw.Scale(4));
			const ImVec4 tValue = ActiveMap[uHash2] || bDragging ? F::Render.Accent.Value : MixColor(F::Render.Inactive, F::Render.Active, bTextHovered ? 1.f : flHover * 0.4f);
			pDrawList->AddText({ flTextX, flTextY }, FadeColor(tValue), sText.c_str());
			if (!Disabled)
			{
				if (bTextHovered)
					SetMouseCursor(ImGuiMouseCursor_TextInput);
				if (ActiveMap[uHash2])
					pDrawList->AddRectFilled({ flTextX, flTextY + GetFontSize() + H::Draw.Scale(1) }, { flTextX + flTextWidth, flTextY + GetFontSize() + H::Draw.Scale(2) }, FadeColor(F::Render.Accent));
				else if (bTextHovered && IsMouseClicked(ImGuiMouseButton_Left))
				{
					float* pVar = !pVar2 || GetMousePos().x - flTextX < flTextWidth / 2 ? pVar1 : pVar2;
					sInput = std::format("{}", *pVar);
					ActiveMap[uHash2] = pVar == pVar1 ? 1 : 2;
				}
			}

			if (flSteps > 0.01f)
			{
				const float flCenterY = floorf(flTextY + GetFontSize() / 2) + H::Draw.Scale(0.5f);
				const float flHalf = H::Draw.Scale(3.5f), flThick = H::Draw.Scale(0.6f), flHit = H::Draw.Scale(6);
				for (int iDir : { -1, 1 })
				{
					const float flX = iDir < 0 ? flTextX - H::Draw.Scale(10) : vRowMin.x + flValueRight - H::Draw.Scale(4);
					const bool bStepHovered = !Disabled && IsWindowHovered() && IsMouseWithin(flX - flHit, flCenterY - flHit, flHit * 2, flHit * 2);
					const ImU32 uColor = FadeColor(MixColor(F::Render.Inactive, F::Render.Active, bStepHovered ? 1.f : 0.f), flSteps);
					pDrawList->AddRectFilled({ flX - flHalf, flCenterY - flThick }, { flX + flHalf, flCenterY + flThick }, uColor);
					if (iDir > 0)
						pDrawList->AddRectFilled({ flX - flThick, flCenterY - flHalf }, { flX + flThick, flCenterY + flHalf }, uColor);
					if (bStepHovered)
					{
						SetMouseCursor(ImGuiMouseCursor_Hand);
						if (IsMouseClicked(ImGuiMouseButton_Left) && !ActiveMap[uHash])
						{
							flSVar1 = StepValue(flSVar1, iDir, flStep, flMin, flMax);
							*pVar1 = flSVar1;
						}
					}
				}
			}
		}

		const float flInset = H::Draw.Scale(5), flTrackY = bInline ? floorf(flLabelHeight / 2) : flLabelHeight + H::Draw.Scale(10);
		const float flTrackMin = flFieldX + flInset;
		const float flTrackMax = bInline ? std::max(flValueRight - flReserve - H::Draw.Scale(pVar2 ? 14 : 34), flTrackMin + H::Draw.Scale(40)) : flWidth - flInset;
		const ImVec2 vMins = { flTrackMin, flTrackY - H::Draw.Scale(2) }, vMaxs = { flTrackMax, flTrackY + H::Draw.Scale(2) };
		const float flHitPad = H::Draw.Scale(7);
		const bool bWithin = !Disabled && IsWindowHovered() && IsMouseWithin(vRowMin.x + vMins.x - flInset, vRowMin.y + vMins.y - flHitPad, vMaxs.x - vMins.x + flInset * 2, (vMaxs.y - vMins.y) + flHitPad * 2);
		if (bWithin)
			SetMouseCursor(ImGuiMouseCursor_Hand);
		const float flMarkerHover = Anim(uHash, 1, bWithin || bDragging ? 1.f : 0.f);
		const float flPosSpeed = bDragging ? 45.f : 20.f;

		const float flMousePerc = (GetMousePos().x - (vRowMin.x + vMins.x)) / (vMaxs.x - vMins.x) + (flStep / 2) / (flMax - flMin);
		if (pVar2)
		{
			const float flLowerPos = vMins.x + (vMaxs.x - vMins.x) * Anim(uHash, 2, std::clamp((flSVar1 - flMin) / (flMax - flMin), 0.f, 1.f), flPosSpeed);
			const float flUpperPos = vMins.x + (vMaxs.x - vMins.x) * Anim(uHash, 3, std::clamp((flSVar2 - flMin) / (flMax - flMin), 0.f, 1.f), flPosSpeed);
			DrawSliderTrack(pDrawList, vRowMin, vMins, vMaxs, flLowerPos, flUpperPos, true, flMarkerHover, bDragging);

			if (!Disabled)
			{
				if (bWithin && !ActiveMap[uHash])
				{
					if (IsMouseClicked(ImGuiMouseButton_Left))
						ActiveMap[uHash] = GetMousePos().x - vRowMin.x < (flLowerPos + flUpperPos) / 2 ? 1 : 2;
				}
				else if (ActiveMap[uHash] && IsMouseDown(ImGuiMouseButton_Left))
				{
					const bool bVar1 = ActiveMap[uHash] == 1;
					float& flVar = bVar1 ? flSVar1 : flSVar2;
					flVar = flMin + (flMax - flMin) * flMousePerc;
					flVar = std::clamp(flVar - fnmodf(flVar, flStep), !bVar1 ? flSVar1 + flStep : flMin, bVar1 ? flSVar2 - flStep : flMax);
				}
				else
					ActiveMap[uHash] = false;

				if (iFlags & FSliderEnum::NoAutoUpdate ? !ActiveMap[uHash] : true)
				{
					*pVar1 = flSVar1;
					*pVar2 = flSVar2;
				}
			}
		}
		else
		{
			const float flPos = vMins.x + (vMaxs.x - vMins.x) * Anim(uHash, 2, std::clamp((flSVar1 - flMin) / (flMax - flMin), 0.f, 1.f), flPosSpeed);
			DrawSliderTrack(pDrawList, vRowMin, vMins, vMaxs, flPos, flPos, false, flMarkerHover, bDragging, iToggleFlags & FToggleEnum::PlainColor);

			if (!Disabled)
			{
				if (bWithin && !ActiveMap[uHash])
				{
					if (IsMouseClicked(ImGuiMouseButton_Left))
						ActiveMap[uHash] = 1;
				}
				else if (ActiveMap[uHash] && IsMouseDown(ImGuiMouseButton_Left))
				{
					flSVar1 = flMin + (flMax - flMin) * flMousePerc;
					flSVar1 = std::clamp(flSVar1 - fnmodf(flSVar1, flStep), flMin, flMax);
				}
				else
					ActiveMap[uHash] = false;

				if (iFlags & FSliderEnum::NoAutoUpdate ? !ActiveMap[uHash] : true)
					*pVar1 = flSVar1;
			}
		}

		if (bSliderOff)
			PopStyleVar();

		SetCursorPos(vOriginalPos);
		SetNextItemAllowOverlap();
		InvisibleButton("##Row", vSize);
		if (pHovered)
			*pHovered = bRowHovered || bToggleHovered;

		LastRow = { vRowMin, vRowMin + vSize, bInline && tLayout.m_bTitle ? vRowMin.x + flFieldX - H::Draw.Scale(8) : vRowMin.x + flValueRight - flEntryWidth - H::Draw.Scale(pVar2 ? 12 : 30), flLabelHeight };
		EndRow(vOriginalPos, vSize);

		if (Transparent || Disabled)
			PopStyleVar();
		PopID();

		return *pVar1 != flOriginal1 || pVar2 && *pVar2 != flOriginal2 || pToggle && *pToggle != bOriginalToggle;
	}

	inline bool FSlider(const char* sLabel, float* pVar1, float* pVar2, float flMin, float flMax, float flStep = 1.f, const char* fmt = "%g", int iFlags = FSliderEnum::None, bool* pHovered = nullptr)
	{
		return FSliderCore(sLabel, pVar1, pVar2, flMin, flMax, flStep, fmt, iFlags, pHovered, nullptr, FToggleEnum::None);
	}

	inline bool FSlider(const char* sLabel, int* pVar1, int* pVar2, int iMin, int iMax, int iStep = 1, const char* fmt = "%i", int iFlags = FSliderEnum::None, bool* pHovered = nullptr)
	{
		const std::string sFormat = IntFormat(fmt);
		float flRedir1 = float(*pVar1), flRedir2 = pVar2 ? float(*pVar2) : 0.f;
		bool bReturn = FSliderCore(sLabel, &flRedir1, pVar2 ? &flRedir2 : nullptr, float(iMin), float(iMax), float(iStep), sFormat.c_str(), iFlags, pHovered, nullptr, FToggleEnum::None);
		*pVar1 = int(flRedir1); if (pVar2) *pVar2 = int(flRedir2);
		return bReturn;
	}

	inline bool FSlider(const char* sLabel, float* pVar, float flMin, float flMax, float flStep = 1.f, const char* fmt = "%g", int iFlags = FSliderEnum::None, bool* pHovered = nullptr)
	{
		return FSlider(sLabel, pVar, nullptr, flMin, flMax, flStep, fmt, iFlags, pHovered);
	}

	inline bool FSlider(const char* sLabel, int* pVar, int iMin, int iMax, int iStep = 1, const char* fmt = "%i", int iFlags = FSliderEnum::None, bool* pHovered = nullptr)
	{
		return FSlider(sLabel, pVar, nullptr, iMin, iMax, iStep, fmt, iFlags, pHovered);
	}

	inline bool FToggleSlider(const char* sLabel, bool* pToggleVar, float* pSliderVar, float flMin, float flMax, float flStep = 1.f, const char* fmt = "%g", int iToggleFlags = FToggleEnum::None, int iSliderFlags = FSliderEnum::None, bool* pHovered = nullptr)
	{
		return FSliderCore(sLabel, pSliderVar, nullptr, flMin, flMax, flStep, fmt, iSliderFlags, pHovered, pToggleVar, iToggleFlags);
	}

	inline bool FToggleSlider(const char* sLabel, bool* pToggleVar, int* pSliderVar, int iMin, int iMax, int iStep = 1, const char* fmt = "%i", int iToggleFlags = FToggleEnum::None, int iSliderFlags = FSliderEnum::None, bool* pHovered = nullptr)
	{
		const std::string sFormat = IntFormat(fmt);
		float flRedir = float(*pSliderVar);
		bool bReturn = FSliderCore(sLabel, &flRedir, nullptr, float(iMin), float(iMax), float(iStep), sFormat.c_str(), iSliderFlags, pHovered, pToggleVar, iToggleFlags);
		*pSliderVar = int(flRedir);
		return bReturn;
	}

	struct OptionRow_t
	{
		ImVec2 m_vMin = {};
		ImVec2 m_vMax = {};
		bool m_bClicked = false;
		bool m_bHovered = false;
	};
	inline OptionRow_t FOptionRow(const char* sID, bool bSelected, ImGuiSelectableFlags iFlags = ImGuiSelectableFlags_None)
	{
		OptionRow_t tRow = {};

		PushStyleColor(ImGuiCol_Header, ImVec4());
		PushStyleColor(ImGuiCol_HeaderHovered, ImVec4());
		PushStyleColor(ImGuiCol_HeaderActive, ImVec4());
		tRow.m_bClicked = Selectable(sID, bSelected, iFlags, { 0, H::Draw.Scale(26) });
		PopStyleColor(3);

		tRow.m_vMin = GetItemRectMin();
		tRow.m_vMax = GetItemRectMax();
		tRow.m_bHovered = IsItemHovered();
		if (tRow.m_bHovered)
		{
			SetMouseCursor(ImGuiMouseCursor_Hand);
			const ImVec2 vInset = { H::Draw.Scale(4), H::Draw.Scale(1) };
			GetWindowDrawList()->AddRectFilled(tRow.m_vMin + vInset, tRow.m_vMax - vInset, FadeColor(F::Render.Active, IsMouseDown(ImGuiMouseButton_Left) ? 0.1f : 0.06f), H::Draw.Scale(4));
		}

		return tRow;
	}
	inline void FOptionText(const OptionRow_t& tRow, const char* sText, float flX, ImU32 uColor)
	{
		GetWindowDrawList()->AddText({ tRow.m_vMin.x + flX, tRow.m_vMin.y + floorf((tRow.m_vMax.y - tRow.m_vMin.y - GetTextLineHeight()) / 2) }, uColor, sText);
	}
	inline ImU32 OptionColor(const OptionRow_t& tRow, bool bSelected, bool bAccent)
	{
		if (bSelected && bAccent)
			return FadeColor(F::Render.Accent);
		return FadeColor(bSelected || tRow.m_bHovered ? F::Render.Active.Value : MixColor(F::Render.Inactive, F::Render.Active, 0.5f));
	}
	inline bool FOption(const char* sID, const char* sText, bool bSelected, bool bMulti, ImGuiSelectableFlags iFlags = ImGuiSelectableFlags_None)
	{
		auto tRow = FOptionRow(sID, bSelected, iFlags);
		if (bMulti)
			FCheckboxIcon({ tRow.m_vMin.x + H::Draw.Scale(12), floorf((tRow.m_vMin.y + tRow.m_vMax.y) / 2 - H::Draw.Scale(7)) }, bSelected);
		FOptionText(tRow, sText, H::Draw.Scale(bMulti ? 34 : 12), OptionColor(tRow, bSelected, !bMulti));
		return tRow.m_bClicked;
	}
	inline void FOptionDivider()
	{
		const ImVec2 vPos = GetCursorScreenPos();
		const float flWidth = GetContentRegionAvail().x;
		GetWindowDrawList()->AddRectFilled(vPos + ImVec2(H::Draw.Scale(12), H::Draw.Scale(4)), vPos + ImVec2(flWidth - H::Draw.Scale(12), H::Draw.Scale(4) + H::Draw.Scale()), FadeColor(F::Render.Background2));
		DebugDummy({ 0, H::Draw.Scale(9) });
	}

	inline void DrawFieldLabel(const char* sLabel, const FieldLayout_t& tLayout, ImVec2 vRowMin, float flHover)
	{
		if (!tLayout.m_bTitle)
			return;

		const std::string sText = StripDoubleHash(sLabel);
		const float flY = floorf(vRowMin.y + (tLayout.m_flLineHeight - GetFontSize()) / 2);
		GetWindowDrawList()->AddText({ vRowMin.x, flY }, LabelColor(flHover), tLayout.m_bStacked ? TruncateText(sText, int(tLayout.m_vRowSize.x)).c_str() : sText.c_str());
	}
	inline void DrawField(ImVec2 vMin, ImVec2 vMax, const std::string& sValue, float flHover, float flOpen, bool bChevron, bool bTyping = false)
	{
		ImDrawList* pDrawList = GetWindowDrawList();
		const float flPad = H::Draw.Scale(10);
		pDrawList->AddRectFilled(vMin, vMax, FadeColor(MixColor(F::Render.Background1p5, F::Render.Background1p5L, std::max(flHover, flOpen))), H::Draw.Scale(4));

		const float flTextWidth = vMax.x - vMin.x - flPad * 2 - (bChevron ? H::Draw.Scale(14) : 0.f);
		ImFont* pFont = GetFont();
		std::string sShown = sValue;
		if (CalcTextSize(sShown.c_str()).x > flTextWidth)
		{
			pFont = F::Render.FontSmall;
			sShown = TruncateText(sValue, int(flTextWidth), pFont);
		}
		const float flFontSize = pFont->LegacySize;
		const ImVec2 vValuePos = { vMin.x + flPad, floorf(vMin.y + (vMax.y - vMin.y - flFontSize) / 2) };
		const ImU32 uText = FadeColor(F::Render.Active);
		pDrawList->AddText(pFont, flFontSize, vValuePos, uText, sShown.c_str());
		if (bTyping && fmodf(float(GetTime()), 1.f) < 0.55f)
		{
			const float flCaretX = floorf(vValuePos.x + FCalcTextSize(sShown.c_str(), pFont).x) + H::Draw.Scale(1);
			pDrawList->AddRectFilled({ flCaretX, vValuePos.y }, { flCaretX + H::Draw.Scale(), vValuePos.y + flFontSize }, FadeColor(F::Render.Accent));
		}

		if (bChevron)
		{
			const ImU32 uChevron = FadeColor(MixColor(F::Render.Inactive, F::Render.Active, std::max(flHover, flOpen)));
			const float flChevronX = vMax.x - flPad - H::Draw.Scale(4), flChevronY = floorf((vMin.y + vMax.y) / 2) + H::Draw.Scale(0.5f);
			const float flHalf = H::Draw.Scale(3.5f), flRise = H::Draw.Scale(2.f) * (1.f - 2.f * flOpen);
			pDrawList->AddLine({ flChevronX - flHalf, flChevronY - flRise }, { flChevronX, flChevronY + flRise }, uChevron, H::Draw.Scale(1.4f));
			pDrawList->AddLine({ flChevronX, flChevronY + flRise }, { flChevronX + flHalf, flChevronY - flRise }, uChevron, H::Draw.Scale(1.4f));
		}
	}

	struct FieldRow_t
	{
		FieldLayout_t m_tLayout = {};
		ImVec2 m_vOriginalPos = {};
		ImVec2 m_vRowMin = {};
		ImVec2 m_vFieldMin = {};
		ImVec2 m_vFieldMax = {};
		ImGuiID m_uPopup = 0;
		bool m_bHovered = false;
		bool m_bOpen = false;
	};
	inline FieldRow_t BeginFieldRow(const char* sLabel, int iFlags, int iSizeOffset, float flFieldWidth = 0.f)
	{
		FieldRow_t tRow = {};
		const float flWidth = RowWidth(iFlags);
		tRow.m_tLayout = FieldLayout(sLabel, flWidth, iSizeOffset < 0 ? H::Draw.Scale(-iSizeOffset) : 0.f, flFieldWidth);
		tRow.m_vOriginalPos = GetCursorPos();
		tRow.m_vRowMin = GetDrawPos() + tRow.m_vOriginalPos;
		tRow.m_vFieldMin = tRow.m_vRowMin + tRow.m_tLayout.m_vFieldPos;
		tRow.m_vFieldMax = tRow.m_vFieldMin + tRow.m_tLayout.m_vFieldSize;

		PushID(sLabel);
		tRow.m_uPopup = GetID("##FieldPopup");
		SetNextItemAllowOverlap();
		const bool bClicked = InvisibleButton("##Field", tRow.m_tLayout.m_vRowSize);
		tRow.m_bHovered = IsItemHovered() && !Disabled;
		if (tRow.m_bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);
		if (bClicked && !Disabled && !IsPopupOpen(tRow.m_uPopup, ImGuiPopupFlags_None))
			OpenPopupEx(tRow.m_uPopup, ImGuiPopupFlags_None);
		PopID();
		return tRow;
	}
	inline bool BeginFieldPopup(FieldRow_t& tRow)
	{
		PushStyleVar(ImGuiStyleVar_FramePadding, { 0, 0 });
		PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, H::Draw.Scale(4) });
		PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.f);
		tRow.m_bOpen = BeginComboPopup(tRow.m_uPopup, ImRect(tRow.m_vFieldMin, tRow.m_vFieldMax + ImVec2(0, H::Draw.Scale(4))), ImGuiComboFlags_HeightLarge);
		PopStyleVar(3);
		if (tRow.m_bOpen)
			PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
		return tRow.m_bOpen;
	}
	inline void EndFieldPopup()
	{
		PopStyleVar();
		EndCombo();
	}
	inline void EndFieldRow(const char* sLabel, FieldRow_t& tRow, const std::string& sValue, bool bChevron, bool bTyping = false)
	{
		const ImGuiID uID = GetID(sLabel);
		const float flHover = Anim(uID, 0, tRow.m_bHovered ? 1.f : 0.f);
		const float flOpen = Anim(uID, 1, tRow.m_bOpen ? 1.f : 0.f, 22.f);
		DrawFieldLabel(sLabel, tRow.m_tLayout, tRow.m_vRowMin, std::max(flHover, flOpen));
		DrawField(tRow.m_vFieldMin, tRow.m_vFieldMax, sValue, flHover, flOpen, bChevron, bTyping);

		const auto& tLayout = tRow.m_tLayout;
		const float flAccessoryX = tLayout.m_bStacked || !tLayout.m_bTitle ? tRow.m_vRowMin.x + tLayout.m_vRowSize.x : tRow.m_vFieldMin.x - H::Draw.Scale(8);
		LastRow = { tRow.m_vRowMin, tRow.m_vRowMin + tLayout.m_vRowSize, flAccessoryX, tLayout.m_flLineHeight };
		EndRow(tRow.m_vOriginalPos, tLayout.m_vRowSize);
	}

	inline bool FDropdown(const char* sLabel, int* pVar, std::vector<const char*> vEntries, std::vector<int> vValues = {}, int iFlags = FDropdownEnum::None, int iSizeOffset = 0, const char* sDefaultPreview = "None", bool* pHovered = nullptr, int* pModified = nullptr, std::initializer_list<std::tuple<int, const char*, int>> group_headers = {})
	{
		bool bReturn = false;

		if (vValues.empty())
		{
			int i = 0; for (auto& sEntry : vEntries)
			{
				if (IsDividerEntry(sEntry))
					continue;

				vValues.push_back(iFlags & FDropdownEnum::Multi ? 1 << i : i);
				i++;
			}
		}
		if (vValues.size() != std::count_if(vEntries.begin(), vEntries.end(), [](auto sEntry) { return !IsDividerEntry(sEntry); }))
		{
			IM_ASSERT_USER_ERROR(0, "FDropdown() vValues size mismatch to vEntries.");
			return bReturn;
		}

		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		if (!(iFlags & FDropdownEnum::NoSanitization) && !vValues.empty())
		{
			if (!(iFlags & FDropdownEnum::Multi))
			{
				if (std::find(vValues.begin(), vValues.end(), *pVar) == vValues.end())
					*pVar = vValues.front();
			}
			else
			{
				for (int i = 0; i < MAX_GROUPS; i++)
				{
					bool bFound = *pVar & (1 << i) && std::find(vValues.begin(), vValues.end(), (1 << i)) != vValues.end();
					if (!bFound)
						*pVar &= ~(1 << i);
				}
			}
		}

		std::vector<std::string> vSelected = {};
		{
			size_t i = 0; for (auto& iValue : vValues)
			{
				while (IsDividerEntry(vEntries[i]))
					i++;

				if (iFlags & FDropdownEnum::Multi ? (*pVar & iValue) != 0 : *pVar == iValue)
					vSelected.push_back(StripDoubleHash(vEntries[i]));
				i++;
			}
		}

		auto tRow = BeginFieldRow(sLabel, iFlags, iSizeOffset);
		if (pHovered)
			*pHovered = tRow.m_bHovered;
		if (BeginFieldPopup(tRow))
		{
			DebugDummy({ 0, H::Draw.Scale(2) });

			int i = 0; for (auto& sEntry : vEntries)
			{
				if (IsDividerEntry(sEntry))
				{
					FOptionDivider();
					continue;
				}

				for (const auto& group_header : group_headers)
				{
					if (i != std::get<0>(group_header))
						continue;

					const char* group_label = std::get<1>(group_header);
					int group_mask = std::get<2>(group_header);
					bool group_active = (*pVar & group_mask) == group_mask;
					auto tOption = FOptionRow(std::format("##{}{}", group_label, i).c_str(), group_active, ImGuiSelectableFlags_DontClosePopups);
					if (tOption.m_bClicked)
					{
						if (group_active)
							*pVar &= ~group_mask;
						else
							*pVar |= group_mask;
						bReturn = true;
					}
					FCheckboxIcon({ tOption.m_vMin.x + H::Draw.Scale(12), floorf((tOption.m_vMin.y + tOption.m_vMax.y) / 2 - H::Draw.Scale(7)) }, group_active);
					PushFont(F::Render.FontBold);
					FOptionText(tOption, group_label, H::Draw.Scale(34), FadeColor(group_active || tOption.m_bHovered ? F::Render.Active : F::Render.Inactive));
					PopFont();
				}

				const std::string sStripped = StripDoubleHash(sEntry);
				if (iFlags & FDropdownEnum::Multi)
				{
					bool bFlagActive = *pVar & vValues[i];
					if (FOption(std::format("##{}{}", sEntry, i).c_str(), sStripped.c_str(), bFlagActive, true, ImGuiSelectableFlags_DontClosePopups))
					{
						if (bFlagActive)
							*pVar &= ~vValues[i];
						else
							*pVar |= vValues[i];
						bReturn = true;
					}
				}
				else
				{
					const ImVec2 vOriginalPos2 = GetCursorPos();
					const bool bModifiable = iFlags & FDropdownEnum::Modifiable;
					if (bModifiable)
					{
						SetCursorPos(vOriginalPos2 + ImVec2(H::Draw.Scale(6), H::Draw.Scale(1)));
						if (IconButton(vValues[i] == -1 ? ICON_MD_ADD_CIRCLE : ICON_MD_REMOVE_CIRCLE, H::Draw.Scale(24)) && pModified)
							*pModified = vValues[i];
						SetCursorPos(vOriginalPos2);
					}

					const bool bSelected = *pVar == vValues[i];
					auto tOption = FOptionRow(std::format("##{}{}", sEntry, i).c_str(), bSelected);
					if (tOption.m_bClicked)
						*pVar = vValues[i], bReturn = true;
					FOptionText(tOption, sStripped.c_str(), H::Draw.Scale(bModifiable ? 36 : 12), OptionColor(tOption, bSelected, true));
				}
				i++;
			}

			DebugDummy({ 0, H::Draw.Scale(2) });
			EndFieldPopup();
		}

		const float flValueWidth = tRow.m_tLayout.m_vFieldSize.x - H::Draw.Scale(34);
		const std::string sPreview = iFlags & FDropdownEnum::Multi ? ListPreview(vSelected, flValueWidth, sDefaultPreview) : !vSelected.empty() ? vSelected.front() : sDefaultPreview;
		EndFieldRow(sLabel, tRow, sPreview, true);

		if (Transparent || Disabled)
			PopStyleVar();

		return bReturn;
	}

	struct SDropdownBinding_t
	{
		std::string m_sCurrent;
		std::function<bool(const std::string&)> m_fCanCommit;
		std::function<void(const std::string&)> m_fCommit;
	};
	inline bool FSDropdownCore(const char* sLabel, const std::vector<const char*>& vEntries, int iFlags, int iSizeOffset, bool* pHovered, const SDropdownBinding_t& tBinding)
	{
		auto uHash = FNV1A::Hash32Const(sLabel);
		bool bReturn = false;

		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		const bool bFree = vEntries.empty();
		auto tRow = BeginFieldRow(sLabel, iFlags, iSizeOffset);
		if (pHovered)
			*pHovered = tRow.m_bHovered;
		if (tRow.m_bHovered)
			SetMouseCursor(ImGuiMouseCursor_TextInput);

		static std::string sPreview = "", sInput = "", sTab = "\n";
		static unsigned int uActiveHash = 0;
		if (bFree)
			PushStyleColor(ImGuiCol_PopupBg, {});
		const bool bOpen = BeginFieldPopup(tRow);
		if (bFree)
			PopStyleColor();
		if (bOpen)
		{
			if (!ActiveMap[uHash] || uActiveHash != uHash)
			{
				sPreview = sInput = "";
				sTab = "\n";
				uActiveHash = uHash;
			}

			ActiveMap[uHash] = true;

			ImVec2 vOriginalPos2 = GetCursorPos();
			SetCursorPos({ -1000, bFree ? -100 : GetScrollY() });
			if (!IsAnyItemActive())
				SetKeyboardFocusHere();
			bool bEnter = InputText("##FSDropdown", &sInput, ImGuiInputTextFlags_EnterReturnsTrue);
			if (sInput != sTab)
			{
				sPreview = sInput;
				sTab = "\n";
			}
			SetCursorPos(vOriginalPos2);

			auto uPreviewHash = FNV1A::Hash32(sPreview.c_str());
			std::deque<std::string> vValid = {};
			{
				std::string sSearch = sInput;
				std::transform(sSearch.begin(), sSearch.end(), sSearch.begin(), ::tolower);
				for (auto& sEntry : vEntries)
				{
					if (IsDividerEntry(sEntry))
					{
						vValid.push_back(sEntry);
						continue;
					}

					std::string sEntryLower = sEntry;
					std::transform(sEntryLower.begin(), sEntryLower.end(), sEntryLower.begin(), ::tolower);
					if (sEntryLower.find(sSearch) != std::string::npos)
						vValid.push_back(sEntry);
				}
			}

			if (!vValid.empty() && IsDividerEntry(vValid.front().c_str()))
				vValid.pop_front();
			if (!vValid.empty() && IsDividerEntry(vValid.back().c_str()))
				vValid.pop_back();
			if (!vValid.empty())
			{
				if (U::KeyHandler.Pressed(VK_TAB))
				{
					int iIndex = -1;
					for (int i = 0; i < vValid.size(); i++)
					{
						if (uPreviewHash == FNV1A::Hash32(vValid[i].c_str()))
							iIndex = i;
					}
					if (iIndex == -1 && U::KeyHandler.Down(VK_SHIFT))
						iIndex = 0;
					while (true)
					{
						iIndex += !U::KeyHandler.Down(VK_SHIFT) ? 1 : -1;
						if (iIndex < 0)
							iIndex += int(vValid.size());
						else if (iIndex >= vValid.size())
							iIndex -= int(vValid.size());
						if (IsDividerEntry(vValid[iIndex].c_str()))
							continue;
						sPreview = vValid[iIndex];
						sTab = sInput;
						break;
					}
				}

				DebugDummy({ 0, H::Draw.Scale(2) });

				bool bDivider = false;
				for (int i = 0; i < vValid.size(); i++)
				{
					auto& sEntry = vValid[i];
					if (IsDividerEntry(sEntry.c_str()))
					{
						if (!bDivider)
							FOptionDivider();
						bDivider = true;
						continue;
					}
					else
						bDivider = false;

					if (bEnter && !(iFlags & FSDropdownEnum::Custom))
					{
						tBinding.m_fCommit(sEntry); bReturn = true;
						CloseCurrentPopup(); break;
					}

					if (FOption(std::format("##{}{}", sEntry, i).c_str(), sEntry.c_str(), tBinding.m_sCurrent == sEntry, false))
						tBinding.m_fCommit(sEntry), bReturn = true;
				}

				DebugDummy({ 0, H::Draw.Scale(2) });
			}

			if ((bEnter || iFlags & FSDropdownEnum::AutoUpdate) && (iFlags & FSDropdownEnum::Custom || bFree) && tBinding.m_fCanCommit(sPreview))
			{
				tBinding.m_fCommit(sPreview);
				bReturn = true;
			}
			if (bEnter || U::KeyHandler.Down(VK_ESCAPE))
				CloseCurrentPopup();

			EndFieldPopup();
		}
		else
			ActiveMap[uHash] = false;

		EndFieldRow(sLabel, tRow, ActiveMap[uHash] ? sPreview : tBinding.m_sCurrent, !bFree, ActiveMap[uHash]);

		if (Transparent || Disabled)
			PopStyleVar();

		return bReturn;
	}

	inline bool FSDropdown(const char* sLabel, std::string* pVar, std::vector<const char*> vEntries = {}, int iFlags = FDropdownEnum::None, int iSizeOffset = 0, bool* pHovered = nullptr)
	{
		return FSDropdownCore(sLabel, vEntries, iFlags, iSizeOffset, pHovered, {
			*pVar,
			[](const std::string&) { return true; },
			[&](const std::string& sValue) { *pVar = sValue; }
		});
	}

	inline bool FSDropdown(const char* sLabel, int* pVar, std::vector<const char*> vEntries = {}, int iFlags = FDropdownEnum::None, int iSizeOffset = 0, bool* pHovered = nullptr)
	{
		return FSDropdownCore(sLabel, vEntries, iFlags, iSizeOffset, pHovered, {
			std::to_string(*pVar),
			[](const std::string& sValue)
			{
				int iValue = 0;
				const auto [pEnd, eError] = std::from_chars(sValue.data(), sValue.data() + sValue.size(), iValue);
				return !sValue.empty() && eError == std::errc{} && pEnd == sValue.data() + sValue.size();
			},
			[&](const std::string& sValue) { *pVar = atoi(sValue.c_str()); }
		});
	}

	inline bool ColorPicker(const char* sLabel, Color_t* pColor, bool bTooltip = true, ImVec2 vSize = { H::Draw.Scale(22), H::Draw.Scale(12) })
	{
		bool bReturn = false;
		PushID(sLabel);

		const ImVec2 vMin = GetCursorScreenPos(), vMax = vMin + vSize;
		const bool bClicked = InvisibleButton("##Swatch", vSize);
		const bool bHovered = !Disabled && IsItemHovered();
		if (bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);

		ImDrawList* pDrawList = GetWindowDrawList();
		const float flRounding = H::Draw.Scale(3);
		const ImVec4 tColor = ColorByteToVec(*pColor);
		if (tColor.w < 1.f)
			RenderColorRectWithAlphaCheckerboard(pDrawList, vMin, vMax, GetColorU32(tColor), floorf(vSize.y / 2), {}, flRounding);
		else
			pDrawList->AddRectFilled(vMin, vMax, GetColorU32(tColor), flRounding);
		const float flHover = Anim(GetItemID(), 0, bHovered || IsPopupOpen("##Picker") ? 1.f : 0.f);
		if (flHover > 0.01f)
			pDrawList->AddRect(vMin - ImVec2(H::Draw.Scale(2), H::Draw.Scale(2)), vMax + ImVec2(H::Draw.Scale(2), H::Draw.Scale(2)), FadeColor(F::Render.Active, 0.35f * flHover), flRounding + H::Draw.Scale(2), ImDrawFlags_None, H::Draw.Scale());

		if (bClicked && !Disabled)
			OpenPopup("##Picker");

		PushStyleVar(ImGuiStyleVar_Alpha, 1.f);
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(10), H::Draw.Scale(10) });
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), H::Draw.Scale(8) });
		PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, { H::Draw.Scale(8), H::Draw.Scale(8) });
		PushStyleVar(ImGuiStyleVar_FramePadding, { H::Draw.Scale(3), H::Draw.Scale(3) });
		PushStyleVar(ImGuiStyleVar_GrabMinSize, H::Draw.Scale(10));
		SetNextWindowPos({ vMin.x, vMax.y + H::Draw.Scale(6) }, ImGuiCond_Appearing);
		if (FBeginPopup("##Picker"))
		{
			ImVec4 tEdit = tColor;
			PushItemWidth(H::Draw.Scale(196));
			if (ColorPicker4("##Picker", &tEdit.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreviewHalf))
			{
				*pColor = ColorVecToByte(tEdit);
				bReturn = true;
			}
			PopItemWidth();

			static std::string sHex = "";
			const ImGuiID uHexID = GetID("##Hex##Hex");
			if (GetActiveID() != uHexID)
				sHex = std::format("{:02X}{:02X}{:02X}{:02X}", pColor->r, pColor->g, pColor->b, pColor->a);
			PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
			if (FInputText("Hex##Hex", sHex, H::Draw.Scale(196), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_AutoSelectAll))
			{
				unsigned int uValue = 0;
				const auto [pEnd, eError] = std::from_chars(sHex.data(), sHex.data() + sHex.size(), uValue, 16);
				if (eError == std::errc{} && (sHex.size() == 6 || sHex.size() == 8))
				{
					if (sHex.size() == 6)
						uValue = uValue << 8 | pColor->a;
					*pColor = { byte(uValue >> 24), byte(uValue >> 16), byte(uValue >> 8), byte(uValue) };
					bReturn = true;
				}
			}
			PopStyleVar();

			EndPopup();
		}
		PopStyleVar(6);
		PopID();

		if (bTooltip && bHovered)
		{
			ImVec2 vAnchor = vMin, vAnchorSize = vSize;
			FTooltip(StripDoubleHash(sLabel).c_str(), true, 300, F::Render.FontSmall, &vAnchor, &vAnchorSize);
		}

		return bReturn;
	}

	inline bool FColorPicker(const char* sLabel, Color_t* pColor, int iFlags = FColorPickerEnum::None, ImVec2 vOffset = {}, ImVec2 vSize = { H::Draw.Scale(22), H::Draw.Scale(12) }, ImVec2 vIconOffset = {}, bool* pHovered = nullptr)
	{
		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		const ImVec2 vSwatch = { H::Draw.Scale(22), H::Draw.Scale(12) };
		bool bReturn = false;
		if (iFlags & (FColorPickerEnum::Left | FColorPickerEnum::Right | FColorPickerEnum::Full))
		{
			const float flWidth = RowWidth(iFlags & FColorPickerEnum::Full ? 0 : iFlags);
			const ImVec2 vOriginalPos = GetCursorPos(), vRowMin = GetDrawPos() + vOriginalPos;
			const auto vLines = RowLabel(sLabel, flWidth - vSwatch.x - H::Draw.Scale(12));
			const ImVec2 vRowSize = { flWidth, H::Draw.Scale(32 + 16 * (int(vLines.size()) - 1)) };

			const bool bRowHovered = !Disabled && IsWindowHovered() && IsMouseWithin(vRowMin.x, vRowMin.y, vRowSize.x, vRowSize.y);
			DrawRowLabel(GetWindowDrawList(), vRowMin, vRowSize.y, vLines, LabelColor(Anim(GetID(sLabel), 0, bRowHovered ? 1.f : 0.f)));

			SetCursorPos(vOriginalPos + ImVec2(flWidth - vSwatch.x, floorf((vRowSize.y - vSwatch.y) / 2)));
			bReturn = ColorPicker(sLabel, pColor, iFlags & FColorPickerEnum::Tooltip, vSwatch);
			if (pHovered)
				*pHovered = bRowHovered;

			LastRow = { vRowMin, vRowMin + vRowSize, vRowMin.x + flWidth - vSwatch.x - H::Draw.Scale(8), vRowSize.y };
			SetCursorPos(vOriginalPos);
			PushID(sLabel);
			SetNextItemAllowOverlap();
			InvisibleButton("##ColorRow", vRowSize);
			PopID();
			EndRow(vOriginalPos, vRowSize);
		}
		else
		{
			const ImVec2 vOriginalPos = GetCursorPos();
			SetCursorScreenPos(TakeAccessory(vSwatch.x, vSwatch.y));
			bReturn = ColorPicker(sLabel, pColor, !(iFlags & FColorPickerEnum::NoTooltip), vSwatch);
			if (pHovered)
				*pHovered = IsItemHovered();
			SetCursorPos(vOriginalPos);
		}

		if (Transparent || Disabled)
			PopStyleVar();

		return bReturn;
	}

	inline void FKeybind(const char* sLabel, int& iOutput, int iFlags = FKeybindEnum::None, std::vector<int> vIgnore = { Vars::Menu::PrimaryKey[DEFAULT_BIND], Vars::Menu::SecondaryKey[DEFAULT_BIND] }, ImVec2 vSize = { 0, 30 }, int iSizeOffset = 0, bool* pHovered = nullptr)
	{
		const ImGuiID uId = GetID(sLabel);
		PushID(sLabel);

		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		const bool bListening = GetActiveID() == uId;
		const std::string sKey = bListening ? "Press a key" : U::KeyHandler.String(iOutput);
		const float flChip = std::max(CalcTextSize(sKey.c_str()).x + H::Draw.Scale(28), H::Draw.Scale(84));

		const float flWidth = vSize.x ? H::Draw.Scale(vSize.x) + H::Draw.Scale(iSizeOffset) : RowWidth(iFlags) + H::Draw.Scale(iSizeOffset);
		if (vSize.x && iFlags & FButtonEnum::SameLine)
			SameLine();
		const FieldLayout_t tLayout = FieldLayout(sLabel, flWidth, 0.f, vSize.x ? flChip : 0.f);
		const ImVec2 vOriginalPos = GetCursorPos(), vRowMin = GetDrawPos() + vOriginalPos;
		const ImVec2 vFieldMin = vRowMin + tLayout.m_vFieldPos, vFieldMax = vFieldMin + tLayout.m_vFieldSize;

		SetNextItemAllowOverlap();
		const bool bClicked = InvisibleButton("##Key", tLayout.m_vRowSize);
		const bool bHovered = IsItemHovered() && !Disabled;
		if (pHovered)
			*pHovered = bHovered;
		if (bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);

		if (bListening)
		{
			F::Menu.m_bInKeybind = true;

			if (bHovered && IsMouseReleased(ImGuiMouseButton_Left))
				ClearActiveID();
			else
			{
				SetActiveID(uId, GetCurrentWindow());

				for (short iKey = 1; iKey < 255; iKey++)
				{
					bool bValid = U::KeyHandler.Pressed(byte(iKey));
					if (!bValid)
						continue;

					bValid = vIgnore.empty() || std::find(vIgnore.begin(), vIgnore.end(), iKey) == vIgnore.end();
					if (!bValid)
						continue;

					switch (iKey)
					{
					case VK_LBUTTON:
						bValid = !bHovered; break;
					case VK_SHIFT:
					case VK_LSHIFT:
					case VK_RSHIFT:
					case VK_CONTROL:
					case VK_LCONTROL:
					case VK_RCONTROL:
					case VK_MENU:
					case VK_LMENU:
					case VK_RMENU:
						bValid = iKey != iOutput; break;
					}
					if (!bValid)
						continue;

					switch (iKey)
					{
					case VK_ESCAPE:
						if (iFlags & FKeybindEnum::AllowNone)
						{
							iOutput = 0x0;
							break;
						}
						[[fallthrough]];
					default:
						iOutput = iKey;
					}
					ClearActiveID();
					break;
				}

				GetCurrentContext()->ActiveIdAllowOverlap = true;
			}
		}
		else if (bClicked && !Disabled)
			SetActiveID(uId, GetCurrentWindow());

		const float flHover = Anim(uId, 0, bHovered ? 1.f : 0.f);
		const float flListen = Anim(uId, 1, GetActiveID() == uId ? 1.f : 0.f, 22.f);
		DrawFieldLabel(sLabel, tLayout, vRowMin, std::max(flHover, flListen));

		ImDrawList* pDrawList = GetWindowDrawList();
		pDrawList->AddRectFilled(vFieldMin, vFieldMax, FadeColor(MixColor(F::Render.Background1p5, F::Render.Background1p5L, flHover)), H::Draw.Scale(4));
		if (flListen > 0.01f)
			pDrawList->AddRectFilled(vFieldMin, vFieldMax, FadeColor(F::Render.AccentDim, flListen), H::Draw.Scale(4));
		const ImVec2 vText = CalcTextSize(sKey.c_str());
		const float flTextX = vSize.x ? floorf((vFieldMin.x + vFieldMax.x - vText.x) / 2) : vFieldMin.x + H::Draw.Scale(10);
		pDrawList->AddText({ flTextX, floorf((vFieldMin.y + vFieldMax.y - vText.y) / 2) }, FadeColor(ImLerp(F::Render.Active.Value, F::Render.Accent.Value, flListen)), sKey.c_str());
		if (!vSize.x)
		{
			const float flIcon = H::Draw.Scale(14);
			pDrawList->AddText(F::Render.IconFont, flIcon, { vFieldMax.x - H::Draw.Scale(10) - flIcon, floorf((vFieldMin.y + vFieldMax.y - flIcon) / 2) }, FadeColor(MixColor(F::Render.Inactive, F::Render.Active, std::max(flHover, flListen) * 0.6f)), ICON_MD_KEYBOARD);
		}

		LastRow = { vRowMin, vRowMin + tLayout.m_vRowSize, tLayout.m_bStacked || !tLayout.m_bTitle ? vRowMin.x + flWidth : vFieldMin.x - H::Draw.Scale(8), tLayout.m_flLineHeight };
		EndRow(vOriginalPos, tLayout.m_vRowSize);

		if (Transparent || Disabled)
			PopStyleVar();
		PopID();
	}
}
