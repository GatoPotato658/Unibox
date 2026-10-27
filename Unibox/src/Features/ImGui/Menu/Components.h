#pragma once
#include "Widgets.h"
#include "../../Binds/Binds.h"
#include "../../Visuals/Materials/Materials.h"

namespace ImGui
{
	inline void FMaterialWindow(const char* sLabel, ChamsMaterial_t* pVar)
	{
		auto vOriginalPos = GetCursorPos();
		PopStyleVar();
		PushStyleVar(ImGuiStyleVar_Alpha, 1.f);
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), 0 });

		if (IconButton(ICON_MD_EDIT))
			OpenPopup(sLabel);

		SetNextWindowSize({ H::Draw.Scale(300), 0 });
		if (FBeginPopup(sLabel))
		{
			PushFont(F::Render.FontBold);
			TextUnformatted(sLabel);
			PopFont();
			DebugDummy({ 0, H::Draw.Scale(6) });

			FColorPicker(std::format("Color##{}", sLabel).c_str(), &pVar->tColor, FColorPickerEnum::Full);
			FSlider(std::format("Render start##{}", sLabel).c_str(), &pVar->flStart, 0.f, 2048.f, 128, "%.fHU", FSliderEnum::Clamp);
			FSlider(std::format("Render end##{}", sLabel).c_str(), &pVar->flEnd, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Min);
			FToggle(std::format("Distance to alpha##{}", sLabel).c_str(), &pVar->bSmoothAlpha);

			EndPopup();
		}
		PopStyleVar(2);

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
		SetCursorPos(vOriginalPos);
	}

	inline bool FMDropdown(const char* sLabel, std::vector<std::pair<std::string, ChamsMaterial_t>>* pVar, int iFlags = FDropdownEnum::None, int iSizeOffset = 0, bool* pHovered = nullptr)
	{
		std::vector<Material_t> vMaterials;
		for (auto& tMaterial : F::Materials.m_mMaterials | std::views::values)
		{
			if (FNV1A::Hash32(tMaterial.m_sName.c_str()) != FNV1A::Hash32Const("None"))
				vMaterials.push_back(tMaterial);
		}

		std::sort(vMaterials.begin(), vMaterials.end(), [&](const auto& a, const auto& b) -> bool
		{
			// keep locked materials higher
			if (a.m_bLocked && !b.m_bLocked)
				return true;
			if (!a.m_bLocked && b.m_bLocked)
				return false;

			return a.m_sName < b.m_sName;
		});

		std::vector<std::string> vEntries = { "Original" };
		for (auto& pair : vMaterials)
			vEntries.push_back(pair.m_sName.c_str());

		bool bReturn = false;

		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		std::unordered_map<std::string, std::vector<std::pair<std::string, ChamsMaterial_t>>::iterator> mIts = {};
		for (auto it = pVar->begin(); it != pVar->end(); it++)
			mIts[it->first] = it;

		std::vector<std::string> vSelected = {};
		for (auto& pair : *pVar)
			vSelected.push_back(pair.first);

		auto tRow = BeginFieldRow(sLabel, iFlags, iSizeOffset);
		if (pHovered)
			*pHovered = tRow.m_bHovered;
		if (BeginFieldPopup(tRow))
		{
			const float flPopupWidth = GetWindowWidth();
			DebugDummy({ 0, H::Draw.Scale(2) });
			for (int i = 0; i < vEntries.size(); i++)
			{
				auto& sEntry = vEntries[i];

				auto it = mIts.find(sEntry);
				bool bFlagActive = it != mIts.end();
				int iEntry = bFlagActive ? int(std::distance(pVar->begin(), mIts[sEntry])) + 1 : 0;

				ImVec2 vOriginalPos2 = GetCursorPos();
				if (bFlagActive)
				{
					SetCursorPos(vOriginalPos2 + ImVec2(flPopupWidth - H::Draw.Scale(32), H::Draw.Scale(1)));
					FMaterialWindow(sEntry.c_str(), &it->second->second);
					SetCursorPos(vOriginalPos2);
				}

				bool bHovered = bFlagActive && IsItemHovered();
				auto tOption = FOptionRow(std::format("##{}{}", sEntry, i).c_str(), bFlagActive, ImGuiSelectableFlags_DontClosePopups);
				if (tOption.m_bClicked)
				{
					if (bFlagActive)
						pVar->erase(it->second);
					else
						pVar->emplace_back(sEntry, Color_t());
					bReturn = true;
				}
				bHovered = !bHovered && tOption.m_bHovered;

				if (bFlagActive)
				{
					PushFont(F::Render.FontSmall);
					FOptionText(tOption, std::format("{}", iEntry).c_str(), H::Draw.Scale(14), FadeColor(F::Render.Accent));
					PopFont();
				}
				FOptionText(tOption, (bHovered ? sEntry : TruncateText(sEntry, int(flPopupWidth - H::Draw.Scale(bFlagActive ? 84 : 52)))).c_str(), H::Draw.Scale(32 + 6 * int(log10(std::max(pVar->size(), 1ui64)))), FadeColor(bFlagActive || tOption.m_bHovered ? F::Render.Active.Value : MixColor(F::Render.Inactive, F::Render.Active, 0.5f)));
			}
			DebugDummy({ 0, H::Draw.Scale(2) });
			EndFieldPopup();
		}

		EndFieldRow(sLabel, tRow, ListPreview(vSelected, tRow.m_tLayout.m_vFieldSize.x - H::Draw.Scale(34), "None"), true);

		if (Transparent || Disabled)
			PopStyleVar();

		return bReturn;
	}
}

#include "BindWidgets.h"

namespace ImGui
{
	WRAPPER(FMDropdown, VA_LIST(std::vector<std::pair<std::string, ChamsMaterial_t>>), VA_LIST(int iFlags = 0, int iSizeOffset = 0), VA_LIST(&tVal, iFlags, iSizeOffset))
}
