#pragma once
#include "Widgets.h"
#include "../Easings/Easings.h"
#include <unordered_set>

struct NavPage_t
{
	const char* m_sLabel;
	const char* m_sIcon;
	std::vector<const char*> m_vSubs;
};

struct MenuShell_t
{
	const char* m_sTitle = "";
	const char* m_sEditingBind = nullptr;
	const char* m_sBuild = nullptr;
	const char* m_sVariant = nullptr;
	float m_flAnim = 1.f;
	bool m_bIsOpen = true;
	std::function<void(int, int)> m_fPage;
	std::function<void(const std::string&)> m_fSearch;
	std::function<void()> m_fStopEditingBind;
};

namespace Shell
{
	using namespace ImGui;

	inline void TransformDrawList(ImDrawList* pDrawList, ImVec2 vCenter, ImVec2 vScale, float flAlpha, ImVec2 vDisplay)
	{
		if (!pDrawList)
			return;

		if (pDrawList->_Splitter._Count > 1)
			pDrawList->ChannelsMerge();

		for (auto& vtx : pDrawList->VtxBuffer)
		{
			vtx.pos.x = vCenter.x + (vtx.pos.x - vCenter.x) * vScale.x;
			vtx.pos.y = vCenter.y + (vtx.pos.y - vCenter.y) * vScale.y;

			if (flAlpha < 0.999f)
			{
				const ImU32 a = (vtx.col >> IM_COL32_A_SHIFT) & 0xFF;
				const ImU32 new_a = static_cast<ImU32>(a * flAlpha);
				vtx.col = (vtx.col & ~IM_COL32_A_MASK) | (new_a << IM_COL32_A_SHIFT);
			}
		}

		for (auto& cmd : pDrawList->CmdBuffer)
		{
			float x1 = std::clamp(vCenter.x + (cmd.ClipRect.x - vCenter.x) * vScale.x, 0.f, vDisplay.x);
			float y1 = std::clamp(vCenter.y + (cmd.ClipRect.y - vCenter.y) * vScale.y, 0.f, vDisplay.y);
			float x2 = std::clamp(vCenter.x + (cmd.ClipRect.z - vCenter.x) * vScale.x, 0.f, vDisplay.x);
			float y2 = std::clamp(vCenter.y + (cmd.ClipRect.w - vCenter.y) * vScale.y, 0.f, vDisplay.y);
			cmd.ClipRect = ImVec4(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));
		}
	}

	inline int Page = 0;
	inline std::vector<int> Subs = {};
	inline std::string SearchText = "";

	enum ETabStyle { TabStyleFull, TabStyleLabel, TabStyleIcon };
	inline float TopTabWidth(const NavPage_t& tPage, int iStyle)
	{
		const float flPad = H::Draw.Scale(12), flIcon = F::Render.IconFont->LegacySize;
		switch (iStyle)
		{
		case TabStyleFull: return flIcon + H::Draw.Scale(8) + CalcTextSize(tPage.m_sLabel).x + flPad * 2;
		case TabStyleLabel: return CalcTextSize(tPage.m_sLabel).x + flPad * 2;
		default: return flIcon + flPad * 2;
		}
	}
	inline bool TopTab(const NavPage_t& tPage, bool bActive, int iStyle, float flHeight)
	{
		ImDrawList* pDrawList = GetWindowDrawList();
		const float flWidth = TopTabWidth(tPage, iStyle), flPad = H::Draw.Scale(12);
		const ImVec2 vMin = GetCursorScreenPos(), vMax = vMin + ImVec2(flWidth, flHeight);

		PushID(tPage.m_sLabel);
		const bool bClicked = InvisibleButton("##Tab", { flWidth, flHeight });
		PopID();
		const bool bHovered = IsItemHovered();
		const ImGuiID uID = GetItemID();
		const float flHover = Anim(uID, 0, bHovered ? 1.f : 0.f), flActive = Anim(uID, 1, bActive ? 1.f : 0.f, 18.f);
		if (bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);
		if (iStyle == TabStyleIcon)
			FTooltip(tPage.m_sLabel, bHovered);

		const float flRounding = H::Draw.Scale(6);
		if (flHover > 0.01f && flActive < 1.f)
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Active, 0.04f * flHover * (1.f - flActive)), flRounding);
		if (flActive > 0.01f)
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.AccentDim, flActive), flRounding);

		const float flIcon = F::Render.IconFont->LegacySize;
		if (iStyle != TabStyleLabel)
		{
			const ImVec4 tIcon = ImLerp(MixColor(F::Render.Inactive, F::Render.Active, flHover * 0.6f), F::Render.Accent.Value, flActive);
			pDrawList->AddText(F::Render.IconFont, flIcon, { vMin.x + flPad, floorf(vMin.y + (flHeight - flIcon) / 2) }, FadeColor(tIcon), tPage.m_sIcon);
		}
		if (iStyle != TabStyleIcon)
		{
			const ImVec4 tText = ImLerp(MixColor(F::Render.Inactive, F::Render.Active, 0.45f + flHover * 0.55f), F::Render.Active.Value, flActive);
			pDrawList->AddText({ vMin.x + flPad + (iStyle == TabStyleFull ? flIcon + H::Draw.Scale(8) : 0.f), floorf(vMin.y + (flHeight - GetFontSize()) / 2) }, FadeColor(tText), tPage.m_sLabel);
		}

		return bClicked;
	}

	inline bool SideTab(const char* sLabel, bool bActive, float flWidth)
	{
		ImDrawList* pDrawList = GetWindowDrawList();
		const float flHeight = H::Draw.Scale(32);
		const ImVec2 vMin = GetCursorScreenPos(), vMax = vMin + ImVec2(flWidth, flHeight);

		PushID(sLabel);
		const bool bClicked = InvisibleButton("##Sub", { flWidth, flHeight });
		PopID();
		const bool bHovered = IsItemHovered();
		const ImGuiID uID = GetItemID();
		const float flHover = Anim(uID, 0, bHovered ? 1.f : 0.f), flActive = Anim(uID, 1, bActive ? 1.f : 0.f, 18.f);
		if (bHovered)
			SetMouseCursor(ImGuiMouseCursor_Hand);

		const float flRounding = H::Draw.Scale(6);
		if (flHover > 0.01f && flActive < 1.f)
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Active, 0.04f * flHover * (1.f - flActive)), flRounding);
		if (flActive > 0.01f)
			pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Background1p5, flActive), flRounding);

		const ImVec4 tText = ImLerp(MixColor(F::Render.Inactive, F::Render.Active, 0.45f + flHover * 0.55f), F::Render.Active.Value, flActive);
		pDrawList->AddText({ vMin.x + H::Draw.Scale(12), floorf(vMin.y + (flHeight - GetFontSize()) / 2) }, FadeColor(tText), TruncateText(sLabel, int(flWidth - H::Draw.Scale(20))).c_str());

		return bClicked;
	}

	inline bool SearchField(std::string& sSearch, float flWidth, float flHeight, bool bFocus)
	{
		ImDrawList* pDrawList = GetWindowDrawList();
		const ImVec2 vPos = GetCursorPos();
		const ImVec2 vMin = GetDrawPos() + vPos, vMax = vMin + ImVec2(flWidth, flHeight);
		const float flIconPad = H::Draw.Scale(30);

		const ImGuiID uID = GetID("##Search");
		const bool bHovered = IsWindowHovered() && IsMouseWithin(vMin.x, vMin.y, flWidth, flHeight);
		const float flHover = Anim(uID, 0, bHovered || GetActiveID() == uID ? 1.f : 0.f);
		pDrawList->AddRectFilled(vMin, vMax, FadeColor(MixColor(F::Render.Background1p5, F::Render.Background1p5L, flHover)), H::Draw.Scale(6));
		const float flIcon = F::Render.IconFont->LegacySize;
		pDrawList->AddText(F::Render.IconFont, flIcon, { vMin.x + H::Draw.Scale(9), floorf(vMin.y + (flHeight - flIcon) / 2) }, FadeColor(F::Render.Inactive), ICON_MD_SEARCH);

		bool bClear = false;
		if (!sSearch.empty())
		{
			SetCursorPos(vPos + ImVec2(flWidth - flHeight, 0));
			bClear = IconButton(ICON_MD_CLOSE, flHeight);
		}

		SetCursorPos(vPos + ImVec2(flIconPad, 0));
		PushStyleVar(ImGuiStyleVar_FramePadding, { 0, floorf((flHeight - GetTextLineHeight()) / 2) });
		PushStyleColor(ImGuiCol_FrameBg, ImVec4());
		PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4());
		PushStyleColor(ImGuiCol_FrameBgActive, ImVec4());
		PushStyleColor(ImGuiCol_Text, F::Render.Active.Value);
		PushItemWidth(flWidth - flIconPad - (sSearch.empty() ? H::Draw.Scale(8) : flHeight));
		if (bFocus)
			SetKeyboardFocusHere();
		bool bChanged = InputText("##Search", &sSearch, ImGuiInputTextFlags_NoKeyboardNavigate);
		const bool bActive = IsItemActive();
		PopItemWidth();
		PopStyleColor(4);
		PopStyleVar();
		if (IsItemHovered())
			SetMouseCursor(ImGuiMouseCursor_TextInput);

		if (bClear || bActive && U::KeyHandler.Pressed(VK_ESCAPE))
		{
			sSearch.clear();
			bChanged = true;
			if (bActive)
				ClearActiveID();
		}

		if (sSearch.empty() && !bActive)
			pDrawList->AddText({ vMin.x + flIconPad, floorf(vMin.y + (flHeight - GetTextLineHeight()) / 2) }, FadeColor(F::Render.Inactive), "Search");

		SetCursorPos(vPos);
		return bChanged;
	}

	inline void Draw(const std::vector<NavPage_t>& vPages, const MenuShell_t& tShell)
	{
		auto ClampMenuPosition = [](const ImVec2& vPosition, const ImVec2& vSize, const ImVec2& vDisplaySize)
		{
			return ImVec2(
				std::clamp(vPosition.x, 0.f, std::max(0.f, vDisplaySize.x - vSize.x)),
				std::clamp(vPosition.y, 0.f, std::max(0.f, vDisplaySize.y - vSize.y))
			);
		};

		static bool bSetPosition = false;
		static bool bDraggingMenu = false;
		static bool bReposition = false;
		static ImVec2 vMenuTargetPos = {};
		static ImVec2 vMenuDragOffset = {};
		const ImVec2 vDisplay = GetIO().DisplaySize;
		const ImVec2 vMinMenuSize = { std::min(H::Draw.Scale(780), vDisplay.x - H::Draw.Scale(16)), std::min(H::Draw.Scale(460), vDisplay.y - H::Draw.Scale(16)) };
		const ImVec2 vDefaultMenuSize = { std::min(H::Draw.Scale(1020), vDisplay.x - H::Draw.Scale(24)), std::min(H::Draw.Scale(640), vDisplay.y - H::Draw.Scale(24)) };
		static ImVec2 vMenuSize = vDefaultMenuSize;
		if (!bSetPosition)
		{
			vMenuTargetPos = ClampMenuPosition((vDisplay - vDefaultMenuSize) / 2, vDefaultMenuSize, vDisplay);
			SetNextWindowPos(vMenuTargetPos, ImGuiCond_Always);
			bSetPosition = true;
		}
		if (bDraggingMenu)
		{
			vMenuTargetPos = ClampMenuPosition(GetMousePos() - vMenuDragOffset, vMenuSize, vDisplay);
			SetNextWindowPos(vMenuTargetPos, ImGuiCond_Always);
		}
		else if (bReposition)
		{
			SetNextWindowPos(vMenuTargetPos, ImGuiCond_Always);
			bReposition = false;
		}
		SetNextWindowSize(vDefaultMenuSize, ImGuiCond_FirstUseEver);
		SetNextWindowSizeConstraints(vMinMenuSize, { std::max(vMinMenuSize.x, vDisplay.x), std::max(vMinMenuSize.y, vDisplay.y) });

		ImGuiWindowFlags iWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove;
		if (tShell.m_flAnim < 1.0f)
			iWindowFlags |= ImGuiWindowFlags_NoInputs;

		if (!Begin("Main", nullptr, iWindowFlags))
			return End();

		const ImVec2 vWindowSize = GetWindowSize();
		vMenuSize = vWindowSize;
		const ImVec2 vDrawPos = GetDrawPos();
		const ImVec2 vMousePos = GetMousePos();
		ImDrawList* pDrawList = GetWindowDrawList();

		if (const ImVec2 vClamped = ClampMenuPosition(vDrawPos, vWindowSize, vDisplay); vClamped != vDrawPos && !bDraggingMenu)
		{
			vMenuTargetPos = vClamped;
			bReposition = true;
		}

		int& iPage = Page;
		auto& vSubs = Subs;
		auto& sSearch = SearchText;
		vSubs.resize(vPages.size());
		iPage = std::clamp(iPage, 0, int(vPages.size() - 1));
		auto& tPage = vPages[iPage];
		int& iSub = vSubs[iPage];
		iSub = std::clamp(iSub, 0, int(tPage.m_vSubs.size() - 1));
		const bool bSearch = !sSearch.empty();

		const float flRounding = H::Draw.Scale(8);
		const float flTopHeight = H::Draw.Scale(48);
		const float flFooterHeight = H::Draw.Scale(24);
		const float flSideWidth = std::clamp(floorf(vWindowSize.x * 0.16f), H::Draw.Scale(140), H::Draw.Scale(170));
		const float flSidePad = H::Draw.Scale(10);
		const float flContentX = flSideWidth + H::Draw.Scale(10);
		const float flBodyBottom = vWindowSize.y - flFooterHeight;

		auto fSelect = [&](int iNewPage, int iNewSub)
		{
			if (iPage != iNewPage || vSubs[iNewPage] != iNewSub || !sSearch.empty())
				mLastHeights.clear();
			iPage = iNewPage;
			vSubs[iNewPage] = iNewSub;
			sSearch.clear();
		};

		pDrawList->AddRectFilled(vDrawPos, vDrawPos + vWindowSize, F::Render.Background1, flRounding);
		pDrawList->AddRectFilled(vDrawPos, vDrawPos + ImVec2(vWindowSize.x, flTopHeight), F::Render.Background0, flRounding, ImDrawFlags_RoundCornersTop);
		pDrawList->AddRectFilled(vDrawPos + ImVec2(0, flTopHeight), vDrawPos + ImVec2(flSideWidth, flBodyBottom), F::Render.Sidebar);
		pDrawList->AddRectFilled(vDrawPos + ImVec2(0, flBodyBottom), vDrawPos + vWindowSize, F::Render.Background0, flRounding, ImDrawFlags_RoundCornersBottom);

		{
			ImFont* pFont = F::Render.FontLarge;
			const float flTitleX = H::Draw.Scale(18);
			const std::string sTitle = TruncateText(tShell.m_sTitle, int(flSideWidth - flTitleX - H::Draw.Scale(8)), pFont);
			pDrawList->AddText(pFont, pFont->LegacySize, vDrawPos + ImVec2(flTitleX, floorf((flTopHeight - pFont->LegacySize) / 2)), F::Render.Active, sTitle.c_str());

			const float flSearchWidth = std::clamp(floorf(vWindowSize.x * 0.2f), H::Draw.Scale(150), H::Draw.Scale(220)), flSearchHeight = H::Draw.Scale(30);
			const float flTabsX = flContentX, flTabsRight = vWindowSize.x - flSearchWidth - H::Draw.Scale(24);
			int iStyle = TabStyleFull;
			for (; iStyle < TabStyleIcon; iStyle++)
			{
				float flTotal = 0.f;
				for (auto& tNavPage : vPages)
					flTotal += TopTabWidth(tNavPage, iStyle) + H::Draw.Scale(4);
				if (flTabsX + flTotal <= flTabsRight)
					break;
			}

			const float flTabHeight = H::Draw.Scale(32);
			float flX = flTabsX;
			for (int i = 0; i < vPages.size(); i++)
			{
				SetCursorPos({ flX, floorf((flTopHeight - flTabHeight) / 2) });
				if (TopTab(vPages[i], i == iPage && !bSearch, iStyle, flTabHeight))
					fSelect(i, vSubs[i]);
				flX += GetItemRectSize().x + H::Draw.Scale(4);
			}

			SetCursorPos({ vWindowSize.x - flSearchWidth - H::Draw.Scale(12), floorf((flTopHeight - flSearchHeight) / 2) });
			const bool bFocusSearch = U::KeyHandler.Down(VK_CONTROL) && U::KeyHandler.Pressed('F');
			if (SearchField(sSearch, flSearchWidth, flSearchHeight, bFocusSearch))
				mLastHeights.clear();
		}

		{
			ImFont* pSmall = F::Render.FontSmall;
			float flY = flTopHeight + H::Draw.Scale(10);

			if (!bSearch)
			{
				for (int i = 0; i < tPage.m_vSubs.size(); i++)
				{
					SetCursorPos({ flSidePad, flY });
					if (SideTab(tPage.m_vSubs[i], i == iSub, flSideWidth - flSidePad * 2))
						fSelect(iPage, i);
					flY += H::Draw.Scale(32 + 2);
				}
			}
			else
			{
				const std::string sResults = std::format("Results for \"{}\"", sSearch);
				const auto vLines = WrapText(sResults, int(flSideWidth - flSidePad * 2 - H::Draw.Scale(24)));
				for (size_t i = 0; i < vLines.size() && i < 3; i++)
					pDrawList->AddText(vDrawPos + ImVec2(flSidePad + H::Draw.Scale(12), flY + H::Draw.Scale(8) + H::Draw.Scale(18) * i), F::Render.Active, vLines[i].c_str());
			}

			if (tShell.m_sEditingBind)
			{
				const float flCardHeight = H::Draw.Scale(46);
				const ImVec2 vCardMin = vDrawPos + ImVec2(flSidePad, flBodyBottom - flSidePad - flCardHeight);
				const ImVec2 vCardMax = vCardMin + ImVec2(flSideWidth - flSidePad * 2, flCardHeight);
				pDrawList->AddRectFilled(vCardMin, vCardMax, F::Render.AccentDim, H::Draw.Scale(6));
				pDrawList->AddText(pSmall, pSmall->LegacySize, vCardMin + ImVec2(H::Draw.Scale(10), H::Draw.Scale(7)), F::Render.Accent, "Editing bind");
				const std::string sName = TruncateText(tShell.m_sEditingBind, int(vCardMax.x - vCardMin.x - H::Draw.Scale(48)));
				pDrawList->AddText(vCardMin + ImVec2(H::Draw.Scale(10), H::Draw.Scale(22)), F::Render.Active, sName.c_str());
				SetCursorScreenPos({ vCardMax.x - H::Draw.Scale(32), vCardMin.y + H::Draw.Scale(11) });
				if (IconButton(ICON_MD_CLOSE, H::Draw.Scale(24)) && tShell.m_fStopEditingBind)
					tShell.m_fStopEditingBind();
			}
		}

		{
			ImFont* pSmall = F::Render.FontSmall;
			const float flTextY = floorf(flBodyBottom + (flFooterHeight - pSmall->LegacySize) / 2);
			const ImU32 uText = FadeColor(F::Render.Inactive, 0.85f);
			if (tShell.m_sBuild)
				pDrawList->AddText(pSmall, pSmall->LegacySize, vDrawPos + ImVec2(H::Draw.Scale(14), flTextY), uText, tShell.m_sBuild);
			if (tShell.m_sVariant)
			{
				const float flWidth = pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, tShell.m_sVariant).x;
				pDrawList->AddText(pSmall, pSmall->LegacySize, vDrawPos + ImVec2(vWindowSize.x - H::Draw.Scale(14) - flWidth, flTextY), uText, tShell.m_sVariant);
			}
		}

		if (!IsMouseDown(ImGuiMouseButton_Left) || !tShell.m_bIsOpen)
			bDraggingMenu = false;
		bool bCanDragMenu = tShell.m_flAnim >= 1.0f && IsMouseWithin(vDrawPos.x, vDrawPos.y, vWindowSize.x, vWindowSize.y) && !IsAnyItemHovered() && !IsAnyItemActive() && !IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
		if (bCanDragMenu && IsMouseClicked(ImGuiMouseButton_Left))
		{
			bDraggingMenu = true;
			vMenuTargetPos = vDrawPos;
			vMenuDragOffset = vMousePos - vDrawPos;
		}
		if (bSearch && IsMouseReleased(ImGuiMouseButton_Left) && !IsAnyItemHovered() && IsMouseWithin(vDrawPos.x + flSideWidth, vDrawPos.y + flTopHeight, vWindowSize.x - flSideWidth, flBodyBottom - flTopHeight))
			sSearch.clear();

		const float flGutter = H::Draw.Scale(10);
		PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(6), H::Draw.Scale(4) });
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(12), H::Draw.Scale(12) });
		SetCursorPos({ flContentX, flTopHeight + H::Draw.Scale(8) });
		if (BeginChild("Page", { vWindowSize.x - flContentX - flGutter, flBodyBottom - flTopHeight - H::Draw.Scale(8) - flGutter + H::Draw.Scale(4) }, ImGuiChildFlags_AlwaysUseWindowPadding))
		{
			if (!bSearch)
			{
				if (tShell.m_fPage)
					tShell.m_fPage(iPage, iSub);
			}
			else if (tShell.m_fSearch)
				tShell.m_fSearch(sSearch);
		} EndChild();
		PopStyleVar(3);

		End();

		if (tShell.m_flAnim < 1.0f)
		{
			const float flT = std::clamp(tShell.m_flAnim, 0.0f, 1.0f);
			float flScaleX = 0.f, flScaleY = 0.f;
			if (tShell.m_bIsOpen)
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
			const float flAlpha = std::clamp(flT * (tShell.m_bIsOpen ? 1.5f : 1.2f), 0.0f, 1.0f);

			const ImVec2 vCenter = vDrawPos + vWindowSize * 0.5f;
			if (ImGuiWindow* pMainWindow = FindWindowByName("Main"))
			{
				std::unordered_set<ImDrawList*> visited;
				auto TransformTree = [&](auto& self, ImGuiWindow* pWindow) -> void
				{
					if (!pWindow || !pWindow->Active)
						return;

					if (pWindow->DrawList && visited.insert(pWindow->DrawList).second)
						TransformDrawList(pWindow->DrawList, vCenter, { flScaleX, flScaleY }, flAlpha, vDisplay);

					for (ImGuiWindow* pChild : pWindow->DC.ChildWindows)
						self(self, pChild);
				};

				TransformTree(TransformTree, pMainWindow);

				ImGuiContext& g = *GImGui;
				for (ImGuiWindow* pWin : g.Windows)
				{
					if (!pWin->Active || pWin == pMainWindow)
						continue;
					if (pWin->RootWindowPopupTree == pMainWindow)
						TransformTree(TransformTree, pWin);
				}
			}
		}
	}
}
