#pragma once

#include "Render.h"
#include "Easings/Easings.h"
#include <ImGui/imgui_internal.h>

struct IndicatorAnim_t
{
	float m_flAnim = 0.f;

	float Update(bool bVisible, bool& bOpening, float flDuration = 0.20f)
	{
		const float flDelta = ImGui::GetIO().DeltaTime > 0.f ? ImGui::GetIO().DeltaTime : (1.f / 60.f);
		if (bVisible)
		{
			bOpening = true;
			m_flAnim = std::min(1.0f, m_flAnim + flDelta / flDuration);
		}
		else
		{
			bOpening = false;
			m_flAnim = std::max(0.0f, m_flAnim - flDelta / flDuration);
		}
		return m_flAnim;
	}

	bool ShouldDraw(bool bVisible) const
	{
		return bVisible || m_flAnim > 0.0001f;
	}
};

static inline void TransformIndicatorVertices(
	ImDrawList* pDrawList,
	int iVtxStart,
	int iVtxEnd,
	int iCmdStart,
	int iCmdEnd,
	float flAnim,
	bool bOpening,
	ImVec2 vDisplay,
	ImVec2 vExplicitCenter = { -FLT_MAX, -FLT_MAX })
{
	if (!pDrawList || iVtxEnd <= iVtxStart || flAnim >= 1.0f)
		return;

	const float flT = std::clamp(flAnim, 0.0f, 1.0f);
	float flScaleX = 0.f, flScaleY = 0.f;
	if (bOpening)
	{
		const float flEaseY = Ease::OutCubic(flT);
		const float flEaseX = Ease::OutCubic(powf(flT, 1.35f));
		const float flPop = 0.04f * sinf(flT * 3.14159265f);
		flScaleY = flEaseY * (1.0f + flPop);
		flScaleX = flEaseX * (1.0f + flPop);
	}
	else
	{
		const float flEaseY = Ease::InCubic(flT);
		const float flEaseX = Ease::InCubic(powf(flT, 1.35f));
		flScaleY = flEaseY;
		flScaleX = flEaseX;
	}
	flScaleX = std::max(0.0001f, flScaleX);
	flScaleY = std::max(0.0001f, flScaleY);
	const float flAlpha = std::clamp(flT * (bOpening ? 1.5f : 1.2f), 0.0f, 1.0f);

	ImVec2 vCenter = vExplicitCenter;
	if (vCenter.x == -FLT_MAX)
	{
		ImVec2 vMin(FLT_MAX, FLT_MAX), vMax(-FLT_MAX, -FLT_MAX);
		for (int i = iVtxStart; i < iVtxEnd; i++)
		{
			const auto& pos = pDrawList->VtxBuffer[i].pos;
			vMin.x = std::min(vMin.x, pos.x);
			vMin.y = std::min(vMin.y, pos.y);
			vMax.x = std::max(vMax.x, pos.x);
			vMax.y = std::max(vMax.y, pos.y);
		}
		vCenter = ImVec2((vMin.x + vMax.x) * 0.5f, (vMin.y + vMax.y) * 0.5f);
	}

	for (int i = iVtxStart; i < iVtxEnd; i++)
	{
		auto& vtx = pDrawList->VtxBuffer[i];
		vtx.pos.x = vCenter.x + (vtx.pos.x - vCenter.x) * flScaleX;
		vtx.pos.y = vCenter.y + (vtx.pos.y - vCenter.y) * flScaleY;

		if (flAlpha < 0.999f)
		{
			const ImU32 a = (vtx.col >> IM_COL32_A_SHIFT) & 0xFF;
			const ImU32 new_a = static_cast<ImU32>(a * flAlpha);
			vtx.col = (vtx.col & ~IM_COL32_A_MASK) | (new_a << IM_COL32_A_SHIFT);
		}
	}

	for (int i = iCmdStart; i < iCmdEnd; i++)
	{
		auto& cmd = pDrawList->CmdBuffer[i];
		float x1 = std::clamp(vCenter.x + (cmd.ClipRect.x - vCenter.x) * flScaleX, 0.f, vDisplay.x);
		float y1 = std::clamp(vCenter.y + (cmd.ClipRect.y - vCenter.y) * flScaleY, 0.f, vDisplay.y);
		float x2 = std::clamp(vCenter.x + (cmd.ClipRect.z - vCenter.x) * flScaleX, 0.f, vDisplay.x);
		float y2 = std::clamp(vCenter.y + (cmd.ClipRect.w - vCenter.y) * flScaleY, 0.f, vDisplay.y);
		cmd.ClipRect = ImVec4(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));
	}
}

static inline void TransformWindowDrawList(
	ImGuiWindow* pWindow,
	float flAnim,
	bool bOpening,
	ImVec2 vDisplay)
{
	if (!pWindow || !pWindow->DrawList || flAnim >= 1.0f)
		return;

	if (pWindow->DrawList->_Splitter._Count > 1)
		pWindow->DrawList->ChannelsMerge();

	const ImVec2 vCenter = ImVec2(pWindow->Pos.x + pWindow->Size.x * 0.5f, pWindow->Pos.y + pWindow->Size.y * 0.5f);
	TransformIndicatorVertices(
		pWindow->DrawList,
		0,
		pWindow->DrawList->VtxBuffer.Size,
		0,
		pWindow->DrawList->CmdBuffer.Size,
		flAnim,
		bOpening,
		vDisplay,
		vCenter);
}
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

static inline ImU32 ColorToU32(const Color_t& tColor)
{
	return IM_COL32(tColor.r, tColor.g, tColor.b, tColor.a);
}

static inline ImVec2 GetIndicatorTextPos(float x, float y, const char* sText, EAlign eAlign)
{
	ImVec2 vTextSize = ImGui::CalcTextSize(sText);
	switch (eAlign)
	{
	case ALIGN_TOP:
		x -= vTextSize.x / 2.f;
		break;
	case ALIGN_TOPRIGHT:
		x -= vTextSize.x;
		break;
	case ALIGN_LEFT:
		y -= vTextSize.y / 2.f;
		break;
	case ALIGN_CENTER:
		x -= vTextSize.x / 2.f;
		y -= vTextSize.y / 2.f;
		break;
	case ALIGN_RIGHT:
		x -= vTextSize.x;
		y -= vTextSize.y / 2.f;
		break;
	case ALIGN_BOTTOMLEFT:
		y -= vTextSize.y;
		break;
	case ALIGN_BOTTOM:
		x -= vTextSize.x / 2.f;
		y -= vTextSize.y;
		break;
	case ALIGN_BOTTOMRIGHT:
		x -= vTextSize.x;
		y -= vTextSize.y;
		break;
	default:
		break;
	}
	return { x, y };
}

static inline void DrawIndicatorText(ImDrawList* pDrawList, float x, float y, const Color_t& tColor, const Color_t& tOutline, EAlign eAlign, const char* sText)
{
	if (!sText || sText[0] == '\0')
		return;

	const ImVec2 vPos = GetIndicatorTextPos(x, y, sText, eAlign);
	if (!Vars::Menu::CheapText.Value)
	{
		Color_t tOutlineColor = tOutline;
		tOutlineColor.a *= Math::RemapVal(tOutlineColor.Brightness(), 0, 255, 0.5f, 0.1f);
		if (tOutlineColor.a)
		{
			const ImU32 uOutline = ColorToU32(tOutlineColor);
			const float flOutline = H::Draw.Scale(1.f);
			pDrawList->AddText({ vPos.x - flOutline, vPos.y }, uOutline, sText);
			pDrawList->AddText({ vPos.x, vPos.y - flOutline }, uOutline, sText);
			pDrawList->AddText({ vPos.x + flOutline, vPos.y }, uOutline, sText);
			pDrawList->AddText({ vPos.x, vPos.y + flOutline }, uOutline, sText);
			pDrawList->AddText({ vPos.x - flOutline, vPos.y - flOutline }, uOutline, sText);
			pDrawList->AddText({ vPos.x + flOutline, vPos.y + flOutline }, uOutline, sText);
			pDrawList->AddText({ vPos.x - flOutline, vPos.y + flOutline }, uOutline, sText);
			pDrawList->AddText({ vPos.x + flOutline, vPos.y - flOutline }, uOutline, sText);
		}
	}

	pDrawList->AddText(vPos, ColorToU32(tColor), sText);
}

static inline void DrawIndicatorText(ImDrawList* pDrawList, float x, float y, const Color_t& tColor, const Color_t& tOutline, EAlign eAlign, const std::string& sText)
{
	DrawIndicatorText(pDrawList, x, y, tColor, tOutline, eAlign, sText.c_str());
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
	int iBarWidth = static_cast<int>(std::ceil(flPanelWidth * flClampedProgress));
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
