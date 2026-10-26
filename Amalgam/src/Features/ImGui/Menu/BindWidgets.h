#pragma once
#include "Widgets.h"

namespace ImGui
{
	// convar wrappers
	template <class T>
	inline int GetBind(ConfigVar<T>& tVar, bool bForce = false)
	{
		if (tVar.m_iFlags & (NOSAVE | NOBIND))
			return DEFAULT_BIND;

		if (bForce)
			return CurrentBind;

		int iParent = CurrentBind;
		while (true)
		{
			if (iParent == DEFAULT_BIND || tVar.contains(iParent))
				break;
			iParent = F::Binds.GetParent(iParent);
		}
		return iParent;
	}

	template <class T>
	inline T GetParentValue(ConfigVar<T>& tVar, int iBind)
	{
		int iParent = iBind;
		while (true)
		{
			iParent = F::Binds.GetParent(iParent);
			if (iParent == DEFAULT_BIND || tVar.contains(iParent))
				break;
		}
		return tVar[iParent];
	}

	inline bool bPushedDisabled = false, bPushedTransparent = false;

	template <class T>
	inline T& FGet(ConfigVar<T>& tVar, bool bDisable = false)
	{
		int iBind = GetBind(tVar);
		if (bDisable)
		{
			bPushedDisabled = false, bPushedTransparent = false;

			if (CurrentBind == DEFAULT_BIND)
			{
				if (Vars::Menu::MenuShowsBinds.Value && tVar[DEFAULT_BIND] != tVar.Value)
				{
					for (auto& [_iBind, tVal] : tVar.Map)
					{
						if (_iBind == DEFAULT_BIND)
							continue;

						if (tVal == tVar.Value)
						{
							PushDisabled(true);
							bPushedDisabled = true;
							return tVal;
						}
					}
				}
			}
			else
			{
				PushTransparent(CurrentBind != iBind && !(tVar.m_iFlags & (NOSAVE | NOBIND)));
				bPushedTransparent = true;
			}
		}
		return tVar[iBind];
	}

	template <class T>
	inline void FSet(ConfigVar<T>& tVar, T tVal)
	{
		if (!Disabled)
		{
			int iBind = GetBind(tVar, true);
			auto _tVal = GetParentValue(tVar, iBind);

			if (tVal != _tVal)
			{
				tVar[iBind] = tVal;
				if (iBind < F::Binds.m_vBinds.size())
				{
					auto& tBind = *std::next(F::Binds.m_vBinds.begin(), iBind);
					if (std::find(tBind.m_vVars.begin(), tBind.m_vVars.end(), &tVar) == tBind.m_vVars.end())
						tBind.m_vVars.push_back(&tVar);
				}
			}
			else if (iBind != DEFAULT_BIND)
			{
				for (auto it = tVar.Map.begin(); it != tVar.Map.end();)
				{
					if (it->first == iBind)
					{
						it = tVar.Map.erase(it);
						if (iBind < F::Binds.m_vBinds.size())
						{
							auto& tBind = *std::next(F::Binds.m_vBinds.begin(), iBind);
							auto it2 = std::find(tBind.m_vVars.begin(), tBind.m_vVars.end(), &tVar);
							if (it2 != tBind.m_vVars.end())
								tBind.m_vVars.erase(it2);
						}
					}
					else
						++it;
				}
			}
		}

		if (bPushedDisabled)
			PopDisabled();
		if (bPushedTransparent)
			PopTransparent();
	}

	template <class T>
	inline int DrawBindInfo(ConfigVar<T>& tVar, T& tVal, const std::string& sBind, bool bNewPopup, bool& bLastHovered)
	{
		PushFont(F::Render.FontBold);
		TextUnformatted(std::format("Bind '{}'", sBind).c_str());
		PopFont();
		DebugDummy({ 0, H::Draw.Scale(6) });

		static int iBind = DEFAULT_BIND;
		static Bind_t tBind = {};
		if (bNewPopup)
		{
			iBind = DEFAULT_BIND;
			tBind = { sBind };
		}

		std::vector<const char*> vEntries = {};
		std::vector<int> vValues = {};

		std::vector<std::string> vStrings = {}; // prevent dangling pointers
		for (auto& _iBind : tVar.Map | std::views::keys)
		{
			if (_iBind != DEFAULT_BIND)
				vValues.push_back(_iBind);
		}
		std::sort(vValues.begin(), vValues.end(), [&](const int a, const int b) -> bool
		{
			return a < b;
		});
		for (auto _iBind : vValues)
			vStrings.push_back(std::format("{}## Bind{}", _iBind != DEFAULT_BIND && _iBind < F::Binds.m_vBinds.size() ? F::Binds.m_vBinds[_iBind].m_sName : sBind, _iBind));
		for (auto& sEntry : vStrings)
			vEntries.push_back(sEntry.c_str());

		vEntries.push_back("New bind");
		vValues.push_back(-1);

		int iModified = -2;
		if (FDropdown("##Bind", &iBind, vEntries, vValues, FDropdownEnum::Modifiable, -90, "None", nullptr, &iModified))
		{
			if (iBind != DEFAULT_BIND && iBind < F::Binds.m_vBinds.size())
				tBind = F::Binds.m_vBinds[iBind];
			else
				tBind = { sBind };
			if (tVar.contains(iBind))
				tVal = tVar[iBind];
		}
		bool bClickedNew = iBind == DEFAULT_BIND && bLastHovered && IsMouseDown(ImGuiMouseButton_Left);
		if (iModified != -2 || bClickedNew)
		{
			if (iModified == -1 || bClickedNew)
			{
				iBind = int(F::Binds.m_vBinds.size());
				tBind = { sBind };
				tBind.m_vVars.push_back(&tVar);
				F::Binds.AddBind(iBind, tBind);
			}
			else
			{
				auto it = tVar.Map.find(iModified);
				if (it != tVar.Map.end())
				{
					tVar.Map.erase(it);
					if (iModified < F::Binds.m_vBinds.size())
					{
						auto& tBind = *std::next(F::Binds.m_vBinds.begin(), iModified);
						auto it2 = std::find(tBind.m_vVars.begin(), tBind.m_vVars.end(), &tVar);
						if (it2 != tBind.m_vVars.end())
							tBind.m_vVars.erase(it2);
					}
				}

				F::Binds.RemoveBind(iModified, false);
				iBind = -1;
			}
		}
		bool bHovered = false; bLastHovered = false;

		PushTransparent(iBind == DEFAULT_BIND);

		{
			const ImVec2 vOriginalPos = GetCursorPos();
			const float flIcon = H::Draw.Scale(24);
			auto fIcon = [&](const char* sIcon)
			{
				SetCursorScreenPos(TakeAccessory(flIcon, flIcon));
				const bool bClicked = IconButton(sIcon, flIcon, { 1, 1, 1, -1 }, &bHovered);
				bLastHovered |= bHovered;
				return bClicked;
			};

			PushTransparent(Transparent || !tBind.m_bEnabled, true);

			PushTransparent(Transparent || tBind.m_iVisibility == BindVisibilityEnum::Hidden, true);
			if (fIcon(tBind.m_iVisibility == BindVisibilityEnum::Always ? ICON_MD_VISIBILITY : ICON_MD_VISIBILITY_OFF))
				tBind.m_iVisibility = (tBind.m_iVisibility + 1) % 3;
			PopTransparent(1, 1);

			if (fIcon(!tBind.m_bNot ? ICON_MD_CODE : ICON_MD_CODE_OFF))
				tBind.m_bNot = !tBind.m_bNot;

			if (fIcon(!tBind.m_bEnabled ? ICON_MD_TOGGLE_OFF : ICON_MD_TOGGLE_ON))
				tBind.m_bEnabled = !tBind.m_bEnabled;

			PopTransparent(1, 1);

			SetCursorPos(vOriginalPos);
		}

		FDropdown("Type", &tBind.m_iType, { "Key", "Class", "Weapon type", "Item slot", "Misc" }, {}, FDropdownEnum::Left, 0, "None", &bHovered);
		bLastHovered |= bHovered;
		switch (tBind.m_iType)
		{
		case BindEnum::Key: FDropdown("Behavior", &tBind.m_iInfo, { "Hold", "Toggle", "Double click" }, {}, FDropdownEnum::Right, 0, "None", &bHovered); break;
		case BindEnum::Class: FDropdown("Class", &tBind.m_iInfo, { "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy" }, {}, FDropdownEnum::Right, 0, "None", &bHovered); break;
		case BindEnum::WeaponType: FDropdown("Weapon type", &tBind.m_iInfo, { "Hitscan", "Projectile", "Melee", "Throwable" }, {}, FDropdownEnum::Right, 0, "None", &bHovered); break;
		case BindEnum::ItemSlot: FDropdown("Item slot", &tBind.m_iInfo, { "1", "2", "3", "4", "5", "6", "7", "8", "9" }, {}, FDropdownEnum::Right, 0, "None", &bHovered); break;
		case BindEnum::Misc: FDropdown("Misc", &tBind.m_iInfo, { "Spectated", "Spectated 1st", "Spectated 3rd", "##Divider", "Zoomed", "Aiming" }, {}, FDropdownEnum::Right, 0, "None", &bHovered); break;
		}
		bLastHovered |= bHovered;

		if (tBind.m_iType == BindEnum::Key)
		{
			FKeybind("Key", tBind.m_iKey, FKeybindEnum::None, { Vars::Menu::PrimaryKey[DEFAULT_BIND], Vars::Menu::SecondaryKey[DEFAULT_BIND] }, { 0, 30 }, 0, &bHovered);
			bLastHovered |= bHovered;
		}
		Divider(H::Draw.Scale(6), H::Draw.Scale(4));

		if (!Disabled && iBind != DEFAULT_BIND && iBind < F::Binds.m_vBinds.size())
		{
			tVar[iBind] = tVal;

			// don't completely override to retain misc info
			auto& _tBind = F::Binds.m_vBinds[iBind];
			_tBind.m_iType = tBind.m_iType;
			_tBind.m_iInfo = tBind.m_iInfo;
			_tBind.m_iKey = tBind.m_iKey;
			_tBind.m_bEnabled = tBind.m_bEnabled;
			_tBind.m_iVisibility = tBind.m_iVisibility;
			_tBind.m_bNot = tBind.m_bNot;
		}

		return iBind;
	}

	#define WRAPPER(function, type, parameters, arguments) \
	inline bool function(ConfigVar<type>& tVar, parameters, bool* pHovered = nullptr, int iBindOverride = -1, int iLabelOverride = -1) \
	{ \
		const char* sLabel = iLabelOverride != -1 ? tVar.m_vNames[iLabelOverride] : tVar.m_vNames.front(); \
		int iVarFlags = tVar.m_iFlags & ~(VISUAL | NOSAVE | NOBIND | DEBUGVAR); \
		iFlags |= iVarFlags; \
		auto tVal = FGet(tVar, true); \
		bool bHovered = false; \
		bool bReturn = function(std::format("{}## {}", sLabel, tVar.Name()).c_str(), arguments, &bHovered); \
		FSet(tVar, tVal); \
		if (pHovered) \
			*pHovered = bHovered; \
		if (!(tVar.m_iFlags & (NOBIND | NOSAVE)) && !Disabled && CurrentBind == DEFAULT_BIND) \
		{ \
			static auto tStaticVal = tVal; \
			bool bNewPopup = bHovered && IsMouseReleased(ImGuiMouseButton_Right) && !IsMouseDown(ImGuiMouseButton_Left) && !IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId); \
			if (bNewPopup) \
			{ \
				OpenPopup(tVar.Name()); \
				tStaticVal = tVal; \
			} \
			SetNextWindowSize({ H::Draw.Scale(320), 0 }, ImGuiCond_Always); \
			PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(12), H::Draw.Scale(10) }); \
			bool bPopup = FBeginPopup(tVar.Name()); \
			PopStyleVar(); \
			if (bPopup) \
			{ \
				std::string sBind = iBindOverride != -1 ? tVar.m_vNames[iBindOverride] : tVar.m_vNames.back(); \
				std::transform(sBind.begin(), sBind.end(), sBind.begin(), ::tolower); \
				iFlags = iVarFlags; /*get rid of any visual flags*/ \
				if (FNV1A::Hash32Const(#function) == FNV1A::Hash32Const("FColorPicker")) \
					iFlags |= FColorPickerEnum::Full | FColorPickerEnum::RemoveVisuals; \
				PushTransparent(false); \
				static bool bLastHovered = false; \
				DrawBindInfo(tVar, tStaticVal, StripDoubleHash(sBind.c_str()), bNewPopup, bLastHovered); \
				tVal = tStaticVal; \
				function(std::format("{}## Bind", tVar.m_vNames.front()).c_str(), arguments, &bHovered); \
				bLastHovered |= bHovered; \
				tStaticVal = tVal; \
				PopTransparent(2); \
				EndPopup(); \
			} \
		} \
		return bReturn; \
	}

	template <class S>
	inline bool FToggleSliderVar(ConfigVar<bool>& tToggleVar, ConfigVar<S>& tSliderVar, int iToggleFlags, int iSliderFlags, const char* sFormatOverride, bool* pHovered)
	{
		int iToggleVarFlags = tToggleVar.m_iFlags & ~(VISUAL | NOSAVE | NOBIND | DEBUGVAR);
		iToggleFlags |= iToggleVarFlags;
		int iSliderVarFlags = tSliderVar.m_iFlags & ~(VISUAL | NOSAVE | NOBIND | DEBUGVAR);
		iSliderFlags |= iSliderVarFlags;

		const auto fRange = [&](auto& tMin, auto& tMax, auto& tStep)
		{
			if constexpr (std::is_same_v<S, float>)
				tMin = tSliderVar.m_unMin.f, tMax = tSliderVar.m_unMax.f, tStep = tSliderVar.m_unStep.f;
			else
				tMin = tSliderVar.m_unMin.i, tMax = tSliderVar.m_unMax.i, tStep = tSliderVar.m_unStep.i;
		};
		S tMin, tMax, tStep; fRange(tMin, tMax, tStep);
		const char* sFormat = sFormatOverride ? sFormatOverride : tSliderVar.m_sExtra;

		auto bVal = FGet(tToggleVar, true);
		auto tSliderVal = FGet(tSliderVar, true);
		bool bHovered = false;

		const char* sLabel = tToggleVar.m_vNames.front();
		bool bReturn = FToggleSlider(std::format("{}## {}", sLabel, tToggleVar.Name()).c_str(), &bVal, &tSliderVal, tMin, tMax, tStep, sFormat, iToggleFlags, iSliderFlags, &bHovered);

		FSet(tToggleVar, bVal);
		FSet(tSliderVar, tSliderVal);

		if (pHovered)
			*pHovered = bHovered;

		if (!(tToggleVar.m_iFlags & (NOBIND | NOSAVE)) && !Disabled && CurrentBind == DEFAULT_BIND)
		{
			static auto bStaticVal = bVal;
			static auto tStaticSliderVal = tSliderVal;
			static int iLastBind = DEFAULT_BIND;
			bool bNewPopup = bHovered && IsMouseReleased(ImGuiMouseButton_Right) && !IsMouseDown(ImGuiMouseButton_Left) && !IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
			if (bNewPopup)
			{
				OpenPopup(tToggleVar.Name());
				bStaticVal = bVal;
				tStaticSliderVal = tSliderVal;
				iLastBind = DEFAULT_BIND;
			}
			SetNextWindowSize({ H::Draw.Scale(320), 0 });
			PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(12), H::Draw.Scale(10) });
			const bool bPopup = FBeginPopup(tToggleVar.Name());
			PopStyleVar();
			if (bPopup)
			{
				std::string sBind = tToggleVar.m_vNames.back();
				std::transform(sBind.begin(), sBind.end(), sBind.begin(), ::tolower);
				iToggleFlags = iToggleVarFlags;
				iSliderFlags = iSliderVarFlags;
				PushTransparent(false);
				static bool bLastHovered = false;
				int iBind = DrawBindInfo(tToggleVar, bStaticVal, StripDoubleHash(sBind.c_str()), bNewPopup, bLastHovered);
				if (iBind != iLastBind && iBind != DEFAULT_BIND && iBind >= 0)
				{
					tStaticSliderVal = tSliderVar.contains(iBind) ? tSliderVar[iBind] : GetParentValue(tSliderVar, iBind);
					iLastBind = iBind;
				}
				bVal = bStaticVal;

				FToggleSlider(std::format("{}## Bind", tToggleVar.m_vNames.front()).c_str(), &bStaticVal, &tStaticSliderVal, tMin, tMax, tStep, sFormat, iToggleFlags, iSliderFlags, &bHovered);
				if (!Disabled && iBind != DEFAULT_BIND && iBind < F::Binds.m_vBinds.size())
				{
					tSliderVar[iBind] = tStaticSliderVal;
					auto& tBind = F::Binds.m_vBinds[iBind];
					if (std::find(tBind.m_vVars.begin(), tBind.m_vVars.end(), &tSliderVar) == tBind.m_vVars.end())
						tBind.m_vVars.push_back(&tSliderVar);
				}

				bLastHovered = bLastHovered || bHovered;
				PopTransparent(2);
				EndPopup();
			}
		}

		return bReturn;
	}
	inline bool FToggleSlider(ConfigVar<bool>& tToggleVar, ConfigVar<float>& tSliderVar, int iToggleFlags = FToggleEnum::None, int iSliderFlags = FSliderEnum::None, const char* sFormatOverride = nullptr, bool* pHovered = nullptr)
	{
		return FToggleSliderVar(tToggleVar, tSliderVar, iToggleFlags, iSliderFlags, sFormatOverride, pHovered);
	}
	inline bool FToggleSlider(ConfigVar<bool>& tToggleVar, ConfigVar<int>& tSliderVar, int iToggleFlags = FToggleEnum::None, int iSliderFlags = FSliderEnum::None, const char* sFormatOverride = nullptr, bool* pHovered = nullptr)
	{
		return FToggleSliderVar(tToggleVar, tSliderVar, iToggleFlags, iSliderFlags, sFormatOverride, pHovered);
	}

	WRAPPER(FToggle, bool, VA_LIST(int iFlags = 0), VA_LIST(&tVal, iFlags))
	WRAPPER(FToggle, int, VA_LIST(int iBit, int iFlags = 0), VA_LIST(&tVal, iBit, iFlags))
	WRAPPER(FSlider, FloatRange_t, VA_LIST(int iFlags = 0, const char* sFormatOverride = nullptr), VA_LIST(&tVal.Min, &tVal.Max, tVar.m_unMin.f, tVar.m_unMax.f, tVar.m_unStep.f, sFormatOverride ? sFormatOverride : tVar.m_sExtra, iFlags))
	WRAPPER(FSlider, IntRange_t, VA_LIST(int iFlags = 0, const char* sFormatOverride = nullptr), VA_LIST(&tVal.Min, &tVal.Max, tVar.m_unMin.i, tVar.m_unMax.i, tVar.m_unStep.i, sFormatOverride ? sFormatOverride : tVar.m_sExtra, iFlags))
	WRAPPER(FSlider, float, VA_LIST(int iFlags = 0, const char* sFormatOverride = nullptr), VA_LIST(&tVal, tVar.m_unMin.f, tVar.m_unMax.f, tVar.m_unStep.f, sFormatOverride ? sFormatOverride : tVar.m_sExtra, iFlags))
	WRAPPER(FSlider, int, VA_LIST(int iFlags = 0, const char* sFormatOverride = nullptr), VA_LIST(&tVal, tVar.m_unMin.i, tVar.m_unMax.i, tVar.m_unStep.i, sFormatOverride ? sFormatOverride : tVar.m_sExtra, iFlags))
	WRAPPER(FDropdown, int, VA_LIST(int iFlags = 0, int iSizeOffset = 0), VA_LIST(&tVal, tVar.m_vValues, {}, iFlags, iSizeOffset, tVar.m_sExtra ? tVar.m_sExtra : "None"))
	WRAPPER(FDropdown, int, VA_LIST(std::vector<const char*> vEntries, std::vector<int> vValues = {}, int iFlags = 0, int iSizeOffset = 0), VA_LIST(&tVal, vEntries, vValues, iFlags, iSizeOffset, tVar.m_sExtra ? tVar.m_sExtra : "None"))
	WRAPPER(FSDropdown, std::string, VA_LIST(int iFlags = 0, int iSizeOffset = 0), VA_LIST(&tVal, tVar.m_vValues, iFlags, iSizeOffset))
	WRAPPER(FSDropdown, int, VA_LIST(int iFlags = 0, int iSizeOffset = 0), VA_LIST(&tVal, tVar.m_vValues, iFlags, iSizeOffset))
	WRAPPER(FColorPicker, Color_t, VA_LIST(int iFlags = 0, ImVec2 vOffset = {}, ImVec2 vSize = { H::Draw.Scale(22), H::Draw.Scale(12) }, ImVec2 vIconOffset = {}), VA_LIST(&tVal, iFlags, vOffset, vSize, vIconOffset))
	WRAPPER(FColorPicker, Gradient_t, VA_LIST(bool bStart = true, int iFlags = 0, ImVec2 vOffset = {}, ImVec2 vSize = { H::Draw.Scale(22), H::Draw.Scale(12) }, ImVec2 vIconOffset = {}), VA_LIST(bStart ? &tVal.StartColor : &tVal.EndColor, iFlags, vOffset, vSize, vIconOffset))

	inline void FKeybind(ConfigVar<int>& tVar, int iFlags = FKeybindEnum::None, std::vector<int> vIgnore = { Vars::Menu::PrimaryKey[DEFAULT_BIND], Vars::Menu::SecondaryKey[DEFAULT_BIND] }, ImVec2 vSize = { 0, 30 }, int iSizeOffset = 0, bool* pHovered = nullptr)
	{
		FKeybind(tVar.m_vNames.front(), tVar[DEFAULT_BIND], iFlags, vIgnore, vSize, iSizeOffset, pHovered);
	}

	inline bool dropdown_with_group_headers(ConfigVar<int>& t_var, int flags, int size_offset, std::initializer_list<std::tuple<int, const char*, int>> group_headers)
	{
		const char* label = t_var.m_vNames.front();
		int variable_flags = t_var.m_iFlags & ~(VISUAL | NOSAVE | NOBIND | DEBUGVAR);
		flags |= variable_flags;
		int value = FGet(t_var, true);
		bool changed = FDropdown(std::format("{}## {}", label, t_var.Name()).c_str(), &value, t_var.m_vValues, {}, flags, size_offset, t_var.m_sExtra ? t_var.m_sExtra : "None", nullptr, nullptr, group_headers);
		FSet(t_var, value);
		return changed;
	}
}
