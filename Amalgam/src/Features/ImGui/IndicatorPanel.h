#pragma once

#include "Render.h"
#include <algorithm>
#include <cfloat>

static inline ImU32 ColorToU32(const Color_t& tColor)
{
	return IM_COL32(tColor.r, tColor.g, tColor.b, tColor.a);
}

static inline void DrawIndicatorPanel(
	ImDrawList* pDrawList,
	const ImVec2& vPanelPos,
	float flPanelWidth,
	float flPanelHeight,
	const char* sLeftText,
	const char* sRightText,
	const Color_t& tLeftColor,
	const Color_t& tRightColor,
	const Color_t& tBarColor,
	float flProgress,
	bool bDrawFooter = false,
	const char* sFooterText = "Not Ready")
{
	const float flBarHeight = H::Draw.Scale(3.f);
	const float flTextBoxHeight = flPanelHeight - flBarHeight;
	const ImVec2 vPanelSize = { flPanelWidth, flPanelHeight };
	const ImU32 uBackground = ColorToU32(Color_t(0, 0, 0, 180));
	const ImU32 uBarBackground = ColorToU32(Color_t(0, 0, 0, 180));
	const ImU32 uLeft = ColorToU32(tLeftColor);
	const ImU32 uRight = ColorToU32(tRightColor);
	const ImU32 uBar = ColorToU32(tBarColor);

	pDrawList->AddRectFilled(vPanelPos, ImVec2(vPanelPos.x + vPanelSize.x, vPanelPos.y + vPanelSize.y), uBackground, H::Draw.Scale(3.f));
	pDrawList->AddRectFilled(
		ImVec2(vPanelPos.x, vPanelPos.y + flTextBoxHeight),
		ImVec2(vPanelPos.x + flPanelWidth, vPanelPos.y + flPanelHeight),
		uBarBackground,
		H::Draw.Scale(3.f));

	float flClampedProgress = std::clamp(flProgress, 0.f, 1.f);
	int iBarWidth = static_cast<int>(flPanelWidth * flClampedProgress);
	if (iBarWidth > 0)
	{
		ImVec4 vBarColor = ImGui::ColorConvertU32ToFloat4(uBar);
		for (int i = 0; i < iBarWidth; i++)
		{
			float flBlend = iBarWidth > 1 ? static_cast<float>(i) / static_cast<float>(iBarWidth - 1) : 1.f;
			ImVec4 vLineColor =
			{
				vBarColor.x * flBlend,
				vBarColor.y * flBlend,
				vBarColor.z * flBlend,
				vBarColor.w
			};
			pDrawList->AddLine(
				ImVec2(vPanelPos.x + static_cast<float>(i), vPanelPos.y + flTextBoxHeight),
				ImVec2(vPanelPos.x + static_cast<float>(i), vPanelPos.y + flPanelHeight),
				ImGui::ColorConvertFloat4ToU32(vLineColor),
				1.f);
		}
	}

	const float flFontSize = ImGui::GetFontSize();
	const float flLeftTextY = vPanelPos.y + ((flTextBoxHeight - flFontSize) * 0.5f);
	const float flLeftTextX = vPanelPos.x + H::Draw.Scale(5.f);
	pDrawList->AddText(ImVec2(flLeftTextX, flLeftTextY), uLeft, sLeftText);

	if (sRightText && sRightText[0] != '\0')
	{
		ImVec2 vRightTextSize = ImGui::CalcTextSize(sRightText);
		const float flRightTextX = vPanelPos.x + flPanelWidth - H::Draw.Scale(5.f) - vRightTextSize.x;
		pDrawList->AddText(ImVec2(flRightTextX, flLeftTextY), uRight, sRightText);
	}

	if (bDrawFooter && sFooterText && sFooterText[0] != '\0')
	{
		ImVec2 vFooterTextSize = ImGui::CalcTextSize(sFooterText);
		const float flFooterTextX = vPanelPos.x + (flPanelWidth - vFooterTextSize.x) * 0.5f;
		pDrawList->AddText(ImVec2(flFooterTextX, vPanelPos.y + flPanelHeight + H::Draw.Scale(2.f)), uLeft, sFooterText);
	}
}
