#pragma once
#include "Widgets.h"
#include "../../Binds/Binds.h"
#include "../../Visuals/Materials/Materials.h"

namespace ImGui
{
	struct MaterialPreset_t
	{
		const char* m_sName;
		ChamsMaterial_t m_tMaterial;
	};

	inline const auto& MaterialPresets()
	{
		static const std::array<MaterialPreset_t, 24> vPresets = {{
			{ "Arctic chrome", { .tColor = { 195, 225, 245, 255 }, .iEffect = 8, .tSecondary = { 65, 155, 255, 255 }, .flRoughness = 0.12f, .flReflectivity = 0.95f, .flIntensity = 1.1f, .flRimPower = 4.f } },
			{ "Opal silk", { .tColor = { 235, 220, 250, 255 }, .iEffect = 4, .tSecondary = { 100, 235, 215, 255 }, .flRoughness = 0.28f, .flReflectivity = 0.7f, .flScale = 1.5f, .flSpeed = 0.3f, .flRimPower = 2.5f } },
			{ "Cyan hologram", { .tColor = { 35, 210, 255, 180 }, .iEffect = 6, .tSecondary = { 155, 100, 255, 255 }, .flRoughness = 0.15f, .flReflectivity = 0.4f, .flScale = 2.5f, .flSpeed = 0.8f, .flIntensity = 1.3f, .flRimPower = 2.f, .iBlend = 1 } },
			{ "Solar plasma", { .tColor = { 255, 85, 25, 255 }, .iEffect = 13, .tSecondary = { 255, 220, 65, 255 }, .flScale = 1.7f, .flSpeed = 1.2f, .flIntensity = 1.6f, .flRimPower = 2.f, .iBlend = 2 } },
			{ "Nebula", { .tColor = { 70, 35, 160, 255 }, .iEffect = 14, .tSecondary = { 50, 205, 255, 255 }, .flScale = 1.2f, .flSpeed = 0.4f, .flIntensity = 1.4f, .flRimPower = 2.5f, .iBlend = 2 } },
			{ "Carbon weave", { .tColor = { 35, 40, 48, 255 }, .iEffect = 15, .tSecondary = { 110, 125, 145, 255 }, .flRoughness = 0.42f, .flReflectivity = 0.6f, .flScale = 4.f, .flSpeed = 0.f, .flRimPower = 4.f } },
			{ "Gold brushed", { .tColor = { 225, 170, 65, 255 }, .iEffect = 16, .tSecondary = { 255, 235, 170, 255 }, .flRoughness = 0.3f, .flReflectivity = 0.85f, .flScale = 3.f, .flSpeed = 0.f, .flRimPower = 4.f } },
			{ "Digital", { .tColor = { 35, 245, 155, 220 }, .iEffect = 18, .tSecondary = { 40, 110, 255, 255 }, .flScale = 2.8f, .flSpeed = 1.1f, .flIntensity = 1.2f, .flRimPower = 2.f, .iBlend = 1 } },
			{ "Molten lava", { .tColor = { 255, 70, 15, 255 }, .iEffect = 19, .tSecondary = { 255, 210, 60, 255 }, .flReflectivity = 0.4f, .flSpeed = 0.8f, .flRimPower = 2.5f } },
			{ "Candy bubble", { .tColor = { 0, 230, 255, 255 }, .iEffect = 20, .tSecondary = { 255, 230, 0, 255 }, .flReflectivity = 0.3f } },
			{ "Neon trip", { .tColor = { 120, 40, 200, 255 }, .iEffect = 21, .flSpeed = 1.f, .flIntensity = 1.2f } },
			{ "Neural net", { .tColor = { 40, 120, 255, 255 }, .iEffect = 22, .tSecondary = { 120, 230, 255, 255 }, .flReflectivity = 0.5f, .flSpeed = 1.f, .flIntensity = 1.2f, .iBlend = 1 } },
			{ "Soap film", { .tColor = { 255, 255, 255, 255 }, .iEffect = 23, .tSecondary = { 255, 255, 255, 255 }, .flSpeed = 1.f } },
			{ "Aurora borealis", { .tColor = { 90, 255, 140, 255 }, .iEffect = 24, .tSecondary = { 80, 180, 255, 255 }, .flSpeed = 1.f, .flIntensity = 1.2f } },
			{ "Aquarium", { .tColor = { 15, 100, 255, 255 }, .iEffect = 25, .tSecondary = { 120, 210, 255, 255 }, .flSpeed = 1.f } },
			{ "Daydream", { .tColor = { 110, 200, 255, 255 }, .iEffect = 26, .tSecondary = { 90, 205, 40, 255 }, .flSpeed = 0.6f } },
			{ "Galaxy", { .tColor = { 80, 30, 170, 255 }, .iEffect = 28, .tSecondary = { 255, 120, 200, 255 }, .flReflectivity = 0.5f, .flScale = 1.2f, .flSpeed = 0.5f, .flIntensity = 1.3f, .flRimPower = 2.5f } },
			{ "Matrix", { .tColor = { 0, 40, 10, 255 }, .iEffect = 29, .tSecondary = { 40, 255, 90, 255 }, .flScale = 1.5f, .flSpeed = 1.f, .flIntensity = 1.2f, .flRimPower = 2.f } },
			{ "Inferno", { .tColor = { 255, 60, 0, 255 }, .iEffect = 30, .tSecondary = { 255, 220, 80, 255 }, .flScale = 1.4f, .flSpeed = 1.2f, .flIntensity = 1.3f, .flRimPower = 2.f, .iBlend = 1 } },
			{ "Frostbite", { .tColor = { 170, 220, 255, 230 }, .iEffect = 31, .tSecondary = { 235, 250, 255, 255 }, .flRoughness = 0.2f, .flReflectivity = 0.8f, .flScale = 1.5f, .flSpeed = 0.5f } },
			{ "Cel shaded", { .tColor = { 255, 140, 60, 255 }, .iEffect = 32, .tSecondary = { 20, 15, 30, 255 }, .flRoughness = 0.3f, .flReflectivity = 0.8f } },
			{ "Bio cells", { .tColor = { 40, 200, 120, 255 }, .iEffect = 33, .tSecondary = { 200, 255, 120, 255 }, .flReflectivity = 0.4f, .flScale = 1.5f, .flSpeed = 0.7f, .flRimPower = 2.5f } },
			{ "Circuit board", { .tColor = { 10, 60, 40, 255 }, .iEffect = 34, .tSecondary = { 60, 255, 200, 255 }, .flReflectivity = 0.5f, .flScale = 1.5f, .flSpeed = 1.f, .flIntensity = 1.2f } },
			{ "Storm", { .tColor = { 60, 90, 255, 255 }, .iEffect = 35, .tSecondary = { 200, 230, 255, 255 }, .flScale = 1.2f, .flSpeed = 1.f, .flIntensity = 1.3f, .flRimPower = 2.f, .iBlend = 1 } }
		}};
		return vPresets;
	}

	inline ChamsMaterial_t ShaderLayer(int iEffect)
	{
		for (const auto& tPreset : MaterialPresets())
		{
			if (tPreset.m_tMaterial.iEffect == iEffect)
				return tPreset.m_tMaterial;
		}
		ChamsMaterial_t tMaterial;
		tMaterial.iEffect = iEffect;
		return tMaterial;
	}

	inline std::string MaterialLabel(const std::string& sName)
	{
		return sName.starts_with(ChamsShaderPrefix) ? sName.substr(std::strlen(ChamsShaderPrefix)) : sName;
	}

	inline void FMaterialPresets(ChamsMaterial_t* pVar)
	{
		const auto& vPresets = MaterialPresets();
		int iSelected = -1;
		for (int i = 0; i < int(vPresets.size()); i++)
		{
			auto tMaterial = vPresets[i].m_tMaterial;
			tMaterial.flStart = pVar->flStart;
			tMaterial.flEnd = pVar->flEnd;
			tMaterial.bSmoothAlpha = pVar->bSmoothAlpha;
			if (tMaterial == *pVar)
				iSelected = i;
		}

		auto tRow = BeginFieldRow("Preset", FDropdownEnum::None, 0);
		if (BeginFieldPopup(tRow))
		{
			DebugDummy({ 0, H::Draw.Scale(2) });
			for (int i = 0; i < int(vPresets.size()); i++)
			{
				const auto& tPreset = vPresets[i];
				const auto tOption = FOptionRow(std::format("##Preset{}", i).c_str(), iSelected == i);
				FOptionText(tOption, tPreset.m_sName, H::Draw.Scale(12), OptionColor(tOption, iSelected == i, true));
				const ImVec2 vMax = tOption.m_vMax - ImVec2(H::Draw.Scale(12), H::Draw.Scale(8));
				const ImVec2 vMin = { vMax.x - H::Draw.Scale(46), tOption.m_vMin.y + H::Draw.Scale(8) };
				const ImU32 uPrimary = GetColorU32(ColorByteToVec(tPreset.m_tMaterial.tColor.Alpha(255)));
				const ImU32 uSecondary = GetColorU32(ColorByteToVec(tPreset.m_tMaterial.tSecondary.Alpha(255)));
				GetWindowDrawList()->AddRectFilledMultiColor(vMin, vMax, uPrimary, uSecondary, uSecondary, uPrimary);
				if (tOption.m_bClicked && !Disabled)
				{
					auto tMaterial = tPreset.m_tMaterial;
					tMaterial.flStart = pVar->flStart;
					tMaterial.flEnd = pVar->flEnd;
					tMaterial.bSmoothAlpha = pVar->bSmoothAlpha;
					*pVar = tMaterial;
					iSelected = i;
				}
			}
			DebugDummy({ 0, H::Draw.Scale(2) });
			EndFieldPopup();
		}
		EndFieldRow("Preset", tRow, iSelected >= 0 ? vPresets[iSelected].m_sName : "Custom", true);
		FTooltip("Apply a complete material style. Keeps this layer's engine fallback and distance settings. Swatches show the color palette, not a render preview.", tRow.m_bHovered);
	}

	inline void FMaterialWindow(const char* sLabel, ChamsMaterial_t* pVar)
	{
		PushID(sLabel);
		auto vOriginalPos = GetCursorPos();
		PopStyleVar();
		PushStyleVar(ImGuiStyleVar_Alpha, 1.f);
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), 0 });
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(12), H::Draw.Scale(10) });

		if (IconButton(ICON_MD_EDIT))
			OpenPopup(sLabel);

		SetNextWindowSize({ H::Draw.Scale(400), 0 });
		SetNextWindowSizeConstraints({ 0, 0 }, { FLT_MAX, GetIO().DisplaySize.y * 0.85f });
		if (FBeginPopup(sLabel))
		{
			pVar->Sanitize();
			PushFont(F::Render.FontBold);
			TextUnformatted(sLabel);
			PopFont();
			DebugDummy({ 0, H::Draw.Scale(6) });
			const ImGuiID uID = GetID("##MaterialEditor");
			auto fLabel = [uID](const char* sName) { return std::format("{}##{}", sName, uID); };

			enum EffectControl_t { Roughness = 1 << 0, Reflectivity = 1 << 1, Pattern = 1 << 2, Animated = 1 << 3, Rim = 1 << 4, Secondary = 1 << 5 };
			static constexpr std::array<int, ChamsEffectCount + 1> vControls = {
				0,
				0,
				Reflectivity | Rim | Secondary | Roughness,
				Roughness | Reflectivity | Rim | Secondary,
				Roughness | Reflectivity | Pattern | Animated | Secondary,
				Roughness | Reflectivity | Rim | Secondary,
				Pattern | Animated | Rim | Secondary,
				Roughness | Reflectivity | Pattern | Animated | Secondary,
				Roughness | Reflectivity | Rim | Secondary,
				Roughness | Reflectivity | Pattern | Animated | Rim | Secondary,
				Reflectivity | Rim | Secondary,
				Roughness | Reflectivity | Rim | Secondary,
				Reflectivity | Pattern | Animated | Rim | Secondary,
				Reflectivity | Pattern | Animated | Rim | Secondary,
				Reflectivity | Pattern | Animated | Rim | Secondary,
				Roughness | Reflectivity | Pattern | Secondary,
				Roughness | Reflectivity | Pattern | Secondary,
				Roughness | Reflectivity | Pattern | Animated | Rim | Secondary,
				Reflectivity | Pattern | Animated | Rim | Secondary,
				Reflectivity | Pattern | Animated | Rim | Secondary,
				Roughness | Reflectivity | Secondary,
				Pattern | Animated,
				Reflectivity | Pattern | Animated | Rim | Secondary,
				Pattern | Animated | Secondary,
				Pattern | Animated | Secondary,
				Pattern | Animated | Secondary,
				Pattern | Animated | Secondary,
				Reflectivity | Rim | Secondary,
				Reflectivity | Pattern | Animated | Rim | Secondary,
				Pattern | Animated | Rim | Secondary,
				Pattern | Animated | Rim | Secondary,
				Roughness | Reflectivity | Pattern | Animated | Rim | Secondary,
				Roughness | Reflectivity | Secondary,
				Reflectivity | Pattern | Animated | Rim | Secondary,
				Roughness | Reflectivity | Pattern | Animated | Secondary,
				Pattern | Animated | Rim | Secondary,
				Roughness | Reflectivity | Pattern | Animated,
				Roughness | Reflectivity | Pattern | Secondary,
				Pattern | Animated,
				Roughness | Reflectivity | Pattern | Secondary
			};

			bool bHovered = false;
			FDropdown("Effect", &pVar->iEffect, std::vector<const char*>(std::begin(ChamsEffectNames), std::end(ChamsEffectNames)), {}, FDropdownEnum::None, 0, "Source", &bHovered);
			FTooltip("Source uses the selected engine material. Custom effects are independent of its name; this engine material remains the fallback when custom rendering is unavailable. Select other material names to add independent effect layers.", bHovered);
			FMaterialPresets(pVar);

			const int iEffect = pVar->iEffect;
			const int iControls = vControls[std::clamp(iEffect, 0, int(vControls.size()) - 1)];
			const bool bRoughness = iControls & Roughness;
			const bool bReflectivity = iControls & Reflectivity;
			const bool bPattern = iControls & Pattern;
			const bool bAnimated = iControls & Animated;
			const bool bRim = iControls & Rim;
			const bool bSecondary = iControls & Secondary;

			FColorPicker("Primary color", &pVar->tColor, FColorPickerEnum::Full);
			if (bSecondary)
			{
				FColorPicker("Secondary color", &pVar->tSecondary, FColorPickerEnum::Full);
				FLabel("Palette");
				const ImVec2 vSize = { H::Draw.Scale(110), H::Draw.Scale(12) };
				const ImVec2 vMin = TakeAccessory(vSize.x, vSize.y);
				const ImU32 uPrimary = GetColorU32(ColorByteToVec(pVar->tColor.Alpha(255)));
				const ImU32 uSecondary = GetColorU32(ColorByteToVec(pVar->tSecondary.Alpha(255)));
				GetWindowDrawList()->AddRectFilledMultiColor(vMin, vMin + vSize, uPrimary, uSecondary, uSecondary, uPrimary);
			}
			if (iEffect)
			{
				if (bRoughness)
					FSlider(fLabel("Roughness").c_str(), &pVar->flRoughness, 0.f, 1.f, 0.01f, "%.2f", FSliderEnum::Clamp);
				if (bReflectivity)
					FSlider(fLabel("Reflectivity").c_str(), &pVar->flReflectivity, 0.f, 1.f, 0.01f, "%.2f", FSliderEnum::Clamp);
				if (bPattern)
					FSlider(fLabel("Pattern scale").c_str(), &pVar->flScale, 0.05f, 16.f, 0.05f, "%.2fx", FSliderEnum::Clamp);
				if (bAnimated)
				{
					FSlider(fLabel("Animation speed").c_str(), &pVar->flSpeed, 0.f, 8.f, 0.05f, "%.2fx", FSliderEnum::Clamp);
					FTooltip("Set to zero to freeze the pattern.");
				}
				FSlider(fLabel("Intensity").c_str(), &pVar->flIntensity, 0.f, 4.f, 0.05f, "%.2f", FSliderEnum::Clamp);
				if (bRim)
					FSlider(fLabel("Rim power").c_str(), &pVar->flRimPower, 0.25f, 8.f, 0.05f, "%.2f", FSliderEnum::Clamp);
				FDropdown("Blend", &pVar->iBlend, { "Alpha", "Additive", "Screen" });
				FToggle("Write depth", &pVar->bDepthWrite);
				FTooltip("Writes this layer into the depth buffer, which can hide later layers.");
			}

			Subheader("Distance");
			FSlider(fLabel("Render start").c_str(), &pVar->flStart, 0.f, 2048.f, 128.f, "%.fHU", FSliderEnum::Min);
			FSlider(fLabel("Render end").c_str(), &pVar->flEnd, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Min);
			FToggle("Distance to alpha", &pVar->bSmoothAlpha);
			pVar->Sanitize();

			EndPopup();
		}
		PopStyleVar(3);

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
		SetCursorPos(vOriginalPos);
		PopID();
	}

	inline void FGlowControls(const char* sID, Glow_t* pVar)
	{
		PushID(sID);
		const ImGuiID uID = GetID("##GlowEditor");
		auto fLabel = [uID](const char* sName) { return std::format("{}##{}", sName, uID); };
		pVar->Sanitize();
		bool bStyleHovered = false;
		FDropdown("Style", &pVar->Style, { "Stencil", "Enhanced" }, {}, FDropdownEnum::None, 0, "Stencil", &bStyleHovered);
		FTooltip("Stencil is the classic glow: stencil outline plus engine blur. Enhanced uses the D3D9 dilate and bloom pipeline with layers, tint and depth modes.", bStyleHovered);
		if (!pVar->Style)
		{
			PushTransparent(Transparent || !pVar->Stencil);
			FSlider(fLabel("Stencil scale").c_str(), &pVar->Stencil, 0, 10, 1, "%i", FSliderEnum::Min);
			PopTransparent();
			PushTransparent(Transparent || !pVar->Blur);
			FSlider(fLabel("Blur scale").c_str(), &pVar->Blur, 0.f, 10.f, 1.f, "%g", FSliderEnum::Min | FSliderEnum::Precision);
			PopTransparent();
			FSlider(fLabel("Render start").c_str(), &pVar->Start, 0.f, 2048.f, 128.f, "%.fHU", FSliderEnum::Min);
			FSlider(fLabel("Render end").c_str(), &pVar->End, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Min);
			FToggle("Distance to alpha", &pVar->SmoothAlpha);
			pVar->Sanitize();
			PopID();
			return;
		}
		FSlider(fLabel("Outline radius").c_str(), &pVar->Stencil, 0, 16, 1, "%i", FSliderEnum::Clamp);
		FSlider(fLabel("Bloom radius").c_str(), &pVar->Blur, 0.f, 16.f, 0.25f, "%.2f", FSliderEnum::Clamp);
		FSlider(fLabel("Intensity").c_str(), &pVar->Intensity, 0.f, 4.f, 0.05f, "%.2f", FSliderEnum::Clamp);
		FSlider(fLabel("Opacity").c_str(), &pVar->Opacity, 0.f, 1.f, 0.01f, "%.2f", FSliderEnum::Clamp);
		if (FButton("Glow settings"))
			OpenPopup("##GlowSettings");
		FTooltip("Shape, layers, blend, depth, outer tint and distance. Both radii at zero disable glow.");

		SetNextWindowSize({ H::Draw.Scale(360), 0 });
		SetNextWindowSizeConstraints({ 0, 0 }, { FLT_MAX, GetIO().DisplaySize.y * 0.85f });
		if (FBeginPopup("##GlowSettings"))
		{
			FSlider(fLabel("Falloff").c_str(), &pVar->Falloff, 0.25f, 4.f, 0.05f, "%.2f", FSliderEnum::Clamp);
			PushTransparent(Transparent || !pVar->Blur);
			FSlider(fLabel("Bloom strength").c_str(), &pVar->Bloom, 0.f, 4.f, 0.05f, "%.2f", FSliderEnum::Clamp);
			PopTransparent();
			FSlider(fLabel("Layers").c_str(), &pVar->Layers, 1, 4, 1, "%i", FSliderEnum::Clamp);
			FTooltip("More layers produce a richer halo at a higher rendering cost.");
			FDropdown("Blend", &pVar->Blend, { "Additive", "Alpha", "Screen" });
			FDropdown("Depth", &pVar->Depth, { "All", "Visible", "Occluded" });
			FToggle("Custom outer tint", &pVar->CustomOuter);
			if (pVar->CustomOuter)
				FColorPicker("Outer color", &pVar->OuterColor, FColorPickerEnum::Full);
			Subheader("Distance");
			FSlider(fLabel("Render start").c_str(), &pVar->Start, 0.f, 2048.f, 128.f, "%.fHU", FSliderEnum::Min);
			FSlider(fLabel("Render end").c_str(), &pVar->End, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Min);
			FToggle("Distance to alpha", &pVar->SmoothAlpha);
			EndPopup();
		}
		pVar->Sanitize();
		PopID();
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
			if (a.m_iEffect || b.m_iEffect)
				return a.m_iEffect != b.m_iEffect && (!a.m_iEffect || (b.m_iEffect && a.m_iEffect < b.m_iEffect));

			// keep locked materials higher
			if (a.m_bLocked && !b.m_bLocked)
				return true;
			if (!a.m_bLocked && b.m_bLocked)
				return false;

			return a.m_sName < b.m_sName;
		});

		std::vector<std::string> vEntries = { "Original" };
		std::vector<int> vEffects = { 0 };
		for (auto& pair : vMaterials)
		{
			vEntries.push_back(pair.m_sName.c_str());
			vEffects.push_back(pair.m_iEffect);
		}

		bool bReturn = false;

		if (Transparent || Disabled)
			PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		std::unordered_map<std::string, std::vector<std::pair<std::string, ChamsMaterial_t>>::iterator> mIts = {};
		for (auto it = pVar->begin(); it != pVar->end(); it++)
			mIts[it->first] = it;

		std::vector<std::string> vSelected = {};
		for (auto& pair : *pVar)
			vSelected.push_back(MaterialLabel(pair.first));

		auto tRow = BeginFieldRow(sLabel, iFlags, iSizeOffset);
		if (pHovered)
			*pHovered = tRow.m_bHovered;
		if (BeginFieldPopup(tRow))
		{
			const float flPopupWidth = GetWindowWidth();
			std::string sModified;
			DebugDummy({ 0, H::Draw.Scale(2) });
			for (int i = 0; i < vEntries.size(); i++)
			{
				auto& sEntry = vEntries[i];
				const std::string sLabelText = MaterialLabel(sEntry);
				if (vEffects[i] && !vEffects[i - 1])
				{
					DebugDummy({ 0, H::Draw.Scale(4) });
					SetCursorPosX(GetCursorPosX() + H::Draw.Scale(12));
					PushFont(F::Render.FontSmall);
					PushStyleColor(ImGuiCol_Text, F::Render.Accent.Value);
					TextUnformatted("Custom shaders");
					PopStyleColor();
					PopFont();
					DebugDummy({ 0, H::Draw.Scale(2) });
				}

				auto it = mIts.find(sEntry);
				bool bFlagActive = it != mIts.end();
				int iEntry = bFlagActive ? int(std::distance(pVar->begin(), mIts[sEntry])) + 1 : 0;

				ImVec2 vOriginalPos2 = GetCursorPos();
				if (bFlagActive)
				{
					SetCursorPos(vOriginalPos2 + ImVec2(flPopupWidth - H::Draw.Scale(32), H::Draw.Scale(1)));
					auto tOriginal = it->second->second;
					FMaterialWindow(sEntry.c_str(), &it->second->second);
					bReturn |= tOriginal != it->second->second;
					SetCursorPos(vOriginalPos2);
				}

				bool bHovered = bFlagActive && IsItemHovered();
				auto tOption = FOptionRow((std::string("##") + sEntry + std::to_string(i)).c_str(), bFlagActive, ImGuiSelectableFlags_DontClosePopups);
				if (tOption.m_bClicked && !Disabled && (bFlagActive || pVar->size() < 32))
					sModified = sEntry;
				FTooltip("A maximum of 32 material layers can be selected.", !bFlagActive && pVar->size() >= 32 && tOption.m_bHovered);
				bHovered = !bHovered && tOption.m_bHovered;

				if (bFlagActive)
				{
					PushFont(F::Render.FontSmall);
					FOptionText(tOption, std::format("{}", iEntry).c_str(), H::Draw.Scale(14), FadeColor(F::Render.Accent));
					PopFont();
				}
				FOptionText(tOption, (bHovered ? sLabelText : TruncateText(sLabelText, int(flPopupWidth - H::Draw.Scale(bFlagActive ? 84 : 52)))).c_str(), H::Draw.Scale(32 + 6 * int(log10(std::max(pVar->size(), 1ui64)))), FadeColor(bFlagActive || tOption.m_bHovered ? F::Render.Active.Value : MixColor(F::Render.Inactive, F::Render.Active, 0.5f)));
			}
			if (!sModified.empty())
			{
				auto it = mIts.find(sModified);
				if (it != mIts.end())
					pVar->erase(it->second);
				else
				{
					auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sModified.c_str()));
					pVar->emplace_back(sModified, pMaterial && pMaterial->m_iEffect ? ShaderLayer(pMaterial->m_iEffect) : ChamsMaterial_t());
				}
				bReturn = true;
				vSelected.clear();
				for (const auto& tMaterial : *pVar)
					vSelected.push_back(MaterialLabel(tMaterial.first));
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
