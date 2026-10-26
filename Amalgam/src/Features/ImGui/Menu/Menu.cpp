#include "Menu.h"

#ifndef TEXTMODE

#include "Components.h"
#include "Pages.h"
#include "../Notifications/Notifications.h"
#include "../../Configs/Configs.h"
#include "../../Binds/Binds.h"
#include "../../Visuals/Groups/Groups.h"
#include "../../Players/PlayerUtils.h"
#include "../../Players/SteamProfileCache.h"
#include "../../Spectate/Spectate.h"
#include "../../Resolver/Resolver.h"
#include "../../Visuals/Visuals.h"
#include "../../Visuals/SkinChanger/SkinChanger.h"
#include "../../Visuals/SkinChanger/SkinData.h"
#include "../../Misc/Misc.h"
#include "../../Output/Output.h"
#include "../../World/World.h"

#include "../../../SDK/Definitions/Main/IMaterial.h"
#include "../../../SDK/Definitions/Main/IMaterialVar.h"
#include "../../../SDK/Definitions/Main/ITexture.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <wrl/client.h>

static bool RoleDropdown(const char* sLabel, std::vector<int>& vRoles)
{
	using namespace ImGui;

	std::vector<int> vValidRoles = {};
	std::vector<const char*> vEntries = {};
	for (int i = 0; i < F::PlayerUtils.m_vTags.size(); i++)
	{
		auto& tTag = F::PlayerUtils.m_vTags[i];
		if (tTag.m_bLabel)
			continue;

		vValidRoles.push_back(i);
		vEntries.push_back(tTag.m_sName.c_str());
	}

	for (auto it = vRoles.begin(); it != vRoles.end();)
	{
		if (std::ranges::find(vValidRoles, *it) == vValidRoles.end())
			it = vRoles.erase(it);
		else
			++it;
	}

	std::vector<std::string> vSelected = {};
	for (int iRole : vRoles)
	{
		if (auto pTag = F::PlayerUtils.GetTag(iRole))
			vSelected.push_back(pTag->m_sName);
	}

	if (Transparent || Disabled)
		PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

	bool bReturn = false;
	auto tRow = BeginFieldRow(sLabel, FDropdownEnum::None, 0);
	if (BeginFieldPopup(tRow))
	{
		DebugDummy({ 0, H::Draw.Scale(2) });
		for (int i = 0; i < vEntries.size(); i++)
		{
			const int iRole = vValidRoles[i];
			const bool bActiveRole = std::ranges::find(vRoles, iRole) != vRoles.end();
			if (FOption(std::format("##{}{}", vEntries[i], i).c_str(), vEntries[i], bActiveRole, true, ImGuiSelectableFlags_DontClosePopups))
			{
				if (bActiveRole)
					std::erase(vRoles, iRole);
				else
					vRoles.push_back(iRole);
				bReturn = true;
			}
		}
		DebugDummy({ 0, H::Draw.Scale(2) });
		EndFieldPopup();
	}
	EndFieldRow(sLabel, tRow, ListPreview(vSelected, tRow.m_tLayout.m_vFieldSize.x - H::Draw.Scale(34), "All"), true);

	if (Transparent || Disabled)
		PopStyleVar();

	return bReturn;
}

void CMenu::DrawMenu()
{
	using namespace ImGui;

	Bind_t tBind;
	if (!F::Binds.GetBind(CurrentBind, &tBind))
		CurrentBind = DEFAULT_BIND;

	MenuShell_t tShell = {};
	tShell.m_sTitle = Vars::Menu::CheatTitle.Value.c_str();
	tShell.m_sEditingBind = CurrentBind != DEFAULT_BIND ? tBind.m_sName.c_str() : nullptr;
	tShell.m_sBuild = "Built " __DATE__ ", " __TIME__;
	tShell.m_sVariant = __CONFIGURATION__ " " __PLATFORM__;
	tShell.m_fPage = [this](int iPage, int iSub)
	{
		if (Pages::Draw(iPage, iSub))
			return;

		switch (iPage)
		{
		case MenuPageVisuals: MenuESP(); break;
		case MenuPageAnticheat: MenuAnticheat(iSub); break;
		case MenuPagePlayers: MenuLogs(iSub); break;
		case MenuPageSkins: MenuSkins(iSub); break;
		case MenuPageSettings: MenuSettings(iSub); break;
		}
	};
	tShell.m_fSearch = Pages::Search;
	tShell.m_fStopEditingBind = [] { CurrentBind = DEFAULT_BIND; };
	Shell::Draw(vMenuPages, tShell);
}

#pragma region Tabs
void CMenu::MenuESP()
{
	using namespace ImGui;

	static size_t iCurrentGroup = 0;

	if (Section("Groups"))
	{
		static std::string sStaticName;
		const float flPad = GetStyle().WindowPadding.x, flGap = H::Draw.Scale(8);
		const float flWidth = GetWindowWidth() - flPad * 2;

		PushDisabled(F::Groups.m_vGroups.size() >= MAX_GROUPS);
		{
			const float flButton = H::Draw.Scale(28);
			const bool bEnter = FInputText("New group", sStaticName, flWidth - flButton - H::Draw.Scale(4), ImGuiInputTextFlags_EnterReturnsTrue);
			SameLine(0, H::Draw.Scale(4));
			PushDisabled(Disabled || sStaticName.empty());
			{
				if (FIconButton(ICON_MD_ADD, "Create", flButton) || bEnter && !Disabled)
				{
					F::Groups.m_vGroups.emplace_back(sStaticName);
					sStaticName.clear();

					iCurrentGroup = F::Groups.m_vGroups.size() - 1;
				}
			}
			PopDisabled();
		}
		PopDisabled();
		DebugDummy({ 0, H::Draw.Scale(4) });

		FDropdown(Vars::ESP::ActiveGroups, FDropdownEnum::Multi);

		const int iCount = int(F::Groups.m_vGroups.size());
		if (iCount)
		{
			DebugDummy({ 0, H::Draw.Scale(8) });

			const int iColumns = std::clamp(int((flWidth + flGap) / H::Draw.Scale(170)), 1, 4);
			const float flChipWidth = floorf((flWidth - flGap * (iColumns - 1)) / iColumns), flChipHeight = H::Draw.Scale(32);
			const float flGridY = GetCursorPosY();
			auto fPositionToIndex = [&](ImVec2 vPos)
			{
				const int iColumn = std::clamp(int((vPos.x - flPad) / (flChipWidth + flGap)), 0, iColumns - 1);
				const int iRow = std::max(int((vPos.y - flGridY) / (flChipHeight + flGap)), 0);
				return std::clamp(iRow * iColumns + iColumn, 0, iCount - 1);
			};

			static int iDragging = -1;
			if (!IsMouseDown(ImGuiMouseButton_Left))
				iDragging = -1;
			else if (iDragging != -1)
			{
				int iTo = fPositionToIndex(GetMousePos() - GetDrawPos());
				if (iDragging != iTo)
				{
					F::Groups.Move(iDragging, iTo);
					if (iCurrentGroup == iDragging)
						iCurrentGroup = iTo;
					else if (iCurrentGroup < iDragging && iCurrentGroup >= iTo)
						iCurrentGroup++;
					else if (iCurrentGroup > iDragging && iCurrentGroup <= iTo)
						iCurrentGroup--;
					iDragging = iTo;
				}
			}

			ImDrawList* pDrawList = GetWindowDrawList();
			for (auto it = F::Groups.m_vGroups.begin(); it < F::Groups.m_vGroups.end();)
			{
				int iGroup = int(std::distance(F::Groups.m_vGroups.begin(), it));
				auto& tGroup = *it;

				const ImVec2 vPos = { flPad + (iGroup % iColumns) * (flChipWidth + flGap), flGridY + (iGroup / iColumns) * (flChipHeight + flGap) };
				const ImVec2 vMin = GetDrawPos() + vPos, vMax = vMin + ImVec2(flChipWidth, flChipHeight);
				const bool bSelected = iCurrentGroup == iGroup, bEnabled = Vars::ESP::ActiveGroups.Value & 1 << iGroup;
				const bool bWithin = IsWindowHovered() && IsMouseWithin(vMin.x, vMin.y, flChipWidth, flChipHeight);
				const float flHover = Anim(GetID(std::format("##Group{}", iGroup).c_str()), 0, bWithin ? 1.f : 0.f);

				DrawColorChip(pDrawList, vMin, vMax, ColorByteToVec(tGroup.m_tColor), tGroup.m_sName, bSelected, bEnabled, flHover, flChipWidth - H::Draw.Scale(bWithin ? 46 : 22));

				PushID(iGroup);
				SetCursorPos(vPos);
				SetNextItemAllowOverlap();
				bool bClicked = InvisibleButton("##Group", { flChipWidth, flChipHeight });
				const bool bChipHovered = IsItemHovered();
				bool bDelete = false, bDuplicate = false;
				if (bWithin)
				{
					SetCursorPos(vPos + ImVec2(flChipWidth - H::Draw.Scale(28), H::Draw.Scale(4)));
					bDelete = FIconButton(ICON_MD_DELETE, "Delete", H::Draw.Scale(24));
				}
				if (bWithin)
					SetMouseCursor(ImGuiMouseCursor_Hand);

				if (bClicked && !bDelete)
					iCurrentGroup = iGroup;
				else if (bWithin && IsMouseReleased(ImGuiMouseButton_Right))
					OpenPopup("##Menu");
				else if (iDragging == -1 && bChipHovered && IsMouseDown(ImGuiMouseButton_Left))
					iDragging = iGroup;

				if (FBeginContextMenu("##Menu"))
				{
					static std::string sInput = "";

					FMenuCaption("Name");
					SetCursorPosX(GetCursorPosX() + H::Draw.Scale(4));
					bool bEnter = FInputText("Name", sInput, std::max(GetContentRegionAvail().x - H::Draw.Scale(8), H::Draw.Scale(180)), ImGuiInputTextFlags_EnterReturnsTrue);
					if (!IsItemFocused())
						sInput = tGroup.m_sName;
					if (bEnter)
						tGroup.m_sName = sInput;
					Dummy({ 0, H::Draw.Scale(4) });

					FMenuSeparator();
					PushDisabled(F::Groups.m_vGroups.size() >= MAX_GROUPS);
					{
						bDuplicate = FMenuItem("Duplicate", ICON_MD_CONTENT_COPY);
					}
					PopDisabled();
					if (FMenuItem("Delete", ICON_MD_DELETE, F::Render.Danger, FMenuItemEnum::TintLabel))
						bDelete = true;

					FEndContextMenu();
				}
				PopID();

				if (bDelete)
					it = F::Groups.m_vGroups.erase(it);
				else if (bDuplicate)
				{
					it = F::Groups.m_vGroups.insert(it + 1, tGroup);
					it->m_sName += " duplicate";
				}
				else
					++it;
			}

			const int iRows = (iCount + iColumns - 1) / iColumns;
			SetCursorPos({ flPad, flGridY });
			DebugDummy({ flWidth, iRows * (flChipHeight + flGap) - flGap });
		}
	} EndSection();

	if (F::Groups.m_vGroups.empty() || !PageTable("VisualsESPTable", 2))
		return;

	iCurrentGroup = std::clamp(iCurrentGroup, 0ui64, F::Groups.m_vGroups.size() - 1);
	auto& tGroup = F::Groups.m_vGroups[iCurrentGroup];

	TableNextColumn();
	if (Section("Group"))
	{
		FColorPicker("Color", &tGroup.m_tColor, FColorPickerEnum::Full);
		FToggle("Tags override color", &tGroup.m_bTagsOverrideColor);
		FDropdown("Targets", &tGroup.m_iTargets, { "Players", "Buildings", "Projectiles", "Ragdolls", "Objective", "NPCs", "Health", "Ammo", "Money", "Powerups", "Spellbook", "Bombs", "Gargoyle", "##Divider", "Fake angle", "Viewmodel weapon", "Viewmodel hands" }, {}, FDropdownEnum::Multi);
	} EndSection();
	if (Section("Conditions"))
	{
		FDropdown("Conditions", &tGroup.m_iConditions, { "Enemy", "Team", "BLU", "RED", "##Divider", "Local", "Friends", "Party", "Priority", "Target", "##Divider", "Dormant" }, {}, FDropdownEnum::Multi);
		PushTransparent(!(tGroup.m_iTargets & TargetsEnum::Players));
		{
			RoleDropdown("Roles", tGroup.m_vRoles);
			FDropdown("Players", &tGroup.m_iPlayers, { "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy", "##Divider", "Invulnerable", "Crits", (tGroup.m_iPlayers & PlayerEnum::NotInvis) ? "Not invisible" : "Invisible", "Disguise", "Hurt", "##Divider", "Invisible -> Not invisible" }, {}, FDropdownEnum::Multi, 0, "All");
		}
		PopTransparent();
		PushTransparent(!(tGroup.m_iTargets & TargetsEnum::Buildings));
		{
			FDropdown("Buildings", &tGroup.m_iBuildings, { "Sentry", "Dispenser", "Teleporter", "##Divider", "Hurt" }, {}, FDropdownEnum::Multi, 0, "All");
		}
		PopTransparent();
		PushTransparent(!(tGroup.m_iTargets & TargetsEnum::Projectiles));
		{
			FDropdown("Projectiles", &tGroup.m_iProjectiles, { "Rocket", "Sticky", "Pipe", "Arrow", "Heal", "Flare", "Fire", "Repair", "Cleaver", "Milk", "Jarate", "Gas", "Bauble", "Baseball", "Energy", "Short circuit", "Meteor shower", "Lightning", "Fireball", "Bomb", "Bats", "Pumpkin", "Monoculus", "Skeleton", "Misc", "##Divider", "Crit", "Minicrit" }, {}, FDropdownEnum::Multi, 0, "All");
		}
		PopTransparent();
	} EndSection();
	if (Section("Misc"))
	{
		FToggle("Offscreen arrows", &tGroup.m_bOffscreenArrows);
		if (FPopupButton("OffscreenArrows"))
		{
			FSlider("Offset", &tGroup.m_iOffscreenArrowsOffset, 0, 1000, 25, "%i", FSliderEnum::Precision);
			FSlider("Max distance", &tGroup.m_flOffscreenArrowsMaxDistance, 0.f, 5000.f, 50.f, "%g", FSliderEnum::Min | FSliderEnum::Precision);

			EndPopup();
		}

		FToggle("Pickup timer", &tGroup.m_bPickupTimer);

		FToggle("Backtrack", &tGroup.m_iBacktrack, BacktrackEnum::Enabled);
		if (FPopupButton("Backtrack"))
		{
			FDropdown("Draw##Backtrack", &tGroup.m_iBacktrack, { "Last", "First", "##Divider", "Always" }, { BacktrackEnum::Last, BacktrackEnum::First, BacktrackEnum::Always }, FDropdownEnum::Multi | FDropdownEnum::NoSanitization, 0, "All");
			FMDropdown("Visible material## Backtrack", &tGroup.m_tBacktrackChams.Visible);
			FMDropdown("Occluded material## Backtrack", &tGroup.m_tBacktrackChams.Occluded);

			Subheader("Glow");
			PushTransparent(!tGroup.m_tBacktrackGlow.Stencil);
			{
				FSlider("Stencil scale## Backtrack", &tGroup.m_tBacktrackGlow.Stencil, 0, 10, 1, "%i", FSliderEnum::Min);
			}
			PopTransparent();
			PushTransparent(!tGroup.m_tBacktrackGlow.Blur);
			{
				FSlider("Blur scale## Backtrack", &tGroup.m_tBacktrackGlow.Blur, 0.f, 10.f, 1.f, "%g", FSliderEnum::Min | FSliderEnum::Precision);
			}
			PopTransparent();
			FSlider("Render start## Backtrack", &tGroup.m_tBacktrackGlow.Start, 0.f, 2048.f, 128, "%.fHU", FSliderEnum::Min);
			FSlider("Render end## Backtrack", &tGroup.m_tBacktrackGlow.End, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Min);
			FToggle("Distance to alpha## Backtrack", &tGroup.m_tBacktrackGlow.SmoothAlpha);

			EndPopup();
		}

		FToggle("Trajectory", &tGroup.m_iTrajectory, TrajectoryEnum::Enabled);
		if (FPopupButton("Trajectory"))
		{
			FDropdown("Flags", &tGroup.m_iTrajectory, { "Predict", "##Divider", "Radius", "Trace", "Sphere", "##Divider", "Path" }, { TrajectoryEnum::Predict, TrajectoryEnum::Radius, TrajectoryEnum::Trace, TrajectoryEnum::Sphere, TrajectoryEnum::Path }, FDropdownEnum::Multi | FDropdownEnum::NoSanitization);
			FToggle("Ignore Z", &tGroup.m_iTrajectory, SightlinesEnum::IgnoreZ);

			EndPopup();
		}

		FToggle("Sightlines", &tGroup.m_iSightlines, SightlinesEnum::Enabled);
		if (FPopupButton("Sightlines"))
		{
			FToggle("Ignore Z", &tGroup.m_iSightlines, SightlinesEnum::IgnoreZ);

			EndPopup();
		}
	} EndSection();

	TableNextColumn();
	if (Section("ESP"))
	{
		std::vector<const char*> vEntries = { "Name", "Name background", "Box", "Distance" };
		std::vector<int> vValues = { ESPEnum::Name, ESPEnum::NameBackground, ESPEnum::Box, ESPEnum::Distance };
		if (tGroup.m_iTargets & TargetsEnum::Players)
		{
			vEntries.insert(vEntries.end(), { "Bones" });
			vValues.insert(vValues.end(), { ESPEnum::Bones });
		}
		if (tGroup.m_iTargets & (TargetsEnum::Players | TargetsEnum::Buildings))
		{
			vEntries.insert(vEntries.end(), { "Health bar", "Health text" });
			vValues.insert(vValues.end(), { ESPEnum::HealthBar, ESPEnum::HealthText });
		}
		if (tGroup.m_iTargets & TargetsEnum::Players)
		{
			vEntries.insert(vEntries.end(), { "Uber bar", "Uber text", "Class icon", "Class text", "Weapon icon", "Weapon text", "Priority", "Labels", "Buffs", "Debuffs" });
			vValues.insert(vValues.end(), { ESPEnum::UberBar, ESPEnum::UberText, ESPEnum::ClassIcon, ESPEnum::ClassText, ESPEnum::WeaponIcon, ESPEnum::WeaponText, ESPEnum::Priority, ESPEnum::Labels, ESPEnum::Buffs, ESPEnum::Debuffs });
		}
		if (tGroup.m_iTargets & (TargetsEnum::Players | TargetsEnum::Buildings | TargetsEnum::Projectiles | TargetsEnum::Objective))
		{
			vEntries.insert(vEntries.end(), { "Flags" });
			vValues.insert(vValues.end(), { ESPEnum::Flags });
		}
		if (tGroup.m_iTargets & TargetsEnum::Players)
		{
			vEntries.insert(vEntries.end(), { "Lag compensation", "Ping", "KDR", "That's how mafia works" });
			vValues.insert(vValues.end(), { ESPEnum::LagCompensation, ESPEnum::Ping, ESPEnum::KDR, ESPEnum::ThatsHowMafiaWorks });
		}
		if (tGroup.m_iTargets & (TargetsEnum::Buildings | TargetsEnum::Projectiles))
		{
			vEntries.insert(vEntries.end(), { "Owner" });
			vValues.insert(vValues.end(), { ESPEnum::Owner });
		}
		if (tGroup.m_iTargets & TargetsEnum::Buildings)
		{
			vEntries.insert(vEntries.end(), { "Level", "Ammo bars", "Ammo text" });
			vValues.insert(vValues.end(), { ESPEnum::Level, ESPEnum::AmmoBars, ESPEnum::AmmoText });
		}
		if (tGroup.m_iTargets & TargetsEnum::Objective)
		{
			vEntries.insert(vEntries.end(), { "Intel return time" });
			vValues.insert(vValues.end(), { ESPEnum::IntelReturnTime });
		}

		PushTransparent(tGroup.m_iTargets && !(tGroup.m_iTargets & TargetsEnum::ESP));
		{
			FDropdown("Draw", &tGroup.m_tESP.Draw, vEntries, vValues, FDropdownEnum::Multi);
			PushTransparent(Transparent || !(tGroup.m_tESP.Draw & ESPEnum::Box));
			{
				FDropdown("Box style", &tGroup.m_tESP.BoxStyle, { "Solid", "Corner accent", "Corners only", "Outline", "Rounded" });
			}
			PopTransparent();
			PushTransparent(Transparent || !(tGroup.m_tESP.Draw & ESPEnum::NameBackground));
			{
				int iOpacity = tGroup.m_tESP.BackgroundOpacity;
				FSlider("Background opacity", &iOpacity, 0, 255, 5, "%i", FSliderEnum::Clamp);
				tGroup.m_tESP.BackgroundOpacity = byte(iOpacity);
			}
			PopTransparent();
			FSlider("Draw start## ESP", &tGroup.m_tESP.Start, 0.f, 2048.f, 128.f, "%.fHU", FSliderEnum::Clamp);
			FSlider("Draw end## ESP", &tGroup.m_tESP.End, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Min);
			FToggle("Distance to alpha## ESP", &tGroup.m_tESP.SmoothAlpha);
		}
		PopTransparent();
	} EndSection();
	if (Section("Chams"))
	{
		if (!tGroup.m_iTargets || tGroup.m_iTargets & TargetsEnum::Occluded)
		{
			FMDropdown("Visible material", &tGroup.m_tChams.Visible);
			FMDropdown("Occluded material", &tGroup.m_tChams.Occluded);
		}
		else
			FMDropdown("Material", &tGroup.m_tChams.Visible);
	} EndSection();
	if (Section("Glow"))
	{
		PushTransparent(!tGroup.m_tGlow.Stencil);
		{
			FSlider("Stencil scale", &tGroup.m_tGlow.Stencil, 0, 10, 1, "%i", FSliderEnum::Min);
		}
		PopTransparent();
		PushTransparent(!tGroup.m_tGlow.Blur);
		{
			FSlider("Blur scale", &tGroup.m_tGlow.Blur, 0.f, 10.f, 1.f, "%g", FSliderEnum::Min | FSliderEnum::Precision);
		}
		PopTransparent();
		FSlider("Render start## Glow", &tGroup.m_tGlow.Start, 0.f, 2048.f, 128, "%.fHU", FSliderEnum::Clamp);
		FSlider("Render end## Glow", &tGroup.m_tGlow.End, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Min);
		FToggle("Distance to alpha## Glow", &tGroup.m_tGlow.SmoothAlpha);
	} EndSection();

	EndTable();
}

namespace
{
	using namespace ImGui;

	std::string ToLower(std::string sText)
	{
		std::transform(sText.begin(), sText.end(), sText.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		return sText;
	}

	void SteamMenuItems(uint32_t uAccountID)
	{
		const CSteamID tSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual);
		FMenuCaption("Steam");
		if (FMenuItem("Profile", ICON_MD_PERSON))
			I::SteamFriends->ActivateGameOverlayToUser("steamid", tSteamID);
		if (FMenuItem("History", ICON_MD_HISTORY))
			I::SteamFriends->ActivateGameOverlayToWebPage(std::format("https://steamhistory.net/id/{}", tSteamID.ConvertToUint64()).c_str());
		if (FMenuItem("Refetch profile", ICON_MD_SYNC))
		{
			F::SteamProfileCache.Refresh(uAccountID);
			SDK::Output("SteamProfileCache", std::format("Queued native profile refresh for {}", tSteamID.ConvertToUint64()).c_str(), { 175, 150, 255, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
		}
	}

	void SetAlias(uint32_t uAccountID, const char* sName, const std::string& sAlias)
	{
		const bool bHasAlias = F::PlayerUtils.m_mPlayerAliases.contains(uAccountID);
		if (sAlias.empty())
		{
			if (!bHasAlias)
				return;

			F::Output.AliasChanged(sName, "Removed", F::PlayerUtils.m_mPlayerAliases[uAccountID].c_str());
			F::PlayerUtils.m_mPlayerAliases.erase(uAccountID);
		}
		else
		{
			F::PlayerUtils.m_mPlayerAliases[uAccountID] = sAlias;
			F::Output.AliasChanged(sName, bHasAlias ? "Changed" : "Added", sAlias.c_str());
		}
		F::PlayerUtils.m_bSave = true;
	}
}

void CMenu::MenuAnticheat(int iTab)
{
	using namespace ImGui;

	if (iTab != 0)
		return;

	static std::string sSearch = "";
	static std::vector<std::pair<uint32_t, CheaterRecord_t>> vCache = {};
	static std::string sCacheSearch = "";
	static double dCacheTime = -1.0;
	static bool bCacheDirty = true;

	if (bCacheDirty || sCacheSearch != sSearch || GetTime() - dCacheTime > 1.0)
	{
		auto vFound = F::PlayerUtils.GetCheaterVector();
		const std::string sLowered = ToLower(sSearch);
		if (!sLowered.empty())
		{
			vFound.erase(std::remove_if(vFound.begin(), vFound.end(), [&](const std::pair<uint32_t, CheaterRecord_t>& tEntry)
				{
					return ToLower(std::format("{} {} {}", tEntry.second.m_sName, tEntry.second.m_sReason, tEntry.first)).find(sLowered) == std::string::npos;
				}), vFound.end());
		}
		std::sort(vFound.begin(), vFound.end(), [](const auto& a, const auto& b) -> bool
			{
				if (a.second.m_bAuto != b.second.m_bAuto)
					return a.second.m_bAuto > b.second.m_bAuto;
				if (a.second.m_iDetections != b.second.m_iDetections)
					return a.second.m_iDetections > b.second.m_iDetections;
				return a.second.m_sName < b.second.m_sName;
			});

		vCache = std::move(vFound);
		sCacheSearch = sSearch;
		dCacheTime = GetTime();
		bCacheDirty = false;
	}

	auto getReasonColor = [](const CheaterRecord_t& tRecord) -> Color_t
		{
			if (!tRecord.m_bAuto)
				return { 185, 185, 185, 255 };

			const std::string sReason = ToLower(tRecord.m_sReason);
			auto contains = [&](std::string_view sNeedle) -> bool { return sReason.find(sNeedle) != std::string::npos; };

			if (contains("crit"))
				return { 210, 140, 255, 255 };
			if (contains("lag") || contains("burst"))
				return { 255, 180, 130, 255 };
			if (contains("chok"))
				return { 255, 150, 150, 255 };
			if (contains("flick") || contains("aim"))
				return { 140, 190, 255, 255 };
			if (contains("duck"))
				return { 255, 210, 150, 255 };
			if (contains("pitch"))
				return { 150, 210, 210, 255 };

			return { 255, 120, 120, 255 };
		};

	if (Section("##Cheaters"))
	{
		FToolbarSearch(sSearch, 4);
		PushDisabled(F::PlayerUtils.m_bCheaterLoad);
		{
			if (FToolbarButton(ICON_MD_SYNC, "Reload"))
				F::PlayerUtils.m_bCheaterLoad = true;
			if (FToolbarButton(ICON_MD_UPLOAD, "Copy to clipboard"))
			{
				SDK::SetClipboard(F::PlayerUtils.ExportCheatersToJson());
				SDK::Output("unibox", "Copied cheaterlist to clipboard", { 255, 150, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
			}
			if (FToolbarButton(ICON_MD_DOWNLOAD, "Import from clipboard"))
			{
				if (F::PlayerUtils.ImportCheatersFromJson(SDK::GetClipboard(), true))
					SDK::Output("unibox", "Imported cheaterlist", { 255, 150, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
				else
					SDK::Output("unibox", "Failed to import cheaterlist", { 255, 150, 150, 127 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
				bCacheDirty = true;
			}
		}
		PopDisabled();
		if (FToolbarButton(ICON_MD_FOLDER_OPEN, "Open folder"))
			ShellExecuteA(NULL, NULL, F::Configs.m_sCorePath.c_str(), NULL, NULL, SW_SHOWNORMAL);

		const float flTop = GetCursorPosY() + H::Draw.Scale(10);
		if (vCache.empty())
			FEmptyState(sSearch.empty() ? "No cheaters yet" : "No matches", flTop);
		else
		{
			FCardGrid(vCache, flTop, [&](const std::pair<uint32_t, CheaterRecord_t>& tEntry, ImVec2 vPos, ImVec2 vSize) -> bool
				{
					const auto& tRecord = tEntry.second;
					const std::string sName = tRecord.m_sName.empty() ? std::format("{}", tEntry.first) : tRecord.m_sName;
					const std::string sReason = !tRecord.m_bAuto ? "Tagged manually" : tRecord.m_sReason.empty() ? "Detected" : tRecord.m_sReason;
					const std::string sBadge = tRecord.m_bAuto ? std::format("{}", std::max(0, tRecord.m_iDetections)) : "";

					PushID(int(tEntry.first));
					auto tCard = FProfileCard(reinterpret_cast<ImTextureID>(F::SteamProfileCache.GetAvatarTexture(tEntry.first)), sName, sReason, ColorByteToVec(getReasonColor(tRecord)), sBadge, "Remove tag", vPos, vSize);
					if (tCard.m_bMenu)
						OpenPopup("##Menu");
					if (FBeginContextMenu("##Menu"))
					{
						SteamMenuItems(tEntry.first);
						FMenuSeparator();
						if (FMenuItem("Remove tag", ICON_MD_DELETE, F::Render.Danger, FMenuItemEnum::TintLabel))
							tCard.m_bRemove = true;
						FEndContextMenu();
					}
					PopID();

					if (tCard.m_bRemove)
					{
						F::PlayerUtils.RemoveTag(tEntry.first, F::PlayerUtils.TagToIndex(CHEATER_TAG), true, sName.c_str());
						bCacheDirty = true;
					}
					return tCard.m_bRemove;
				});
		}
	} EndSection();
}

void CMenu::MenuLogs(int iTab)
{
	using namespace ImGui;

	switch (iTab)
	{
	case 0:
	{
		if (!I::EngineClient->IsInGame())
		{
			if (Section("##Lobby"))
				FEmptyState("Not in game", GetCursorPosY());
			EndSection();
			break;
		}

		std::shared_lock tLock(F::PlayerUtils.m_tMutex);
		const auto vPlayers = F::PlayerUtils.m_vPlayerCache;
		tLock.unlock();

		std::unordered_map<uint64_t, std::vector<const ListPlayer_t*>> mParties = {};
		int iPartyCount = 0;
		for (auto& tPlayer : vPlayers)
		{
			if (tPlayer.m_iParty)
			{
				mParties[tPlayer.m_iParty].push_back(&tPlayer);
				iPartyCount = std::max(iPartyCount, tPlayer.m_iParty);
			}
		}

		struct Pill_t
		{
			std::string m_sName;
			Color_t m_tColor;
			int m_iID;
		};

		auto fDrawPlayer = [&](const ListPlayer_t& tPlayer)
		{
			const float flWidth = GetWindowWidth() - GetStyle().WindowPadding.x * 2, flHeight = H::Draw.Scale(30);
			const ImVec2 vPos = GetCursorPos(), vMin = GetDrawPos() + vPos, vMax = vMin + ImVec2(flWidth, flHeight);
			ImDrawList* pDrawList = GetWindowDrawList();
			const bool bSpectated = F::Spectate.GetTarget(true) == tPlayer.m_iUserID;

			PushID(tPlayer.m_iUserID);
			SetNextItemAllowOverlap();
			InvisibleButton("##Player", { flWidth, flHeight });
			const bool bWithin = IsWindowHovered() && IsMouseHoveringRect(vMin, vMax);
			const float flHover = Anim(GetItemID(), 0, bWithin || IsPopupOpen("##Menu") ? 1.f : 0.f);
			if (flHover > 0.01f)
				pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Background1p5, flHover), H::Draw.Scale(5));

			const char* sIcon = tPlayer.m_bLocal ? ICON_MD_PERSON : bSpectated ? ICON_MD_VISIBILITY : tPlayer.m_bFriend ? ICON_MD_GROUP : tPlayer.m_bParty ? ICON_MD_GROUPS : nullptr;
			if (sIcon)
			{
				ImFont* pIcon = F::Render.IconFont;
				pDrawList->AddText(pIcon, pIcon->LegacySize, { vMin.x + H::Draw.Scale(8), floorf((vMin.y + vMax.y - pIcon->LegacySize) / 2) }, FadeColor(tPlayer.m_bLocal ? F::Render.Accent : F::Render.Inactive), sIcon);
			}

			std::vector<Pill_t> vPills = {};
			if (!tPlayer.m_bFake)
			{
				if (int iParty = tPlayer.m_iParty)
				{
					auto& tTag = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)];
					if (!--iParty)
						vPills.emplace_back(tTag.m_sName, tTag.m_tColor, 0);
					else
						vPills.emplace_back(std::format("{} {}", tTag.m_sName, iParty), tTag.m_tColor.HueShift(iParty * 360.f / iPartyCount), 0);
				}
				if (tPlayer.m_bF2P)
				{
					auto& tTag = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(F2P_TAG)];
					vPills.emplace_back(tTag.m_sName, tTag.m_tColor, 0);
				}
				for (auto& iID : F::PlayerUtils.GetPlayerTags(tPlayer.m_uAccountID))
				{
					if (auto pTag = F::PlayerUtils.GetTag(iID))
						vPills.emplace_back(pTag->m_sName, pTag->m_tColor, iID);
				}
			}

			ImFont* pSmall = F::Render.FontSmall;
			const float flNameX = vMin.x + H::Draw.Scale(32);
			const float flPillHeight = H::Draw.Scale(18), flPillGap = H::Draw.Scale(4), flClose = H::Draw.Scale(14);
			auto fPillWidth = [&](const std::string& sText, bool bRemovable)
			{
				return pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, sText.c_str()).x + H::Draw.Scale(14) + (bRemovable ? flClose : 0.f);
			};

			const float flNameWidth = std::min(CalcTextSize(tPlayer.m_sName.c_str()).x, std::max(flWidth / 2, H::Draw.Scale(80)));
			const float flRoom = vMax.x - H::Draw.Scale(8) - (flNameX + flNameWidth + H::Draw.Scale(12));
			std::vector<std::pair<const Pill_t*, float>> vShown = {};
			float flUsed = 0.f;
			size_t iHidden = 0;
			for (size_t i = 0; i < vPills.size(); i++)
			{
				const float flPill = fPillWidth(vPills[i].m_sName, vPills[i].m_iID);
				const float flMore = i + 1 < vPills.size() ? fPillWidth(std::format("+{}", vPills.size() - i - 1), false) + flPillGap : 0.f;
				if (flUsed + flPill + flMore > flRoom)
				{
					iHidden = vPills.size() - i;
					break;
				}
				vShown.emplace_back(&vPills[i], flPill);
				flUsed += flPill + flPillGap;
			}
			if (iHidden)
				flUsed += fPillWidth(std::format("+{}", iHidden), false) + flPillGap;

			const ImVec4 tName = tPlayer.m_bAlive ? F::Render.Active.Value : MixColor(F::Render.Inactive, F::Render.Active, 0.35f);
			const std::string sName = TruncateText(tPlayer.m_sName, int(vMax.x - H::Draw.Scale(8) - flUsed - flNameX - H::Draw.Scale(4)));
			pDrawList->AddText({ flNameX, floorf((vMin.y + vMax.y - GetTextLineHeight()) / 2) }, FadeColor(tName), sName.c_str());

			float flPillX = vMax.x - H::Draw.Scale(8) - flUsed + flPillGap;
			const float flPillY = floorf((vMin.y + vMax.y - flPillHeight) / 2);
			auto fDrawPill = [&](const std::string& sText, ImVec4 tColor, float flPillWidth, bool bRemovable) -> bool
			{
				const ImVec2 vPillMin = { floorf(flPillX), flPillY }, vPillMax = vPillMin + ImVec2(flPillWidth, flPillHeight);
				bool bRemove = false, bHovered = false;
				if (bRemovable)
				{
					SetCursorScreenPos(vPillMin);
					bRemove = InvisibleButton(std::format("##Tag{}", sText).c_str(), vPillMax - vPillMin);
					bHovered = IsItemHovered();
					if (bHovered)
						SetMouseCursor(ImGuiMouseCursor_Hand);
				}
				pDrawList->AddRectFilled(vPillMin, vPillMax, FadeColor(tColor, bHovered ? 0.26f : 0.15f), flPillHeight / 2);
				const ImVec4 tText = MixColor(tColor, F::Render.Active, 0.25f);
				pDrawList->AddText(pSmall, pSmall->LegacySize, { vPillMin.x + H::Draw.Scale(7), floorf(vPillMin.y + (flPillHeight - pSmall->LegacySize) / 2) }, FadeColor(tText), sText.c_str());
				if (bRemovable)
				{
					ImFont* pIcon = F::Render.IconFont;
					const float flSize = H::Draw.Scale(12);
					pDrawList->AddText(pIcon, flSize, { vPillMax.x - H::Draw.Scale(7) - flSize, floorf(vPillMin.y + (flPillHeight - flSize) / 2) }, FadeColor(bHovered ? tText : MixColor(tColor, F::Render.Inactive, 0.5f)), ICON_MD_CLOSE);
				}
				flPillX += flPillWidth + flPillGap;
				return bRemove;
			};
			for (auto& [pPill, flPill] : vShown)
			{
				if (fDrawPill(pPill->m_sName, ColorByteToVec(pPill->m_tColor), flPill, pPill->m_iID))
					F::PlayerUtils.RemoveTag(tPlayer.m_uAccountID, pPill->m_iID, true, tPlayer.m_sName.c_str());
			}
			if (iHidden)
			{
				const std::string sMore = std::format("+{}", iHidden);
				fDrawPill(sMore, F::Render.Inactive, fPillWidth(sMore, false), false);
			}

			if (bWithin && IsMouseReleased(ImGuiMouseButton_Right))
				OpenPopup("##Menu");
			if (FBeginContextMenu("##Menu"))
			{
				if (!tPlayer.m_bFake)
				{
					SteamMenuItems(tPlayer.m_uAccountID);
					FMenuSeparator();
				}

				if (FMenuItem(bSpectated ? "Stop spectating" : "Spectate", bSpectated ? ICON_MD_VISIBILITY_OFF : ICON_MD_VISIBILITY))
					F::Spectate.SetTarget(tPlayer.m_iUserID);

				if (!I::EngineClient->IsPlayingDemo())
				{
					bool bKick = false;
					if (FBeginMenu("Votekick", ICON_MD_HOW_TO_VOTE, &bKick))
					{
						static const std::vector<std::pair<const char*, const char*>> vReasons = {
							{ "No reason", "other" },
							{ "Cheating", "cheating" },
							{ "Idle", "idle" },
							{ "Scamming", "scamming" }
						};
						for (auto& [sReason, sCommand] : vReasons)
						{
							if (FMenuItem(sReason))
								I::ClientState->SendStringCmd(std::format("callvote Kick \"{} {}\"", tPlayer.m_iUserID, sCommand).c_str());
						}
						ImGui::EndMenu();
					}
					if (bKick)
					{
						I::ClientState->SendStringCmd(std::format("callvote Kick \"{}\"", tPlayer.m_iUserID).c_str());
						CloseCurrentPopup();
					}
				}

				if (!tPlayer.m_bFake)
				{
					if (FBeginMenu("Add tag", ICON_MD_SELL))
					{
						bool bAny = false;
						for (int iID = 0; iID < int(F::PlayerUtils.m_vTags.size()); iID++)
						{
							auto& tTag = F::PlayerUtils.m_vTags[iID];
							if (!tTag.m_bAssignable || F::PlayerUtils.HasTag(tPlayer.m_uAccountID, iID))
								continue;

							bAny = true;
							if (FMenuItem(tTag.m_sName.c_str(), ICON_MD_LABEL, ColorByteToVec(tTag.m_tColor)))
								F::PlayerUtils.AddTag(tPlayer.m_uAccountID, iID, true, tPlayer.m_sName.c_str());
						}
						if (!bAny)
							FMenuNote("Every tag is assigned");
						ImGui::EndMenu();
					}
					if (FBeginMenu("Alias", ICON_MD_EDIT))
					{
						const bool bHasAlias = F::PlayerUtils.m_mPlayerAliases.contains(tPlayer.m_uAccountID);
						static std::string sInput = "";

						const bool bEnter = FInputText("Alias", sInput, H::Draw.Scale(190), ImGuiInputTextFlags_EnterReturnsTrue);
						if (!IsItemFocused())
							sInput = bHasAlias ? F::PlayerUtils.m_mPlayerAliases[tPlayer.m_uAccountID] : "";
						if (bEnter)
							SetAlias(tPlayer.m_uAccountID, tPlayer.m_sName.c_str(), sInput);
						ImGui::EndMenu();
					}
				}

				if (Vars::Resolver::Enabled.Value && !tPlayer.m_bLocal && !I::EngineClient->IsPlayingDemo())
				{
					FMenuSeparator();
					FMenuCaption("Resolver");
					if (FBeginMenu("Yaw"))
					{
						if (FMenuItem("Auto"))
							F::Resolver.SetYaw(tPlayer.m_iUserID, 0.f, true);
						static const std::vector<std::pair<const char*, float>> vYaws = {
							{ "Forward", 0.f },
							{ "Left", 90.f },
							{ "Right", -90.f },
							{ "Backwards", 180.f }
						};
						for (auto& [sYaw, flValue] : vYaws)
						{
							if (FMenuItem(sYaw))
								F::Resolver.SetYaw(tPlayer.m_iUserID, flValue);
						}
						ImGui::EndMenu();
					}
					if (FBeginMenu("Pitch"))
					{
						if (FMenuItem("Auto"))
							F::Resolver.SetPitch(tPlayer.m_iUserID, 0.f, false, true);
						static const std::vector<std::pair<const char*, float>> vPitches = {
							{ "Up", -90.f },
							{ "Down", 90.f },
							{ "Zero", 0.f }
						};
						for (auto& [sPitch, flValue] : vPitches)
						{
							if (FMenuItem(sPitch))
								F::Resolver.SetPitch(tPlayer.m_iUserID, flValue);
						}
						if (FMenuItem("Inverse"))
							F::Resolver.SetPitch(tPlayer.m_iUserID, 0.f, true);
						ImGui::EndMenu();
					}
					if (FBeginMenu("View"))
					{
						if (FMenuItem("Offset from static view"))
							F::Resolver.SetView(tPlayer.m_iUserID, true);
						if (FMenuItem("Offset from view to local"))
							F::Resolver.SetView(tPlayer.m_iUserID, false);
						ImGui::EndMenu();
					}
					if (FBeginMenu("Minwalk"))
					{
						if (FMenuItem("On"))
							F::Resolver.SetMinwalk(tPlayer.m_iUserID, true);
						if (FMenuItem("Off"))
							F::Resolver.SetMinwalk(tPlayer.m_iUserID, false);
						ImGui::EndMenu();
					}
				}

				const bool bParty = mParties.contains(tPlayer.m_iParty);
				if (bParty || tPlayer.m_iLevel != -2)
					FMenuSeparator();
				if (bParty)
				{
					FMenuCaption("Party");
					for (auto& pMember : mParties[tPlayer.m_iParty])
						FMenuNote(pMember->m_sName.c_str());
				}
				if (tPlayer.m_iLevel != -2)
				{
					if (tPlayer.m_iLevel != -1)
					{
						const int iTier = std::max(int(ceilf(tPlayer.m_iLevel / 150.f)), 1);
						const int iLevel = ((tPlayer.m_iLevel - 1) % 150) + 1;
						FMenuNote(std::format("Tier {}, level {}", iTier, iLevel).c_str());
					}
					else
						FMenuNote("Level unknown");
				}

				FEndContextMenu();
			}
			PopID();

			SetCursorPos({ vPos.x, vPos.y + flHeight + H::Draw.Scale(2) });
		};

		std::vector<const ListPlayer_t*> vBlu, vRed, vOther;
		for (auto& tPlayer : vPlayers)
		{
			switch (tPlayer.m_iTeam)
			{
			case 3: vBlu.push_back(&tPlayer); break;
			case 2: vRed.push_back(&tPlayer); break;
			default: vOther.push_back(&tPlayer); break;
			}
		}

		auto fTeam = [&](const char* sLabel, const std::vector<const ListPlayer_t*>& vTeam)
		{
			if (Section(sLabel))
			{
				if (vTeam.empty())
					FEmptyState("Empty", GetCursorPosY() - H::Draw.Scale(16));
				else
				{
					for (auto pPlayer : vTeam)
						fDrawPlayer(*pPlayer);
					DebugDummy({});
				}
			} EndSection();
		};

		if (PageTable("Lobby", 2))
		{
			TableNextColumn();
			fTeam("BLU", vBlu);
			if (!vOther.empty() && vBlu.size() <= vRed.size())
				fTeam("Unassigned", vOther);
			TableNextColumn();
			fTeam("RED", vRed);
			if (!vOther.empty() && vBlu.size() > vRed.size())
				fTeam("Unassigned", vOther);
			EndTable();
		}
		break;
	}

	case 1:
	{
		static int iID = -1;
		static PriorityLabel_t tTag = {};

		if (!PageTable("Tags", 2))
			break;

		TableNextColumn();
		if (Section("Editor"))
		{
			const int iTag = F::PlayerUtils.IndexToTag(iID);
			const bool bDisableVotePriority = iID != -1 && (iTag == IGNORED_TAG || iTag == FRIEND_TAG || iTag == PARTY_TAG);

			FSDropdown("Name", &tTag.m_sName, {}, FSDropdownEnum::AutoUpdate);
			FColorPicker("Color", &tTag.m_tColor, FColorPickerEnum::Full);
			PushDisabled(iID == DEFAULT_TAG || iID == IGNORED_TAG);
			{
				int iLabel = Disabled ? 0 : tTag.m_bLabel;
				FDropdown("Type", &iLabel, { "Priority", "Label" });
				tTag.m_bLabel = Disabled ? false : iLabel;
			}
			PopDisabled();
			PushTransparent(tTag.m_bLabel);
			{
				FSlider("Priority", &tTag.m_iPriority, -10, 10, 1, "%i");
				FSlider("Followbot priority", &tTag.m_iFollowPriority, -1, 10, 1, tTag.m_iFollowPriority < 0 ? "Ignore" : "%i");
				PushDisabled(bDisableVotePriority);
				{
					FDropdown("Vote priority", &tTag.m_iVotePriority, { "Defend", "Ignore", "Kick" }, { -1, 0, 1 });
				}
				PopDisabled();
			}
			PopTransparent();

			DebugDummy({ 0, H::Draw.Scale(8) });
			if (FButton(iID != -1 ? "Discard" : "Clear", FButtonEnum::Left))
			{
				iID = -1;
				tTag = {};
			}
			PushDisabled(tTag.m_sName.empty());
			{
				if (FButton(iID != -1 ? "Save" : "Create", FButtonEnum::Right | FButtonEnum::Primary))
				{
					F::PlayerUtils.m_bSave = true;
					if (iID > -1 && iID < int(F::PlayerUtils.m_vTags.size()))
					{
						auto& tStored = F::PlayerUtils.m_vTags[iID];
						tStored.m_sName = tTag.m_sName;
						tStored.m_tColor = tTag.m_tColor;
						tStored.m_iPriority = tTag.m_iPriority;
						tStored.m_iFollowPriority = tTag.m_iFollowPriority;
						if (!bDisableVotePriority)
							tStored.m_iVotePriority = tTag.m_iVotePriority;
						tStored.m_bLabel = tTag.m_bLabel;
					}
					else
						F::PlayerUtils.m_vTags.push_back(tTag);

					iID = -1;
					tTag = {};
				}
			}
			PopDisabled();
		} EndSection();

		std::vector<int> vPriorities = {}, vLabels = {};
		for (int i = 0; i < int(F::PlayerUtils.m_vTags.size()); i++)
			(F::PlayerUtils.m_vTags[i].m_bLabel ? vLabels : vPriorities).push_back(i);
		auto fCompare = [](int a, int b) -> bool
		{
			auto& tA = F::PlayerUtils.m_vTags[a];
			auto& tB = F::PlayerUtils.m_vTags[b];
			if (tA.m_iPriority != tB.m_iPriority)
				return tA.m_iPriority > tB.m_iPriority;
			if (tA.m_iFollowPriority != tB.m_iFollowPriority)
				return tA.m_iFollowPriority > tB.m_iFollowPriority;
			if (tA.m_iVotePriority != tB.m_iVotePriority)
				return tA.m_iVotePriority > tB.m_iVotePriority;
			return tA.m_sName < tB.m_sName;
		};
		std::sort(vPriorities.begin(), vPriorities.end(), [&](int a, int b) -> bool
		{
			if (a == DEFAULT_TAG || b == DEFAULT_TAG)
				return a == DEFAULT_TAG && b != DEFAULT_TAG;
			return fCompare(a, b);
		});
		std::sort(vLabels.begin(), vLabels.end(), fCompare);

		auto fDrawTag = [&](int _iID) -> bool
		{
			auto& _tTag = F::PlayerUtils.m_vTags[_iID];
			const float flWidth = GetWindowWidth() - GetStyle().WindowPadding.x * 2, flHeight = H::Draw.Scale(30);
			const ImVec2 vPos = GetCursorPos(), vMin = GetDrawPos() + vPos, vMax = vMin + ImVec2(flWidth, flHeight);
			ImDrawList* pDrawList = GetWindowDrawList();
			const float flCenter = floorf((vMin.y + vMax.y) / 2);

			PushID(_iID);
			SetNextItemAllowOverlap();
			const bool bClicked = InvisibleButton("##Tag", { flWidth, flHeight });
			const bool bWithin = IsWindowHovered() && IsMouseHoveringRect(vMin, vMax);
			const float flHover = Anim(GetItemID(), 0, bWithin || IsPopupOpen("##Menu") ? 1.f : 0.f);
			if (bWithin)
				SetMouseCursor(ImGuiMouseCursor_Hand);
			if (iID == _iID)
				pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.AccentDim), H::Draw.Scale(5));
			else if (flHover > 0.01f)
				pDrawList->AddRectFilled(vMin, vMax, FadeColor(F::Render.Background1p5, flHover), H::Draw.Scale(5));

			pDrawList->AddCircleFilled({ vMin.x + H::Draw.Scale(12), flCenter }, H::Draw.Scale(4), FadeColor(ColorByteToVec(_tTag.m_tColor)), 12);
			float flRight = vMax.x - H::Draw.Scale(36);

			ImFont* pIcon = F::Render.IconFont, *pSmall = F::Render.FontSmall;
			const float flIconSize = H::Draw.Scale(14);
			if (!_tTag.m_bLabel)
			{
				auto fInfo = [&](const char* sInfoIcon, int iValue)
				{
					const std::string sValue = std::format("{}", iValue);
					const float flValue = pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, sValue.c_str()).x;
					flRight -= flValue;
					pDrawList->AddText(pSmall, pSmall->LegacySize, { flRight, floorf(flCenter - pSmall->LegacySize / 2) }, FadeColor(MixColor(F::Render.Inactive, F::Render.Active, 0.35f)), sValue.c_str());
					flRight -= flIconSize + H::Draw.Scale(3);
					pDrawList->AddText(pIcon, flIconSize, { flRight, floorf(flCenter - flIconSize / 2) }, FadeColor(F::Render.Inactive), sInfoIcon);
					flRight -= H::Draw.Scale(12);
				};
				fInfo(ICON_MD_CAMPAIGN, _tTag.m_iVotePriority);
				fInfo(ICON_MD_DIRECTIONS_RUN, _tTag.m_iFollowPriority);
				fInfo(_tTag.m_iPriority < 0 ? ICON_MD_FAVORITE : ICON_MD_HEART_BROKEN, _tTag.m_iPriority);
			}
			pDrawList->AddText({ vMin.x + H::Draw.Scale(26), floorf(flCenter - GetTextLineHeight() / 2) }, FadeColor(F::Render.Active), TruncateText(_tTag.m_sName, int(flRight - vMin.x - H::Draw.Scale(30))).c_str());

			bool bDelete = false;
			if (_tTag.m_bLocked)
			{
				const char* sLockIcon = nullptr;
				switch (F::PlayerUtils.IndexToTag(_iID))
				{
				case IGNORED_TAG: sLockIcon = ICON_MD_DO_NOT_DISTURB; break;
				case CHEATER_TAG: sLockIcon = ICON_MD_FLAG; break;
				case FRIEND_TAG: sLockIcon = ICON_MD_GROUP; break;
				case PARTY_TAG: sLockIcon = ICON_MD_GROUPS; break;
				case F2P_TAG: sLockIcon = ICON_MD_MONEY_OFF; break;
				default: sLockIcon = ICON_MD_LOCK; break;
				}
				pDrawList->AddText(pIcon, pIcon->LegacySize, { vMax.x - H::Draw.Scale(28), floorf(flCenter - pIcon->LegacySize / 2) }, FadeColor(F::Render.Inactive, 0.7f), sLockIcon);
			}
			else if (bWithin)
			{
				SetCursorPos(vPos + ImVec2(flWidth - H::Draw.Scale(32), floorf((flHeight - H::Draw.Scale(26)) / 2)));
				bDelete = FIconButton(ICON_MD_DELETE, "Delete", H::Draw.Scale(26));
			}

			if (bClicked && !bDelete)
			{
				iID = _iID;
				tTag.m_sName = _tTag.m_sName;
				tTag.m_tColor = _tTag.m_tColor;
				tTag.m_iPriority = _tTag.m_iPriority;
				tTag.m_iFollowPriority = _tTag.m_iFollowPriority;
				tTag.m_iVotePriority = _tTag.m_iVotePriority;
				tTag.m_bLabel = _tTag.m_bLabel;
			}

			if (bWithin && IsMouseReleased(ImGuiMouseButton_Right))
				OpenPopup("##Menu");
			if (FBeginContextMenu("##Menu"))
			{
				if (FMenuItem("Edit", ICON_MD_EDIT))
				{
					iID = _iID;
					tTag.m_sName = _tTag.m_sName;
					tTag.m_tColor = _tTag.m_tColor;
					tTag.m_iPriority = _tTag.m_iPriority;
					tTag.m_iFollowPriority = _tTag.m_iFollowPriority;
					tTag.m_iVotePriority = _tTag.m_iVotePriority;
					tTag.m_bLabel = _tTag.m_bLabel;
				}
				if (!_tTag.m_bLocked)
				{
					FMenuSeparator();
					if (FMenuItem("Delete", ICON_MD_DELETE, F::Render.Danger, FMenuItemEnum::TintLabel))
						bDelete = true;
				}
				FEndContextMenu();
			}

			if (bDelete)
				OpenPopup("##Delete");
			bool bChanged = false;
			if (FConfirmModal("##Delete", "Delete tag", std::format("'{}' will be removed from every player.", _tTag.m_sName).c_str()))
			{
				F::PlayerUtils.m_vTags.erase(F::PlayerUtils.m_vTags.begin() + _iID);
				F::PlayerUtils.m_bSave = true;

				for (auto& vTags : F::PlayerUtils.m_mPlayerTags | std::views::values)
				{
					for (auto it = vTags.begin(); it != vTags.end();)
					{
						if (_iID == *it)
							it = vTags.erase(it);
						else
						{
							if (_iID < *it)
								(*it)--;
							it++;
						}
					}
				}

				if (iID == _iID)
				{
					iID = -1;
					tTag = {};
				}
				else if (iID > _iID)
					iID--;
				bChanged = true;
			}
			PopID();

			SetCursorPos({ vPos.x, vPos.y + flHeight + H::Draw.Scale(2) });
			return bChanged;
		};

		TableNextColumn();
		bool bChanged = false;
		if (Section("Priorities"))
		{
			for (int _iID : vPriorities)
			{
				bChanged = fDrawTag(_iID);
				if (bChanged)
					break;
			}
			DebugDummy({});
		} EndSection();
		if (!bChanged && Section("Labels"))
		{
			if (vLabels.empty())
				FEmptyState("No labels", GetCursorPosY() - H::Draw.Scale(16));
			for (int _iID : vLabels)
			{
				if (fDrawTag(_iID))
					break;
			}
			DebugDummy({});
		}
		if (!bChanged)
			EndSection();

		EndTable();
		break;
	}

	case 2:
	{
		static std::string sSearch = "";
		static std::vector<MarkedPlayer_t> vCache = {};
		static std::string sCacheSearch = "";
		static double dCacheTime = -1.0;
		static bool bCacheDirty = true;
		if (bCacheDirty || sCacheSearch != sSearch || GetTime() - dCacheTime > 0.5)
		{
			auto vFound = F::PlayerUtils.GetMarkedPlayers();
			const std::string sLowered = ToLower(sSearch);
			if (!sLowered.empty())
			{
				vFound.erase(std::remove_if(vFound.begin(), vFound.end(), [&](const MarkedPlayer_t& tEntry)
					{
						return ToLower(std::format("{} {} {} {}", tEntry.m_sDisplayName, tEntry.m_sAlias, tEntry.m_sRoleName, tEntry.m_uAccountID)).find(sLowered) == std::string::npos;
					}), vFound.end());
			}

			vCache = std::move(vFound);
			sCacheSearch = sSearch;
			dCacheTime = GetTime();
			bCacheDirty = false;
		}

		static std::vector<PriorityLabel_t> vImportTags = {};
		static std::unordered_map<uint32_t, std::vector<int>> mImportPlayerTags = {};
		static std::unordered_map<uint32_t, std::string> mImportAliases = {};
		static std::unordered_map<int, int> mImportAs = {};

		if (Section("##Marked"))
		{
			FToolbarSearch(sSearch, 5);
			PushDisabled(F::PlayerUtils.m_bLoad);
			{
				if (FToolbarButton(ICON_MD_SYNC, "Reload"))
					F::PlayerUtils.m_bLoad = true;

				if (FToolbarButton(ICON_MD_UPLOAD, "Copy to clipboard"))
				{
					std::ifstream fStream(F::Configs.m_sCorePath + "Players.json", std::ios_base::app);
					if (fStream.is_open())
					{
						std::string sString;
						{
							std::string line;
							while (std::getline(fStream, line))
								sString += line + "\n";
							if (!sString.empty())
								sString.pop_back();
						}
						fStream.close();

						SDK::SetClipboard(sString);
						SDK::Output("unibox", "Copied playerlist to clipboard", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
					}
				}

				if (FToolbarButton(ICON_MD_DOWNLOAD, "Import from clipboard"))
				{
					try
					{
						boost::property_tree::ptree tRead;
						std::stringstream ssStream;
						ssStream << SDK::GetClipboard();
						read_json(ssStream, tRead);

						mImportPlayerTags.clear();
						mImportAliases.clear();
						mImportAs.clear();
						vImportTags = {
							{ "Default", { 200, 200, 200, 255 }, 0, 0, 0, false, false, true },
							{ "Ignored", { 200, 200, 200, 255 }, -1, 0, -1, false, true, true },
							{ "Cheater", { 255, 100, 100, 255 }, 1, 0, 0, false, true, true },
							{ "Friend", { 100, 255, 100, 255 }, 0, 2, -1, true, false, true },
							{ "Party", { 100, 100, 255, 255 }, 0, 1, -1, true, false, true },
							{ "F2P", { 255, 255, 255, 255 }, 0, 0, 0, true, false, true }
						};

						if (auto tSub = tRead.get_child_optional("Config"))
						{
							for (auto& [sName, tChild] : *tSub)
							{
								PriorityLabel_t tTag = {};
								F::Configs.LoadJson(tChild, "Name", tTag.m_sName);
								F::Configs.LoadJson(tChild, "Color", tTag.m_tColor);
								F::Configs.LoadJson(tChild, "Priority", tTag.m_iPriority);
								F::Configs.LoadJson(tChild, "FollowPriority", tTag.m_iFollowPriority);
								F::Configs.LoadJson(tChild, "VotePriority", tTag.m_iVotePriority);
								F::Configs.LoadJson(tChild, "Label", tTag.m_bLabel);

								int iID = F::PlayerUtils.TagToIndex(std::stoi(sName));
								if (iID > -1 && iID < vImportTags.size())
								{
									vImportTags[iID].m_sName = tTag.m_sName;
									vImportTags[iID].m_tColor = tTag.m_tColor;
									vImportTags[iID].m_iPriority = tTag.m_iPriority;
									vImportTags[iID].m_iFollowPriority = tTag.m_iFollowPriority;
									vImportTags[iID].m_iVotePriority = tTag.m_iVotePriority;
									vImportTags[iID].m_bLabel = tTag.m_bLabel;
								}
								else
									vImportTags.push_back(tTag);
							}
						}

						if (auto tSub = tRead.get_child_optional("Tags"))
						{
							for (auto& [sName, tChild] : *tSub)
							{
								uint32_t uAccountID = std::stoul(sName);
								for (auto& tTag : tChild | std::views::values)
								{
									const std::string& sTag = tTag.data();

									int iID = F::PlayerUtils.TagToIndex(std::stoi(sTag));
									auto pTag = F::PlayerUtils.GetTag(iID);
									if (!pTag || !pTag->m_bAssignable)
										continue;

									if (!F::PlayerUtils.HasTag(uAccountID, iID, mImportPlayerTags))
										F::PlayerUtils.AddTag(uAccountID, iID, false, "", mImportPlayerTags);
								}
							}
						}

						if (auto tSub = tRead.get_child_optional("Aliases"))
						{
							for (auto& [sName, tAlias] : *tSub)
							{
								uint32_t uAccountID = std::stoul(sName);
								const std::string& sAlias = tAlias.data();

								if (!sAlias.empty())
									mImportAliases[uAccountID] = sAlias;
							}
						}

						for (int i = 0; i < vImportTags.size(); i++)
						{
							if (vImportTags[i].m_bAssignable)
								mImportAs[i] = F::PlayerUtils.IndexToTag(i) <= 0 ? i : -1;
						}
						OpenPopup("##Import");
					}
					catch (...)
					{
						SDK::Output("unibox", "Failed to import playerlist", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
					}
				}

				if (FToolbarButton(ICON_MD_BACKUP, "Save a backup"))
				{
					try
					{
						int iBackupCount = 0;
						for (auto& tEntry : std::filesystem::directory_iterator(F::Configs.m_sCorePath))
						{
							if (!tEntry.is_regular_file() || tEntry.path().extension() != F::Configs.m_sConfigExtension)
								continue;

							std::string sConfigName = tEntry.path().filename().string();
							sConfigName.erase(sConfigName.end() - F::Configs.m_sConfigExtension.size(), sConfigName.end());
							if (sConfigName.find("Backup") != std::string::npos)
								iBackupCount++;
						}
						std::filesystem::copy(
							F::Configs.m_sCorePath + "Players.json",
							F::Configs.m_sCorePath + std::format("Backup{}.json", iBackupCount + 1),
							std::filesystem::copy_options::overwrite_existing
						);
						SDK::Output("unibox", "Saved backup playerlist", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
					}
					catch (...)
					{
						SDK::Output("unibox", "Failed to backup playerlist", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
					}
				}
			}
			PopDisabled();
			if (FToolbarButton(ICON_MD_FOLDER_OPEN, "Open folder"))
				ShellExecuteA(NULL, NULL, F::Configs.m_sCorePath.c_str(), NULL, NULL, SW_SHOWNORMAL);

			SetNextWindowSize({ H::Draw.Scale(340), 0 });
			if (FBeginPopupModal("##Import"))
			{
				FModalHeader("Import playerlist", "Pick which of your tags each imported tag becomes.");

				std::vector<const char*> vEntries = { "Skip" };
				std::vector<int> vValues = { 0 };
				for (int i = 0; i < F::PlayerUtils.m_vTags.size(); i++)
				{
					if (F::PlayerUtils.m_vTags[i].m_bAssignable)
					{
						vEntries.push_back(F::PlayerUtils.m_vTags[i].m_sName.c_str());
						vValues.push_back(i + 1);
					}
				}
				for (int i = 0; i < vImportTags.size(); i++)
				{
					if (!vImportTags[i].m_bAssignable)
						continue;

					auto& iIDTo = mImportAs[i];
					PushTransparent(iIDTo == -1);
					{
						int iTo = iIDTo + 1;
						FDropdown(std::format("{}##{}", vImportTags[i].m_sName, i).c_str(), &iTo, vEntries, vValues);
						iIDTo = iTo - 1;
					}
					PopTransparent();
				}

				DebugDummy({ 0, H::Draw.Scale(10) });
				if (FButton("Cancel", FButtonEnum::Left))
					CloseCurrentPopup();
				if (FButton("Import", FButtonEnum::Right | FButtonEnum::Primary))
				{
					for (auto& [uAccountID, vTags] : mImportPlayerTags)
					{
						for (auto& iTag : vTags)
						{
							int iID = mImportAs.contains(iTag) ? mImportAs[iTag] : -1;
							if (iID != -1 && !F::PlayerUtils.HasTag(uAccountID, iID))
								F::PlayerUtils.AddTag(uAccountID, iID, false);
						}
					}
					for (auto& [uAccountID, sAlias] : mImportAliases)
					{
						if (!F::PlayerUtils.m_mPlayerAliases.contains(uAccountID))
							F::PlayerUtils.m_mPlayerAliases[uAccountID] = sAlias;
					}

					F::PlayerUtils.m_bSave = true;
					bCacheDirty = true;
					SDK::Output("unibox", "Imported playerlist", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);

					CloseCurrentPopup();
				}

				FEndPopupModal();
			}

			const float flTop = GetCursorPosY() + H::Draw.Scale(10);
			if (vCache.empty())
				FEmptyState(sSearch.empty() ? "No marked players yet" : "No matches", flTop);
			else
			{
				FCardGrid(vCache, flTop, [&](const MarkedPlayer_t& tEntry, ImVec2 vPos, ImVec2 vSize) -> bool
					{
						std::string sDetail = tEntry.m_sRoleName.empty() ? "Unmarked" : tEntry.m_sRoleName;
						if (!tEntry.m_sAlias.empty())
							sDetail += std::format(", {}", tEntry.m_sAlias);

						PushID(int(tEntry.m_uAccountID));
						auto tCard = FProfileCard(reinterpret_cast<ImTextureID>(F::SteamProfileCache.GetAvatarTexture(tEntry.m_uAccountID)), tEntry.m_sDisplayName, sDetail, ColorByteToVec(tEntry.m_tRole.m_tColor), "", "Clear role", vPos, vSize);
						if (tCard.m_bMenu)
							OpenPopup("##Menu");
						if (FBeginContextMenu("##Menu"))
						{
							SteamMenuItems(tEntry.m_uAccountID);

							FMenuSeparator();
							FMenuCaption("Alias");
							{
								static std::unordered_map<uint32_t, std::string> mAliasBuffers;
								auto& sAliasBuffer = mAliasBuffers[tEntry.m_uAccountID];
								SetCursorPosX(GetCursorPosX() + H::Draw.Scale(4));
								const bool bSubmit = FInputText("Alias", sAliasBuffer, std::max(GetContentRegionAvail().x - H::Draw.Scale(8), H::Draw.Scale(180)), ImGuiInputTextFlags_EnterReturnsTrue);
								if (!IsItemFocused() && !IsItemActive())
									sAliasBuffer = tEntry.m_sAlias;
								if (bSubmit)
								{
									SetAlias(tEntry.m_uAccountID, tEntry.m_sDisplayName.c_str(), sAliasBuffer);
									bCacheDirty = true;
								}
								Dummy({ 0, H::Draw.Scale(4) });
							}

							FMenuSeparator();
							FMenuCaption("Role");
							for (int iID = 0; iID < F::PlayerUtils.m_vTags.size(); iID++)
							{
								auto& tTag = F::PlayerUtils.m_vTags[iID];
								if (!tTag.m_bAssignable || tTag.m_bLabel)
									continue;

								const bool bCurrent = std::find(tEntry.m_vRoleTags.begin(), tEntry.m_vRoleTags.end(), iID) != tEntry.m_vRoleTags.end() && tEntry.m_tRole.m_sName == tTag.m_sName;
								if (FMenuItem(tTag.m_sName.c_str(), ICON_MD_LABEL, ColorByteToVec(tTag.m_tColor), bCurrent ? FMenuItemEnum::Checked : FMenuItemEnum::None))
								{
									F::PlayerUtils.SetPlayerRole(tEntry.m_uAccountID, iID, true, tEntry.m_sDisplayName.c_str());
									bCacheDirty = true;
								}
							}
							if (FMenuItem("Clear role", ICON_MD_DELETE, F::Render.Danger, FMenuItemEnum::TintLabel))
								tCard.m_bRemove = true;
							FEndContextMenu();
						}
						PopID();

						if (tCard.m_bRemove)
						{
							F::PlayerUtils.SetPlayerRole(tEntry.m_uAccountID, -1, true, tEntry.m_sDisplayName.c_str());
							bCacheDirty = true;
						}
						return tCard.m_bRemove;
					});
			}
		} EndSection();
		break;
	}

	case 4:
	{
		if (Section("Output", 0, GetWindowHeight() - GetStyle().WindowPadding.y * 2))
		{
			const float flPad = GetStyle().WindowPadding.x, flWidth = GetWindowWidth() - flPad * 2;
			bool bClear = false;

			const ImVec2 vTitleButton = { GetWindowWidth() - flPad - H::Draw.Scale(26), H::Draw.Scale(4) };
			const ImVec2 vContent = GetCursorPos();
			SetCursorPos(vTitleButton);
			PushDisabled(m_vOutput.empty());
			{
				if (FIconButton(ICON_MD_DELETE_SWEEP, "Clear", H::Draw.Scale(26)))
					bClear = true;
			}
			PopDisabled();
			SetCursorPos(vContent);

			if (m_vOutput.empty())
				FEmptyState("Nothing logged yet", GetCursorPosY());

			ImDrawList* pDrawList = GetWindowDrawList();
			const float flLine = GetTextLineHeight() + H::Draw.Scale(3);
			for (auto& tOutput : m_vOutput)
			{
				const ImVec2 vPos = GetCursorPos(), vMin = GetDrawPos() + vPos;
				std::vector<std::string> vTitle = {}, vLog = {};
				float flTitleWidth = 0.f;
				if (!tOutput.m_sFunction.empty())
				{
					vTitle = WrapText(tOutput.m_sFunction, int(flWidth));
					flTitleWidth = CalcTextSize(vTitle.back().c_str()).x + H::Draw.Scale(6);
					vLog = WrapText(tOutput.m_sLog, { int(flWidth - flTitleWidth), int(flWidth) });
				}
				else
					vLog = WrapText(tOutput.m_sLog, int(flWidth));

				const size_t iLines = std::max<size_t>(vTitle.size() + vLog.size() - (vTitle.empty() ? 0 : 1), 1);
				const ImVec2 vSize = { flWidth, flLine * iLines - H::Draw.Scale(3) };
				InvisibleButton(std::format("##Output{}", tOutput.m_iID).c_str(), vSize);
				const bool bHovered = IsItemHovered();
				if (bHovered || IsPopupOpen(std::format("Output{}", tOutput.m_iID).c_str()))
					pDrawList->AddRectFilled(vMin - ImVec2(H::Draw.Scale(6), H::Draw.Scale(3)), vMin + vSize + ImVec2(H::Draw.Scale(6), H::Draw.Scale(3)), FadeColor(F::Render.Background1p5), H::Draw.Scale(4));

				const ImU32 uAccent = FadeColor(ColorByteToVec(tOutput.tAccent));
				for (size_t i = 0; i < vTitle.size(); i++)
					pDrawList->AddText(vMin + ImVec2(0, flLine * i), uAccent, vTitle[i].c_str());
				const float flLogY = vTitle.empty() ? 0.f : flLine * (vTitle.size() - 1);
				for (size_t i = 0; i < vLog.size(); i++)
					pDrawList->AddText(vMin + ImVec2(i || vTitle.empty() ? 0.f : flTitleWidth, flLogY + flLine * i), vTitle.empty() ? uAccent : FadeColor(F::Render.Active), vLog[i].c_str());

				if (bHovered && IsMouseReleased(ImGuiMouseButton_Right))
					OpenPopup(std::format("Output{}", tOutput.m_iID).c_str());
				if (FBeginContextMenu(std::format("Output{}", tOutput.m_iID).c_str()))
				{
					if (FMenuItem("Copy", ICON_MD_CONTENT_COPY))
						SDK::SetClipboard(std::format("{}{}{}", tOutput.m_sFunction, tOutput.m_sFunction != "" ? " " : "", tOutput.m_sLog));
					FMenuSeparator();
					if (FMenuItem("Clear all", ICON_MD_DELETE_SWEEP, F::Render.Danger, FMenuItemEnum::TintLabel))
						bClear = true;
					FEndContextMenu();
				}

				SetCursorPos({ vPos.x, vPos.y + vSize.y + H::Draw.Scale(8) });
			}
			DebugDummy({});

			if (bClear)
				m_vOutput.clear();
		} EndSection();
		break;
	}
	}
}

namespace
{
	using namespace ImGui;

	struct ItemIcon_t
	{
		Microsoft::WRL::ComPtr<IDirect3DTexture9> pTexture;
		int iWidth = 0;
		int iHeight = 0;
		int iAttemptFrame = -1;
		int iLastUsedFrame = -1;
		bool bFailed = false;
	};

	std::unordered_map<uint16_t, ItemIcon_t> s_mItemIcons = {};

	bool ConsumeItemIconBudget()
	{
		static int iFrame = -1, iUploads = 0;
		if (ImGui::GetFrameCount() != iFrame)
			iFrame = ImGui::GetFrameCount(), iUploads = 0;
		return iUploads++ < 3;
	}

	void GetAlphaBounds(const uint8_t* pData, int iWidth, int iHeight, int& iLeft, int& iTop, int& iRight, int& iBottom)
	{
		iLeft = iWidth, iTop = iHeight, iRight = -1, iBottom = -1;
		for (int y = 0; y < iHeight; y++)
		{
			const uint8_t* pRow = pData + size_t(y) * iWidth * 4;
			for (int x = 0; x < iWidth; x++)
			{
				if (pRow[x * 4 + 3] <= 8)
					continue;

				iLeft = std::min(iLeft, x);
				iRight = std::max(iRight, x);
				iTop = std::min(iTop, y);
				iBottom = std::max(iBottom, y);
			}
		}
	}

	std::vector<uint8_t> ResizeItemPixels(const uint8_t* pData, int iWidth, int iHeight, int iSourceX, int iSourceY, int iSourceWidth, int iSourceHeight, int iTargetWidth, int iTargetHeight)
	{
		std::vector<uint8_t> vPixels(size_t(iTargetWidth) * iTargetHeight * 4);
		for (int y = 0; y < iTargetHeight; y++)
		{
			const int iSourceY0 = iSourceY + y * iSourceHeight / iTargetHeight;
			const int iSourceY1 = std::max(iSourceY0 + 1, iSourceY + (y + 1) * iSourceHeight / iTargetHeight);
			for (int x = 0; x < iTargetWidth; x++)
			{
				const int iSourceX0 = iSourceX + x * iSourceWidth / iTargetWidth;
				const int iSourceX1 = std::max(iSourceX0 + 1, iSourceX + (x + 1) * iSourceWidth / iTargetWidth);
				uint32_t uAlpha = 0, uCount = 0;
				uint32_t aColor[3] = {};
				for (int iSampleY = iSourceY0; iSampleY < iSourceY1; iSampleY++)
				{
					for (int iSampleX = iSourceX0; iSampleX < iSourceX1; iSampleX++)
					{
						const uint8_t* pSource = pData + (size_t(iSampleY) * iWidth + iSampleX) * 4;
						const uint32_t uSourceAlpha = pSource[3];
						aColor[0] += pSource[0] * uSourceAlpha;
						aColor[1] += pSource[1] * uSourceAlpha;
						aColor[2] += pSource[2] * uSourceAlpha;
						uAlpha += uSourceAlpha;
						uCount++;
					}
				}
				uint8_t* pTarget = vPixels.data() + (size_t(y) * iTargetWidth + x) * 4;
				if (uAlpha)
				{
					for (int i = 0; i < 3; i++)
						pTarget[i] = uint8_t(aColor[i] / uAlpha);
				}
				pTarget[3] = uint8_t(uAlpha / uCount);
				if (pTarget[3] < 2)
					*reinterpret_cast<uint32_t*>(pTarget) = 0;
			}
		}
		return vPixels;
	}

	Microsoft::WRL::ComPtr<IDirect3DTexture9> CreateItemTexture(const uint8_t* pData, uint32_t uWidth, uint32_t uHeight, bool bRGBA = false)
	{
		auto* pDevice = F::Render.GetDevice();
		if (!pDevice || !pData || !uWidth || !uHeight)
			return {};

		auto TryCreate = [&](D3DPOOL ePool, DWORD uUsage) -> Microsoft::WRL::ComPtr<IDirect3DTexture9>
		{
			Microsoft::WRL::ComPtr<IDirect3DTexture9> pTexture;
			if (FAILED(pDevice->CreateTexture(uWidth, uHeight, 1, uUsage, D3DFMT_A8R8G8B8, ePool, &pTexture, nullptr)))
				return {};

			D3DLOCKED_RECT tRect = {};
			const DWORD uFlags = uUsage & D3DUSAGE_DYNAMIC ? D3DLOCK_DISCARD : 0;
			if (FAILED(pTexture->LockRect(0, &tRect, nullptr, uFlags)))
				return {};

			for (uint32_t y = 0; y < uHeight; y++)
			{
				const uint8_t* pSrc = pData + y * uWidth * 4;
				uint8_t* pDst = static_cast<uint8_t*>(tRect.pBits) + y * uint32_t(tRect.Pitch);
				if (!bRGBA)
				{
					std::memcpy(pDst, pSrc, uWidth * 4);
					continue;
				}

				for (uint32_t x = 0; x < uWidth; x++)
				{
					pDst[x * 4 + 0] = pSrc[x * 4 + 2];
					pDst[x * 4 + 1] = pSrc[x * 4 + 1];
					pDst[x * 4 + 2] = pSrc[x * 4 + 0];
					pDst[x * 4 + 3] = pSrc[x * 4 + 3];
				}
			}
			pTexture->UnlockRect(0);
			return pTexture;
		};

		if (auto pManaged = TryCreate(D3DPOOL_MANAGED, 0))
			return pManaged;
		return TryCreate(D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC);
	}

	ImTextureID GetItemIcon(const ItemInfo_t& tItem, ImVec2* pSize = nullptr)
	{
		const int iFrame = ImGui::GetFrameCount();
		{
			static int iLastPruneFrame = -1;
			if (iLastPruneFrame != iFrame)
			{
				iLastPruneFrame = iFrame;
				if (s_mItemIcons.size() > 256)
				{
					for (auto it = s_mItemIcons.begin(); it != s_mItemIcons.end();)
					{
						if (iFrame - it->second.iLastUsedFrame > 120)
							it = s_mItemIcons.erase(it);
						else
							++it;
					}
				}
			}
		}

		auto& tIcon = s_mItemIcons[tItem.iDef];
		tIcon.iLastUsedFrame = iFrame;
		if (tIcon.pTexture)
		{
			if (pSize)
				*pSize = { float(tIcon.iWidth), float(tIcon.iHeight) };
			return reinterpret_cast<ImTextureID>(tIcon.pTexture.Get());
		}
		if (tIcon.bFailed && iFrame - tIcon.iAttemptFrame < 120)
			return static_cast<ImTextureID>(0);
		if (tItem.sImage.empty() || !I::MaterialSystem)
		{
			tIcon.bFailed = true;
			tIcon.iAttemptFrame = iFrame;
			return static_cast<ImTextureID>(0);
		}
		if (!ConsumeItemIconBudget())
			return static_cast<ImTextureID>(0);

		tIcon.iAttemptFrame = iFrame;
		tIcon.bFailed = true;

		auto CreateScaled = [&tIcon](const uint8_t* pData, int iWidth, int iHeight, bool bRGBA) -> Microsoft::WRL::ComPtr<IDirect3DTexture9>
		{
			constexpr int iIconWidth = 128, iIconHeight = 96;
			if (!pData || iWidth <= 0 || iHeight <= 0)
				return {};

			int iLeft = 0, iTop = 0, iRight = iWidth - 1, iBottom = iHeight - 1;
			GetAlphaBounds(pData, iWidth, iHeight, iLeft, iTop, iRight, iBottom);
			if (iRight < iLeft || iBottom < iTop)
				return {};

			const int iSourceWidth = iRight - iLeft + 1, iSourceHeight = iBottom - iTop + 1;
			float flAspect = float(iSourceWidth) / float(iSourceHeight);
			int iTargetWidth = iSourceWidth, iTargetHeight = iSourceHeight;
			if (flAspect > float(iIconWidth) / float(iIconHeight))
				iTargetHeight = std::max(1, int(iIconWidth / flAspect));
			else
				iTargetWidth = std::max(1, int(iIconHeight * flAspect));

			const auto vScaled = ResizeItemPixels(pData, iWidth, iHeight, iLeft, iTop, iSourceWidth, iSourceHeight, iTargetWidth, iTargetHeight);
			auto pTexture = CreateItemTexture(vScaled.data(), uint32_t(iTargetWidth), uint32_t(iTargetHeight), bRGBA);
			if (pTexture)
			{
				tIcon.iWidth = iTargetWidth;
				tIcon.iHeight = iTargetHeight;
			}
			return pTexture;
		};

		std::string sPath = tItem.sImage;
		if (sPath.starts_with("materials/"))
			sPath.erase(0, 10);
		if (!sPath.ends_with(".vmt"))
			sPath += ".vmt";

		IMaterial* pMaterial = I::MaterialSystem->FindMaterial(sPath.c_str(), TEXTURE_GROUP_VGUI, false);
		if (!pMaterial)
			pMaterial = I::MaterialSystem->FindMaterial(sPath.c_str(), TEXTURE_GROUP_OTHER, false);
		if (!pMaterial)
			return static_cast<ImTextureID>(0);

		{
			int iWidth = 0, iHeight = 0;
			ImageFormat eFormat = IMAGE_FORMAT_UNKNOWN;
			bool bTranslucent = false;
			if (pMaterial->GetPreviewImageProperties(&iWidth, &iHeight, &eFormat, &bTranslucent) == MATERIAL_PREVIEW_IMAGE_OK
				&& iWidth > 0 && iHeight > 0 && iWidth <= 512 && iHeight <= 512)
			{
				std::vector<uint8_t> vPixels(size_t(iWidth) * size_t(iHeight) * 4);
				if (pMaterial->GetPreviewImage(vPixels.data(), iWidth, iHeight, IMAGE_FORMAT_BGRA8888) == MATERIAL_PREVIEW_IMAGE_OK)
				{
					if (auto pCreated = CreateScaled(vPixels.data(), iWidth, iHeight, false))
					{
						tIcon.pTexture = pCreated;
						tIcon.bFailed = false;
						return reinterpret_cast<ImTextureID>(tIcon.pTexture.Get());
					}
				}
			}
		}
		return static_cast<ImTextureID>(0);
	}

	std::string Lowercase(const std::string& sText)
	{
		std::string sReturn = sText;
		std::transform(sReturn.begin(), sReturn.end(), sReturn.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		return sReturn;
	}

	struct ItemGrid_t
	{
		uint16_t uClicked = 0;
		uint16_t uDoubleClicked = 0;
	};

	ItemGrid_t DrawItemGrid(const char* sID, const std::vector<const ItemInfo_t*>& vItems, const std::unordered_set<uint16_t>& tOwned, uint16_t uSelected, float flHeight, bool bCounts = false)
	{
		ItemGrid_t tResult = {};
		const float flSpacing = H::Draw.Scale(6), flMinCellWidth = H::Draw.Scale(92);
		const int iTotal = int(vItems.size());

		PushStyleVar(ImGuiStyleVar_ItemSpacing, { flSpacing, flSpacing });
		if (BeginChild(sID, { GetWindowWidth() - GetStyle().WindowPadding.x * 2, flHeight }, ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar))
		{
			if (iTotal == 0)
			{
				const char* sEmpty = "No items";
				SetCursorPos({ (GetWindowWidth() - CalcTextSize(sEmpty).x) * 0.5f, H::Draw.Scale(24) });
				TextColored(F::Render.Inactive, sEmpty);
			}

			const float flAvailWidth = GetContentRegionAvail().x - H::Draw.Scale(4);
			const int iColumns = std::max(1, int((flAvailWidth + flSpacing) / (flMinCellWidth + flSpacing)));
			const float flCellW = floorf((flAvailWidth - flSpacing * (iColumns - 1)) / iColumns);
			const float flIconH = std::clamp(flCellW * 0.7f, H::Draw.Scale(56), H::Draw.Scale(84));
			const float flCellH = H::Draw.Scale(6) + flIconH + H::Draw.Scale(5) + H::Draw.Scale(16) + H::Draw.Scale(4);
			const float flRowH = flCellH + flSpacing;
			const int iTotalRows = (iTotal + iColumns - 1) / iColumns;
			const int iFirstRow = std::clamp(int(GetScrollY() / flRowH) - 1, 0, std::max(0, iTotalRows - 1));
			const int iLastRow = std::min(iTotalRows, iFirstRow + int(GetWindowHeight() / flRowH) + 2);
			if (iFirstRow > 0)
				Dummy({ 0, iFirstRow * flRowH - flSpacing });

			ImDrawList* pDrawList = GetWindowDrawList();
			ImFont* pSmall = F::Render.FontSmall;
			for (int iRow = iFirstRow; iRow < iLastRow; iRow++)
			{
				for (int iColumn = 0; iColumn < iColumns; iColumn++)
				{
					const int iIndex = iRow * iColumns + iColumn;
					if (iIndex >= iTotal)
						break;

					const ItemInfo_t* pItem = vItems[iIndex];
					if (iColumn)
						SameLine();
					PushID(pItem->iDef);
					const ImVec2 vPos = GetCursorScreenPos();
					const bool bClicked = InvisibleButton("##Item", { flCellW, flCellH });
					const bool bHovered = IsItemHovered();
					const bool bSelected = pItem->iDef == uSelected;
					const float flHover = Anim(GetItemID(), 0, bHovered ? 1.f : 0.f);
					const ImVec2 vEnd = vPos + ImVec2(flCellW, flCellH);
					const float flRounding = H::Draw.Scale(5);

					pDrawList->AddRectFilled(vPos, vEnd, FadeColor(MixColor(F::Render.Background1p5, F::Render.Background1p5L, flHover)), flRounding);
					if (bSelected)
						pDrawList->AddRectFilled(vPos, vEnd, FadeColor(F::Render.AccentDim), flRounding);
					if (tOwned.contains(pItem->iDef))
						pDrawList->AddCircleFilled(vPos + ImVec2(H::Draw.Scale(9), H::Draw.Scale(9)), H::Draw.Scale(3), FadeColor(F::Render.Accent), 12);

					ImVec2 vSourceSize = { 1.f, 1.f };
					const ImTextureID pIcon = GetItemIcon(*pItem, &vSourceSize);
					const float flIconWidth = std::max(1.f, flCellW - H::Draw.Scale(14)), flIconHeight = flIconH;
					const ImVec2 vIconMin = vPos + ImVec2((flCellW - flIconWidth) / 2, H::Draw.Scale(6));
					if (pIcon)
					{
						const float flScale = std::min(flIconWidth / vSourceSize.x, flIconHeight / vSourceSize.y);
						const ImVec2 vIconSize = vSourceSize * flScale;
						const ImVec2 vIcon = vIconMin + ImVec2((flIconWidth - vIconSize.x) / 2, (flIconHeight - vIconSize.y) / 2);
						pDrawList->AddImage(pIcon, vIcon, vIcon + vIconSize, { 0, 0 }, { 1, 1 }, FadeColor(ImVec4(1, 1, 1, 1)));
					}
					else
						pDrawList->AddRectFilled(vIconMin, vIconMin + ImVec2(flIconWidth, flIconHeight), FadeColor(F::Render.Background1p5), H::Draw.Scale(4));

					if (bCounts)
					{
						if (const int iCount = F::SkinChanger.GetInventoryCount(pItem->iDef); iCount > 1)
						{
							const std::string sCount = std::format("x{}", iCount);
							const float flCountWidth = pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, sCount.c_str()).x;
							pDrawList->AddText(pSmall, pSmall->LegacySize, { vEnd.x - flCountWidth - H::Draw.Scale(7), vPos.y + H::Draw.Scale(5) }, FadeColor(F::Render.Active), sCount.c_str());
						}
					}

					const std::string sName = TruncateText(pItem->sName, int(flCellW - H::Draw.Scale(10)), pSmall);
					const float flNameWidth = pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, sName.c_str()).x;
					const ImVec2 vNamePos = vPos + ImVec2(floorf((flCellW - flNameWidth) / 2), H::Draw.Scale(6) + flIconH + H::Draw.Scale(5));
					pDrawList->AddText(pSmall, pSmall->LegacySize, vNamePos, FadeColor(MixColor(F::Render.Inactive, F::Render.Active, bSelected ? 1.f : 0.45f + 0.55f * flHover)), sName.c_str());

					if (bClicked)
						tResult.uClicked = pItem->iDef;
					if (bHovered && IsMouseDoubleClicked(ImGuiMouseButton_Left))
						tResult.uDoubleClicked = pItem->iDef;
					if (bHovered)
						FTooltip(std::format("{}\nItem #{}", pItem->sName, pItem->iDef).c_str());

					PopID();
				}
			}

			if (iLastRow < iTotalRows)
				Dummy({ 0, (iTotalRows - iLastRow) * flRowH - flSpacing });
		} EndChild();
		PopStyleVar();

		return tResult;
	}

	const ItemInfo_t* FindItemInfo(uint16_t iDef)
	{
		static std::unordered_map<uint16_t, const ItemInfo_t*> s_mLookup = {};
		static size_t s_uSize = 0;
		static const ItemInfo_t* s_pData = nullptr;
		const auto& vItems = CSkinChanger::GetItems();
		if (s_uSize != vItems.size() || s_pData != vItems.data())
		{
			s_mLookup.clear();
			for (const auto& tItem : vItems)
				s_mLookup[tItem.iDef] = &tItem;
			s_uSize = vItems.size();
			s_pData = vItems.data();
		}

		auto it = s_mLookup.find(iDef);
		return it != s_mLookup.end() ? it->second : nullptr;
	}

	const std::vector<std::pair<int, std::string>>& GetUnusualEffects()
	{
		static std::vector<std::pair<int, std::string>> s_vEffects = {};
		static bool s_bBuilt = false;
		if (!s_bBuilt && I::Localize)
		{
			std::unordered_set<std::string> tSeen = {};
			for (int i = 1; i <= 3242; i++)
			{
				const char* sName = I::Localize->FindAsUTF8(std::format("#Attrib_Particle{}", i).c_str());
				if (!sName || !sName[0])
					continue;

				std::string sEffect = sName;
				if (sEffect == "Invalid Particle" || sEffect.starts_with("Particle ") || sEffect.find("Attrib_Particle") != std::string::npos)
					continue;
				if (!tSeen.insert(sEffect).second)
					continue;
				s_vEffects.emplace_back(i, std::move(sEffect));
			}
			std::sort(s_vEffects.begin(), s_vEffects.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
			s_bBuilt = !s_vEffects.empty();
		}
		return s_vEffects;
	}

	const char* UnusualEffectName(int iEffect)
	{
		for (const auto& [iId, sName] : GetUnusualEffects())
		{
			if (iId == iEffect)
				return sName.c_str();
		}
		return "None";
	}

	int UnusualEffectId(const std::string& sName)
	{
		for (const auto& [iId, sEffect] : GetUnusualEffects())
		{
			if (sEffect == sName)
				return iId;
		}
		return -1;
	}

}

void CMenu::MenuSkins(int iTab)
{
	using namespace ImGui;

	static int iSelectedWeapon = -1;
	static std::string sItemSearch;
	static std::string sBackpackSearch;
	static int iBackpackFilter = 0;
	static uint16_t uBackpackSelected = 0;

	static std::vector<const char*> vWeaponNames = { "Held weapon" };
	static std::vector<int> vWeaponIds = { -1 };
	static bool bWeaponListBuilt = false;
	if (!bWeaponListBuilt)
	{
		std::unordered_set<int> tKeys = {};
		for (const auto& tWeapon : SkinData::kWeaponNames)
		{
			const int iKey = CSkinChanger::Key(tWeapon.iDef);
			if (!tKeys.insert(iKey).second)
				continue;
			vWeaponNames.push_back(tWeapon.sName);
			vWeaponIds.push_back(iKey);
		}
		bWeaponListBuilt = true;
	}

	switch (iTab)
	{
	case 0:
	{
		if (!PageTable("SkinsWeapons", 2))
			break;

		const HeldWeapon_t tHeld = F::SkinChanger.GetHeldWeapon();
		const int iKey = iSelectedWeapon >= 0 ? iSelectedWeapon : (tHeld.bValid ? tHeld.iKey : -1);
		const bool bValid = iKey >= 0;
		const bool bAllowUnsupported = Vars::Visuals::SkinChanger::AllowUnsupported.Value;

		TableNextColumn();
		if (Section("Weapon"))
		{
			FToggle(Vars::Visuals::SkinChanger::Enabled);
			FToggle(Vars::Visuals::SkinChanger::AllowUnsupported);
			FDropdown("Weapon", &iSelectedWeapon, vWeaponNames, vWeaponIds);
			if (!bValid)
				FNote("Pick a weapon or hold one to edit it");
			else if (iSelectedWeapon < 0)
			{
				if (const char* sName = CSkinChanger::WeaponName(iKey))
					FNote(std::format("Holding {}", sName).c_str());
				else
					FNote(std::format("Holding item #{}", iKey).c_str());
			}

			PushDisabled(!bValid);
			Skin_t tSkin = bValid ? F::SkinChanger.Get(iKey) : Skin_t{};

			static int iCachedKitKey = -2;
			static int iCachedKitCurrent = -2;
			static bool bCachedAllowUnsupported = false;
			static std::vector<std::string> vKitStrings = {};
			static std::vector<int> vKitIds = {};
			if (iCachedKitKey != iKey || iCachedKitCurrent != tSkin.iPaintKit || bCachedAllowUnsupported != bAllowUnsupported)
			{
				CSkinChanger::GetKits(iKey, bAllowUnsupported, tSkin.iPaintKit, vKitStrings, vKitIds);
				iCachedKitKey = iKey;
				iCachedKitCurrent = tSkin.iPaintKit;
				bCachedAllowUnsupported = bAllowUnsupported;
			}
			std::vector<const char*> vKitNames = {};
			vKitNames.reserve(vKitStrings.size());
			for (auto& sKit : vKitStrings)
				vKitNames.push_back(sKit.c_str());

			Subheader("Warpaint");
			FDropdown("Paint kit", &tSkin.iPaintKit, vKitNames, vKitIds);
			if (bValid && !CSkinChanger::CanPaint(iKey, false))
				FNote(bAllowUnsupported ? "Not supported on this weapon" : "No warpaints for this weapon");

			static const std::vector<const char*> vWear = { "Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle Scarred" };
			static const std::vector<const char*> vQualityNames = { "Default", "Normal", "Unique", "Vintage", "Genuine", "Strange", "Unusual", "Haunted", "Collector's", "Decorated", "Community", "Self-Made", "Valve" };
			static const std::vector<int> vQualityValues = { -1, 0, 6, 3, 1, 11, 5, 13, 14, 15, 7, 9, 8 };
			if (tSkin.iStatTrak >= 0)
				tSkin.iQuality = 11;
			PushTransparent(tSkin.iPaintKit < 0);
			{
				FDropdown("Wear", &tSkin.iWear, vWear, {});
				FSlider("Seed", &tSkin.iSeed, 0, 255, 1, "%i", FSliderEnum::Clamp);
			}
			PopTransparent();

			Subheader("Item");
			PushDisabled(tSkin.iStatTrak >= 0);
			{
				FDropdown("Quality", &tSkin.iQuality, vQualityNames, vQualityValues);
			}
			PopDisabled();
			FSlider("StatTrak kills", &tSkin.iStatTrak, -1, 100000, 1, tSkin.iStatTrak < 0 ? "Off" : "%i", FSliderEnum::Clamp);
			if (tSkin.iStatTrak >= 0)
				FNote("StatTrak forces Strange quality");
			PushDisabled(!CSkinChanger::CanBotkiller(iKey));
			{
				static const std::vector<const char*> vBotkiller = { "Off", "Silver Mk.I", "Gold Mk.I", "Rust Mk.I", "Blood Mk.I", "Carbonado Mk.I", "Diamond Mk.I", "Silver Mk.II", "Gold Mk.II" };
				FDropdown("Botkiller", &tSkin.iBotkiller, vBotkiller, {});
			}
			PopDisabled();

			if (bValid && !(tSkin == F::SkinChanger.Get(iKey)))
				F::SkinChanger.Set(iKey, tSkin);
			PopDisabled();
		} EndSection();

		TableNextColumn();
		if (Section("Effects"))
		{
			Skin_t tSkin = bValid ? F::SkinChanger.Get(iKey) : Skin_t{};

			PushDisabled(!bValid);
			{
				PushDisabled(!CSkinChanger::CanAustralium(iKey));
				{
					FToggle(CSkinChanger::IsGoldenPan(iKey) ? "Golden" : "Australium", &tSkin.bAustralium);
				}
				PopDisabled();
				PushDisabled(!CSkinChanger::CanFestive(iKey));
				{
					FToggle("Festivizer", &tSkin.bFestive);
				}
				PopDisabled();

				bool bPaintColor = tSkin.iPaintColor >= 0;
				const bool bOldPaintColor = bPaintColor;
				Color_t tPaintColor = bPaintColor ? Color_t{ byte(tSkin.iPaintColor & 255), byte((tSkin.iPaintColor >> 8) & 255), byte((tSkin.iPaintColor >> 16) & 255) } : Color_t{ 255, 255, 255 };
				FToggle("Paint color", &bPaintColor);
				if (bPaintColor)
				{
					if (!bOldPaintColor)
						tSkin.iPaintColor = int(tPaintColor.r) | (int(tPaintColor.g) << 8) | (int(tPaintColor.b) << 16);
					if (FColorPicker("##PaintColor", &tPaintColor))
						tSkin.iPaintColor = int(tPaintColor.r) | (int(tPaintColor.g) << 8) | (int(tPaintColor.b) << 16);
				}
				else
					tSkin.iPaintColor = -1;

				Subheader("Killstreak");
				FSlider("Tier", &tSkin.iKillstreak, 0, 3, 1, tSkin.iKillstreak ? "%i" : "Off", FSliderEnum::Clamp);
				static const std::vector<const char*> vSheen = { "Off", "Team shine", "Deadly daffodil", "Manndarin", "Mean green", "Agonizing emerald", "Villainous violet", "Hot rod" };
				PushTransparent(tSkin.iKillstreak < 2);
				{
					FDropdown("Sheen", &tSkin.iSheen, vSheen, {});
				}
				PopTransparent();
				static const std::vector<const char*> vKillFx = { "Off", "Cerebral Discharge", "Fire Horns", "Flames", "Hypno-Beam", "Incinerator", "Singularity", "Tornado" };
				static const std::vector<int> vKillFxIds = { -1, 2002, 2003, 2004, 2005, 2006, 2007, 2008 };
				PushTransparent(tSkin.iKillstreak < 3);
				{
					FDropdown("Killstreaker", &tSkin.iKillEffect, vKillFx, vKillFxIds);
				}
				PopTransparent();

				Subheader("Unusual");
				static const std::vector<const char*> vUnusual = { "Off", "Hot", "Isotope", "Cool", "Energy orb" };
				FDropdown("Effect", &tSkin.iUnusual, vUnusual, {});
			}
			PopDisabled();

			if (bValid && !(tSkin == F::SkinChanger.Get(iKey)))
				F::SkinChanger.Set(iKey, tSkin);

			DebugDummy({ 0, H::Draw.Scale(8) });
			PushDisabled(!bValid);
			{
				if (FButton("Reset weapon", FButtonEnum::Left) && bValid)
					F::SkinChanger.Set(iKey, Skin_t{});
			}
			PopDisabled();
			if (FButton("Reset all", FButtonEnum::Right | FButtonEnum::Danger))
				OpenPopup("##ResetSkins");
			if (FConfirmModal("##ResetSkins", "Reset all skins", "Every weapon goes back to its default look.", "Reset"))
				F::SkinChanger.Clear();
		} EndSection();
		if (Section("Misc##Skins"))
		{
			FToggle(Vars::Misc::Exploits::BackpackExpander);
			FToggle(Vars::Misc::Exploits::EquipRegionUnlock);
			FSDropdown(Vars::Visuals::Effects::BulletTracer);
			DebugDummy({ 0, H::Draw.Scale(4) });
			if (FButton("Full update"))
				F::SkinChanger.RequestFullUpdate();
		} EndSection();

		EndTable();
		break;
	}

	case 1:
	{
		const float flSectionHeight = std::max(H::Draw.Scale(360), GetContentRegionAvail().y - GetStyle().WindowPadding.y);
		if (!BeginTable("SkinsBackpack", 2))
			break;

		TableSetupColumn("##Owned", ImGuiTableColumnFlags_WidthStretch, 1.f);
		TableSetupColumn("##Catalogue", ImGuiTableColumnFlags_WidthStretch, 1.35f);
		TableNextColumn();
		if (Section("Backpack", 0, flSectionHeight, true))
		{
			FToggle(Vars::Visuals::SkinChanger::Enabled);
			DebugDummy({ 0, H::Draw.Scale(4) });
			FInputText("Search", sBackpackSearch, GetWindowWidth() - GetStyle().WindowPadding.x * 2);

			const std::vector<const ItemInfo_t*> vBackpack = F::SkinChanger.GetInventoryItems();
			static std::vector<const ItemInfo_t*> s_vBackpackFiltered = {};
			static std::string s_sBackpackSearch;
			static size_t s_uBackpackSize = 0;
			static const ItemInfo_t* const* s_pBackpackData = nullptr;
			if (s_sBackpackSearch != sBackpackSearch || s_uBackpackSize != vBackpack.size() || s_pBackpackData != vBackpack.data())
			{
				s_vBackpackFiltered.clear();
				const std::string sNeedle = Lowercase(sBackpackSearch);
				for (const auto* pItem : vBackpack)
				{
					if (sNeedle.empty() || Lowercase(pItem->sName).find(sNeedle) != std::string::npos)
						s_vBackpackFiltered.push_back(pItem);
				}
				s_sBackpackSearch = sBackpackSearch;
				s_uBackpackSize = vBackpack.size();
				s_pBackpackData = vBackpack.data();
			}

			DebugDummy({ 0, H::Draw.Scale(4) });
			FNote(vBackpack.empty() ? "Empty, add items from the catalogue" : std::format("{} items, double click to remove", vBackpack.size()).c_str());

			const ItemInfo_t* pSelected = uBackpackSelected ? FindItemInfo(uBackpackSelected) : nullptr;
			const bool bSelectedOwned = pSelected && vBackpack.cend() != std::find(vBackpack.cbegin(), vBackpack.cend(), pSelected);
			const bool bSelectedAdded = bSelectedOwned && F::SkinChanger.InBackpack(uBackpackSelected);
			const float flGridHeight = std::max(H::Draw.Scale(120), GetWindowHeight() - GetCursorPosY() - GetStyle().WindowPadding.y - H::Draw.Scale(44));
			const ItemGrid_t tGrid = DrawItemGrid("##OwnedGrid", s_vBackpackFiltered, {}, bSelectedOwned ? uBackpackSelected : 0, flGridHeight, true);
			if (tGrid.uClicked)
				uBackpackSelected = tGrid.uClicked;
			if (tGrid.uDoubleClicked && F::SkinChanger.InBackpack(tGrid.uDoubleClicked))
				F::SkinChanger.RemoveFromBackpack(tGrid.uDoubleClicked);

			DebugDummy({ 0, H::Draw.Scale(6) });
			PushDisabled(!bSelectedAdded);
			{
				if (FButton(bSelectedAdded ? std::format("Remove {}", TruncateText(pSelected->sName, int(GetWindowWidth() / 2))).c_str() : "Remove", FButtonEnum::Danger))
					F::SkinChanger.RemoveFromBackpack(uBackpackSelected);
			}
			PopDisabled();
		} EndSection();

		TableNextColumn();
		if (Section("Catalogue", 0, flSectionHeight, true))
		{
			static const std::vector<const char*> vFilters = { "All", "Weapons", "Hats & misc", "Tools & cases" };
			static const std::vector<int> vFilterIds = { 0, 1, 2, 3 };
			FDropdown("Show", &iBackpackFilter, vFilters, vFilterIds);
			DebugDummy({ 0, H::Draw.Scale(4) });
			FInputText("Search", sItemSearch, GetWindowWidth() - GetStyle().WindowPadding.x * 2);

			const auto& vItems = CSkinChanger::GetItems();
			static std::vector<const ItemInfo_t*> s_vCatalogueFiltered = {};
			static std::string s_sCatalogueSearch;
			static int s_iCatalogueFilter = -1;
			static size_t s_uCatalogueSize = 0;
			static const ItemInfo_t* s_pCatalogueData = nullptr;
			if (s_sCatalogueSearch != sItemSearch || s_iCatalogueFilter != iBackpackFilter || s_uCatalogueSize != vItems.size() || s_pCatalogueData != vItems.data())
			{
				s_vCatalogueFiltered.clear();
				const std::string sNeedle = Lowercase(sItemSearch);
				for (const auto& tItem : vItems)
				{
					bool bMatch = true;
					switch (iBackpackFilter)
					{
					case 1: bMatch = tItem.sClass.starts_with("tf_weapon"); break;
					case 2: bMatch = tItem.sClass == "tf_wearable" || tItem.sClass.starts_with("tf_wearable_"); break;
					case 3: bMatch = !tItem.sClass.starts_with("tf_weapon") && !tItem.sClass.starts_with("tf_wearable"); break;
					}
					if (bMatch && !sNeedle.empty())
						bMatch = Lowercase(tItem.sName).find(sNeedle) != std::string::npos;
					if (bMatch)
						s_vCatalogueFiltered.push_back(&tItem);
				}
				s_sCatalogueSearch = sItemSearch;
				s_iCatalogueFilter = iBackpackFilter;
				s_uCatalogueSize = vItems.size();
				s_pCatalogueData = vItems.data();
			}

			std::unordered_set<uint16_t> tBackpack = {};
			for (const auto& tItem : F::SkinChanger.GetBackpack())
				tBackpack.insert(tItem.iDef);
			DebugDummy({ 0, H::Draw.Scale(4) });
			FNote(std::format("{} items, double click to add", s_vCatalogueFiltered.size()).c_str());

			const ItemInfo_t* pSelected = uBackpackSelected ? FindItemInfo(uBackpackSelected) : nullptr;
			const bool bWearable = pSelected && (pSelected->sClass == "tf_wearable" || pSelected->sClass.starts_with("tf_wearable_"));
			const float flFooterHeight = H::Draw.Scale(bWearable ? 82 : 44);
			const float flGridHeight = std::max(H::Draw.Scale(120), GetWindowHeight() - GetCursorPosY() - GetStyle().WindowPadding.y - flFooterHeight);
			const ItemGrid_t tGrid = DrawItemGrid("##CatalogueGrid", s_vCatalogueFiltered, tBackpack, uBackpackSelected, flGridHeight);
			if (tGrid.uClicked)
				uBackpackSelected = tGrid.uClicked;

			bool bAdd = tGrid.uDoubleClicked;

			DebugDummy({ 0, H::Draw.Scale(6) });
			static std::string sUnusualName = "None";
			if (pSelected && bWearable)
			{
				static std::vector<const char*> vUnusualNames = { "None" };
				static size_t s_uUnusualCount = 0;
				const auto& vEffects = GetUnusualEffects();
				if (s_uUnusualCount != vEffects.size())
				{
					vUnusualNames.clear();
					vUnusualNames.push_back("None");
					for (const auto& [iEffect, sEffect] : vEffects)
						vUnusualNames.push_back(sEffect.c_str());
					s_uUnusualCount = vEffects.size();
				}
				FSDropdown("Unusual effect", &sUnusualName, vUnusualNames);
			}

			PushDisabled(!pSelected);
			{
				if (FButton(pSelected ? std::format("Add {}", TruncateText(pSelected->sName, int(GetWindowWidth() / 3))).c_str() : "Add", FButtonEnum::Left | FButtonEnum::Primary))
					bAdd = true;
			}
			PopDisabled();
			PushDisabled(tBackpack.empty());
			{
				if (FButton("Clear backpack", FButtonEnum::Right))
					OpenPopup("##ClearBackpack");
			}
			PopDisabled();
			if (FConfirmModal("##ClearBackpack", "Clear backpack", "Every item you added is removed from the backpack.", "Clear"))
				F::SkinChanger.ClearBackpack();

			if (bAdd)
			{
				const uint16_t uAddDef = tGrid.uDoubleClicked ? tGrid.uDoubleClicked : uBackpackSelected;
				const ItemInfo_t* pAdd = FindItemInfo(uAddDef);
				const bool bAddWearable = pAdd && (pAdd->sClass == "tf_wearable" || pAdd->sClass.starts_with("tf_wearable_"));
				F::SkinChanger.AddToBackpack(uAddDef, bAddWearable ? std::max(UnusualEffectId(sUnusualName), 0) : 0);
			}
		} EndSection();

		EndTable();
		break;
	}
	}
}

void CMenu::MenuSettings(int iTab)
{
	using namespace ImGui;

	switch (iTab)
	{
	case 0:
	{
		if (!PageTable("Configs", 2))
			break;

		auto fDrawConfigs = [&](std::string& sStaticName, int iMode = 0)
		{
			const bool bVisual = iMode == 1, bSkins = iMode == 2;
			auto& sPath = bSkins ? F::Configs.m_sSkinsPath : (!bVisual ? F::Configs.m_sConfigPath : F::Configs.m_sVisualsPath);
			auto& sConfig = bSkins ? F::Configs.m_sCurrentSkins : (!bVisual ? F::Configs.m_sCurrentConfig : F::Configs.m_sCurrentVisuals);
			const bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;

			auto fLoad = [&](const std::string& sName)
			{
				if (bSkins)
					F::Configs.LoadSkins(sName);
				else if (!bVisual)
					F::Configs.LoadConfig(sName);
				else
					F::Configs.LoadVisual(sName);
			};
			auto fSave = [&](const std::string& sName)
			{
				if (bSkins)
					F::Configs.SaveSkins(sName);
				else if (!bVisual)
					F::Configs.SaveConfig(sName);
				else
					F::Configs.SaveVisual(sName);
			};

			bool bFolder = false;
			if (FCreateRow(sStaticName, "New config", &bFolder))
			{
				std::error_code tError;
				const auto tConfigPath = std::filesystem::path(sPath) / (sStaticName + F::Configs.m_sConfigExtension);
				if (!std::filesystem::exists(tConfigPath, tError) && !tError)
					fSave(sStaticName);
				sStaticName.clear();
			}
			if (bFolder)
				ShellExecuteA(NULL, NULL, sPath.c_str(), NULL, NULL, SW_SHOWNORMAL);

			std::vector<std::string> vConfigs = {};
			bool bDefaultFound = false;
			std::error_code tError;
			for (std::filesystem::directory_iterator tIterator(sPath, tError), tEnd; !tError && tIterator != tEnd; tIterator.increment(tError))
			{
				auto& tEntry = *tIterator;
				std::error_code tEntryError;
				if (!tEntry.is_regular_file(tEntryError) || tEntryError || tEntry.path().extension() != F::Configs.m_sConfigExtension)
					continue;

				std::string sName = tEntry.path().filename().string();
				sName.erase(sName.end() - F::Configs.m_sConfigExtension.size(), sName.end());
				if (FNV1A::Hash32(sName.c_str()) == FNV1A::Hash32Const("default"))
					bDefaultFound = true;

				vConfigs.push_back(sName);
			}
			if (!bVisual && !bSkins)
			{
				if (!bDefaultFound)
					F::Configs.SaveConfig("default");
				std::sort(vConfigs.begin(), vConfigs.end(), [&](const auto& a, const auto& b) -> bool
				{
					if (FNV1A::Hash32(a.c_str()) == FNV1A::Hash32Const("default"))
						return true;
					if (FNV1A::Hash32(b.c_str()) == FNV1A::Hash32Const("default"))
						return false;

					return a < b;
				});
			}
			if (vConfigs.empty())
				FNote("Nothing saved yet");

			for (auto& sConfigName : vConfigs)
			{
				const bool bCurrentConfig = FNV1A::Hash32(sConfigName.c_str()) == FNV1A::Hash32(sConfig.c_str());
				const bool bDefault = !bVisual && !bSkins && FNV1A::Hash32(sConfigName.c_str()) == FNV1A::Hash32Const("default");

				PushID(sConfigName.c_str());
				auto tRow = FBeginListRow("##Config", bCurrentConfig);
				FTooltip(ICON_MD_ADD ICON_MD_FILE_UPLOAD_OFF, bNoSave && tRow.m_bWithin, 300.f, F::Render.IconFont);
				bool bLoad = tRow.m_bClicked, bSave = false, bDelete = false, bReset = false;

				ImDrawList* pDrawList = GetWindowDrawList();
				const float flCenter = floorf((tRow.m_vMin.y + tRow.m_vMax.y) / 2);
				const float flNameWidth = tRow.m_vMax.x - tRow.m_vMin.x - H::Draw.Scale(tRow.m_bWithin ? 82 : 22);
				pDrawList->AddText({ tRow.m_vMin.x + H::Draw.Scale(12), floorf(flCenter - GetTextLineHeight() / 2) }, FadeColor(bCurrentConfig ? F::Render.Active.Value : MixColor(F::Render.Inactive, F::Render.Active, 0.6f + 0.4f * tRow.m_flHover)), TruncateText(sConfigName, int(flNameWidth)).c_str());

				if (tRow.m_bWithin)
				{
					PushDisabled(bDefault);
					{
						bDelete = FListRowIcon(tRow, 0, ICON_MD_DELETE, "Delete");
					}
					PopDisabled();
					bSave = FListRowIcon(tRow, 1, ICON_MD_SAVE, bNoSave ? nullptr : "Save");
					FTooltip(ICON_MD_ADD ICON_MD_FILE_DOWNLOAD_OFF, bNoSave && IsItemHovered(), 300.f, F::Render.IconFont);
					if (bSave || bDelete)
						bLoad = false;
				}

				if (tRow.m_bMenu)
					OpenPopup("##Menu");
				if (FBeginContextMenu("##Menu"))
				{
					if (FMenuItem(bCurrentConfig ? "Reload" : "Load", bCurrentConfig ? ICON_MD_REFRESH : ICON_MD_DOWNLOAD))
						bLoad = true;
					if (FMenuItem("Save", ICON_MD_SAVE))
						bSave = true;
					FMenuSeparator();
					if (FMenuItem("Reset", ICON_MD_RESTART_ALT))
						bReset = true;
					PushDisabled(bDefault);
					{
						if (FMenuItem("Delete", ICON_MD_DELETE, F::Render.Danger, FMenuItemEnum::TintLabel))
							bDelete = true;
					}
					PopDisabled();
					FEndContextMenu();
				}

				if (bLoad)
					fLoad(sConfigName);
				else if (bSave)
				{
					if (!bCurrentConfig || !bVisual && !bSkins && !F::Configs.m_sCurrentVisuals.empty())
						OpenPopup("##Save");
					else
						fSave(sConfigName);
				}
				if (bDelete)
					OpenPopup("##Delete");
				if (bReset)
					OpenPopup("##Reset");

				if (FConfirmModal("##Save", "Overwrite config", std::format("'{}' will be replaced with your current settings.", sConfigName).c_str(), "Overwrite", false))
					fSave(sConfigName);
				if (FConfirmModal("##Delete", "Delete config", std::format("'{}' will be deleted for good.", sConfigName).c_str()))
				{
					if (bSkins)
						F::Configs.DeleteSkins(sConfigName);
					else if (!bVisual)
						F::Configs.DeleteConfig(sConfigName);
					else
						F::Configs.DeleteVisual(sConfigName);
				}
				if (FConfirmModal("##Reset", "Reset config", std::format("Everything in '{}' goes back to defaults.", sConfigName).c_str(), "Reset"))
				{
					if (bSkins)
						F::Configs.ResetSkins(sConfigName);
					else if (!bVisual)
						F::Configs.ResetConfig(sConfigName);
					else
						F::Configs.ResetVisual(sConfigName);
				}

				FEndListRow(tRow);
				PopID();
			}
			DebugDummy({});
		};

		TableNextColumn();
		if (Section("Config"))
		{
			static std::string sStaticName;
			fDrawConfigs(sStaticName);
		} EndSection();

		TableNextColumn();
		if (Section("Visuals"))
		{
			static std::string sStaticName;
			fDrawConfigs(sStaticName, 1);
		} EndSection();
		if (Section("Skins"))
		{
			static std::string sStaticName;
			fDrawConfigs(sStaticName, 2);
		} EndSection();

		EndTable();
		break;
	}

	case 1:
	{
		static int iBind = DEFAULT_BIND;
		static Bind_t tBind = {};

		if (!PageTable("Binds", 2))
			break;

		TableNextColumn();
		if (Section("Editor##Bind"))
		{
			FSDropdown("Name", &tBind.m_sName, {}, FSDropdownEnum::AutoUpdate);
			{
				std::vector<std::string> vParentNames = { "None" };
				std::vector<int> vParentIds = { DEFAULT_BIND };
				for (int _iBind = 0; _iBind < int(F::Binds.m_vBinds.size()); _iBind++)
				{
					bool bLoop = false;
					int _iBind2 = _iBind;
					Bind_t _tBind2;
					while (iBind != DEFAULT_BIND && F::Binds.GetBind(_iBind2, &_tBind2))
					{
						if (_iBind2 == iBind)
						{
							bLoop = true;
							break;
						}
						_iBind2 = _tBind2.m_iParent;
					}
					if (bLoop)
						continue;

					vParentNames.push_back(F::Binds.m_vBinds[_iBind].m_sName);
					vParentIds.push_back(_iBind);
				}
				std::vector<const char*> vParentEntries = {};
				for (auto& sName : vParentNames)
					vParentEntries.push_back(sName.c_str());
				if (std::find(vParentIds.begin(), vParentIds.end(), tBind.m_iParent) == vParentIds.end())
					tBind.m_iParent = DEFAULT_BIND;
				FDropdown("Parent", &tBind.m_iParent, vParentEntries, vParentIds);
			}

			Subheader("Condition");
			FDropdown("Type", &tBind.m_iType, { "Key", "Class", "Weapon type", "Item slot", "Misc" });
			switch (tBind.m_iType)
			{
			case BindEnum::Key: FDropdown("Behavior", &tBind.m_iInfo, { "Hold", "Toggle", "Double click" }); break;
			case BindEnum::Class: FDropdown("Class", &tBind.m_iInfo, { "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy" }); break;
			case BindEnum::WeaponType: FDropdown("Weapon type", &tBind.m_iInfo, { "Hitscan", "Projectile", "Melee", "Throwable" }); break;
			case BindEnum::ItemSlot: FDropdown("Item slot", &tBind.m_iInfo, { "1", "2", "3", "4", "5", "6", "7", "8", "9" }); break;
			case BindEnum::Misc: FDropdown("Misc", &tBind.m_iInfo, { "Spectated", "Spectated 1st", "Spectated 3rd", "##Divider", "Zoomed", "Aiming" }); break;
			}
			if (tBind.m_iType == BindEnum::Key)
				FKeybind("Key", tBind.m_iKey, FKeybindEnum::None, { Vars::Menu::PrimaryKey[DEFAULT_BIND], Vars::Menu::SecondaryKey[DEFAULT_BIND] });
			{
				int iNot = tBind.m_bNot;
				FDropdown("While", &iNot, { "Active", "Not active" });
				tBind.m_bNot = iNot;
			}
			FDropdown("Visibility", &tBind.m_iVisibility, { "Always", "While active", "Hidden" });

			const bool bMatch = iBind != DEFAULT_BIND && F::Binds.m_vBinds.size() > iBind;
			const bool bParentValid = tBind.m_iParent == DEFAULT_BIND || F::Binds.m_vBinds.size() > tBind.m_iParent;
			DebugDummy({ 0, H::Draw.Scale(8) });
			if (FButton(bMatch ? "Discard" : "Clear", FButtonEnum::Left))
			{
				iBind = DEFAULT_BIND;
				tBind = {};
			}
			PushDisabled(!bParentValid || tBind.m_iType == BindEnum::Key && !tBind.m_iKey);
			{
				if (FButton(bMatch ? "Save" : "Create", FButtonEnum::Right | FButtonEnum::Primary))
				{
					F::Binds.AddBind(iBind, tBind);
					iBind = DEFAULT_BIND;
					tBind = {};
				}
			}
			PopDisabled();
		} EndSection();
		if (Section("Display##Binds"))
		{
			FToggle(Vars::Menu::BindWindow);
			FToggle(Vars::Menu::BindWindowTitle);
			FToggle(Vars::Menu::MenuShowsBinds);
		} EndSection();

		TableNextColumn();
		if (Section("Binds"))
		{
			const float flStep = H::Draw.Scale(32);
			const float flListTop = GetCursorPosY();

			auto fNumberToIndex = [](int iNumber, int iLayer)
			{
				int iIndex = -1, i = -1;

				std::unordered_mapset<int> mBinds = {};
				std::function<void(int)> fGetBinds = [&](int iParent)
				{
					for (int _iBind = 0; _iBind < F::Binds.m_vBinds.size(); _iBind++)
					{
						auto& _tBind = F::Binds.m_vBinds[_iBind];
						if (iParent != _tBind.m_iParent || mBinds.contains(_iBind))
							continue;

						mBinds[_iBind];

						i++;

						if (iIndex == -1 && iParent == iLayer && iNumber <= i)
							iIndex = _iBind;
						fGetBinds(_iBind);
					}
				};
				fGetBinds(DEFAULT_BIND);

				return iIndex;
			};
			auto fPositionToIndex = [&](ImVec2 vPos, int iLayer = DEFAULT_BIND)
			{
				int iIndex = int(floorf((vPos.y - flListTop) / flStep));
				iIndex = std::clamp(iIndex, 0, int(F::Binds.m_vBinds.size() - 1));
				iIndex = fNumberToIndex(iIndex, iLayer);
				return iIndex;
			};

			static int iDragging = -1, iLayer = DEFAULT_BIND;
			if (!IsMouseDown(ImGuiMouseButton_Left))
				iDragging = -1;
			else if (iDragging != -1)
			{
				int iTo = fPositionToIndex(GetMousePos() - GetDrawPos(), iLayer);
				if (iTo != -1 && iDragging != iTo)
				{
					F::Binds.Move(iDragging, iTo);
					if (iBind == iDragging)
						iBind = iTo;
					else if (iBind < iDragging && iBind >= iTo)
						iBind++;
					else if (iBind > iDragging && iBind <= iTo)
						iBind--;
					iDragging = iTo;
				}
			}

			if (F::Binds.m_vBinds.empty())
				FEmptyState("No binds yet", flListTop - H::Draw.Scale(16));

			std::unordered_mapset<int> mBinds = {};
			std::function<void(int, int)> fGetBinds = [&](int iParent, int x)
			{
				for (int _iBind = 0; _iBind < F::Binds.m_vBinds.size(); _iBind++)
				{
					auto& _tBind = F::Binds.m_vBinds[_iBind];
					if (iParent != DEFAULT_BIND - 1 && iParent != _tBind.m_iParent || mBinds.contains(_iBind))
						continue;

					mBinds[_iBind];

					std::string sType; std::string sInfo;
					switch (_tBind.m_iType)
					{
					case BindEnum::Key:
						switch (_tBind.m_iInfo)
						{
						case BindEnum::KeyEnum::Hold: { sType = "hold"; break; }
						case BindEnum::KeyEnum::Toggle: { sType = "toggle"; break; }
						case BindEnum::KeyEnum::DoubleClick: { sType = "double"; break; }
						}
						sInfo = U::KeyHandler.String(_tBind.m_iKey);
						break;
					case BindEnum::Class:
						sType = "class";
						switch (_tBind.m_iInfo)
						{
						case BindEnum::ClassEnum::Scout: { sInfo = "scout"; break; }
						case BindEnum::ClassEnum::Soldier: { sInfo = "soldier"; break; }
						case BindEnum::ClassEnum::Pyro: { sInfo = "pyro"; break; }
						case BindEnum::ClassEnum::Demoman: { sInfo = "demoman"; break; }
						case BindEnum::ClassEnum::Heavy: { sInfo = "heavy"; break; }
						case BindEnum::ClassEnum::Engineer: { sInfo = "engineer"; break; }
						case BindEnum::ClassEnum::Medic: { sInfo = "medic"; break; }
						case BindEnum::ClassEnum::Sniper: { sInfo = "sniper"; break; }
						case BindEnum::ClassEnum::Spy: { sInfo = "spy"; break; }
						}
						break;
					case BindEnum::WeaponType:
						sType = "weapon";
						switch (_tBind.m_iInfo)
						{
						case BindEnum::WeaponTypeEnum::Hitscan: { sInfo = "hitscan"; break; }
						case BindEnum::WeaponTypeEnum::Projectile: { sInfo = "projectile"; break; }
						case BindEnum::WeaponTypeEnum::Melee: { sInfo = "melee"; break; }
						case BindEnum::WeaponTypeEnum::Throwable: { sInfo = "throwable"; break; }
						}
						break;
					case BindEnum::ItemSlot:
						sType = "slot";
						sInfo = std::format("{}", _tBind.m_iInfo + 1);
						break;
					case BindEnum::Misc:
						switch (_tBind.m_iInfo)
						{
						case BindEnum::MiscEnum::Spectated:
						case BindEnum::MiscEnum::SpectatedFirst:
						case BindEnum::MiscEnum::SpectatedThird:
							sType = "spectated";
							switch (_tBind.m_iInfo)
							{
							case BindEnum::MiscEnum::Spectated: { sInfo = "any"; break; }
							case BindEnum::MiscEnum::SpectatedFirst: { sInfo = "1st"; break; }
							case BindEnum::MiscEnum::SpectatedThird: { sInfo = "3rd"; break; }
							}
							break;
						case BindEnum::MiscEnum::Zoomed:
						case BindEnum::MiscEnum::Aiming:
							sType = "cond";
							switch (_tBind.m_iInfo)
							{
							case BindEnum::MiscEnum::Zoomed: { sInfo = "zoomed"; break; }
							case BindEnum::MiscEnum::Aiming: { sInfo = "aiming"; break; }
							}
							break;
						}
						break;
					}
					if (_tBind.m_bNot && (_tBind.m_iType != BindEnum::Key || _tBind.m_iInfo == BindEnum::KeyEnum::Hold))
						sType = std::format("not {}", sType);

					PushID(_iBind);
					PushTransparent(!F::Binds.WillBeEnabled(_iBind), true);
					auto tRow = FBeginListRow("##Bind", iBind == _iBind, H::Draw.Scale(16) * std::min(x, 3));
					const bool bRowHovered = IsItemHovered();
					ImDrawList* pDrawList = GetWindowDrawList();
					const float flCenter = floorf((tRow.m_vMin.y + tRow.m_vMax.y) / 2);

					bool bDelete = false, bToggle = false;
					{
						const ImVec2 vBox = { tRow.m_vMin.x + H::Draw.Scale(9), flCenter - H::Draw.Scale(7) };
						const bool bBoxHovered = tRow.m_bWithin && IsMouseHoveringRect(vBox - ImVec2(H::Draw.Scale(4), H::Draw.Scale(4)), vBox + ImVec2(H::Draw.Scale(18), H::Draw.Scale(18)));
						DrawCheckbox(pDrawList, vBox, _tBind.m_bEnabled ? 1.f : 0.f, bBoxHovered ? 1.f : 0.f);
						bToggle = bBoxHovered && tRow.m_bClicked;
					}

					const float flTextX = tRow.m_vMin.x + H::Draw.Scale(32);
					const std::string sMeta = sInfo.empty() ? sType : std::format("{}  {}", sType, sInfo);
					ImFont* pSmall = F::Render.FontSmall;
					const float flMeta = pSmall->CalcTextSizeA(pSmall->LegacySize, FLT_MAX, 0.f, sMeta.c_str()).x;
					const float flIcons = tRow.m_bWithin ? H::Draw.Scale(28) * 4 + H::Draw.Scale(4) : flMeta + H::Draw.Scale(12);
					pDrawList->AddText({ flTextX, floorf(flCenter - GetTextLineHeight() / 2) }, FadeColor(_tBind.m_bActive ? F::Render.Accent : F::Render.Active), TruncateText(_tBind.m_sName, int(tRow.m_vMax.x - flIcons - flTextX - H::Draw.Scale(8))).c_str());

					if (tRow.m_bWithin)
					{
						bDelete = FListRowIcon(tRow, 0, ICON_MD_DELETE, "Delete");
						if (FListRowIcon(tRow, 1, ICON_MD_EDIT, CurrentBind == _iBind ? "Stop editing in menu" : "Edit in menu", CurrentBind == _iBind ? F::Render.Accent.Value : ImVec4(1, 1, 1, -1)))
							CurrentBind = CurrentBind != _iBind ? _iBind : DEFAULT_BIND;
						if (FListRowIcon(tRow, 2, !_tBind.m_bNot ? ICON_MD_CODE : ICON_MD_CODE_OFF, !_tBind.m_bNot ? "While active" : "While not active"))
							_tBind.m_bNot = !_tBind.m_bNot;
						const char* sVisibility = _tBind.m_iVisibility == BindVisibilityEnum::Always ? "Always shown" : _tBind.m_iVisibility == BindVisibilityEnum::Hidden ? "Hidden" : "Shown while active";
						if (FListRowIcon(tRow, 3, _tBind.m_iVisibility == BindVisibilityEnum::Always ? ICON_MD_VISIBILITY : ICON_MD_VISIBILITY_OFF, sVisibility))
							_tBind.m_iVisibility = (_tBind.m_iVisibility + 1) % 3;
					}
					else
					{
						pDrawList->AddText(pSmall, pSmall->LegacySize, { tRow.m_vMax.x - flMeta - H::Draw.Scale(10), floorf(flCenter - pSmall->LegacySize / 2) }, FadeColor(MixColor(F::Render.Inactive, F::Render.Active, 0.3f)), sMeta.c_str());
						if (CurrentBind == _iBind)
							pDrawList->AddCircleFilled({ tRow.m_vMax.x - flMeta - H::Draw.Scale(20), flCenter }, H::Draw.Scale(3), FadeColor(F::Render.Accent), 12);
					}
					PopTransparent(1, 1);

					if (bToggle)
						_tBind.m_bEnabled = !_tBind.m_bEnabled;
					else if (tRow.m_bClicked && !bDelete)
					{
						iBind = _iBind;
						tBind = _tBind;
					}
					else if (iDragging == -1 && bRowHovered && IsMouseDown(ImGuiMouseButton_Left))
						iDragging = _iBind, iLayer = iParent;

					if (tRow.m_bMenu)
						OpenPopup("##Menu");
					if (FBeginContextMenu("##Menu"))
					{
						if (FMenuItem("Load into editor", ICON_MD_TUNE))
						{
							iBind = _iBind;
							tBind = _tBind;
						}
						if (FMenuItem(CurrentBind == _iBind ? "Stop editing in menu" : "Edit in menu", ICON_MD_EDIT))
							CurrentBind = CurrentBind != _iBind ? _iBind : DEFAULT_BIND;
						if (FMenuItem(_tBind.m_bEnabled ? "Disable" : "Enable", _tBind.m_bEnabled ? ICON_MD_TOGGLE_OFF : ICON_MD_TOGGLE_ON))
							_tBind.m_bEnabled = !_tBind.m_bEnabled;
						FMenuSeparator();
						if (FMenuItem("Delete", ICON_MD_DELETE, F::Render.Danger, FMenuItemEnum::TintLabel))
							bDelete = true;
						FEndContextMenu();
					}

					if (bDelete)
					{
						if (_tBind.m_vVars.size() <= 1 && !F::Binds.HasChildren(_iBind) || U::KeyHandler.Down(VK_SHIFT))
						{
							F::Binds.RemoveBind(_iBind);
							iBind = DEFAULT_BIND;
							tBind = {};
						}
						else
							OpenPopup("##Delete");
					}
					if (FConfirmModal("##Delete", "Delete bind", std::format("'{}'{} and the values it holds will be removed.", _tBind.m_sName, F::Binds.HasChildren(_iBind) ? ", its children" : "").c_str()))
					{
						F::Binds.RemoveBind(_iBind);
						iBind = DEFAULT_BIND;
						tBind = {};
					}

					FEndListRow(tRow);
					PopID();

					if (iParent != DEFAULT_BIND - 1)
						fGetBinds(_iBind, x + 1);
				}
			};
			fGetBinds(DEFAULT_BIND, 0);

			if (F::Binds.m_vBinds.size() > mBinds.size())
			{
				Subheader("Dangling");
				fGetBinds(DEFAULT_BIND - 1, 0);
			}
			DebugDummy({});
		} EndSection();

		EndTable();
		break;
	}

	case 2:
	{
		static TextEditor tTextEditor;
		static std::string sCurrentMaterial;
		static bool bLockedMaterial;

		const bool bTable = !sCurrentMaterial.empty() && BeginTable("Materials", 2);
		if (bTable)
		{
			TableSetupColumn("##Manager", ImGuiTableColumnFlags_WidthFixed, H::Draw.Scale(280));
			TableSetupColumn("##Editor", ImGuiTableColumnFlags_WidthStretch);
			TableNextColumn();
		}

		if (Section("Materials"))
		{
			static std::string sStaticName;
			bool bFolder = false;
			if (FCreateRow(sStaticName, "New material", &bFolder))
			{
				F::Materials.AddMaterial(sStaticName.c_str());
				sStaticName.clear();
			}
			if (bFolder)
				ShellExecuteA(NULL, NULL, F::Configs.m_sMaterialsPath.c_str(), NULL, NULL, SW_SHOWNORMAL);

			std::vector<Material_t> vMaterials;
			for (auto& tMaterial : F::Materials.m_mMaterials | std::views::values)
				vMaterials.push_back(tMaterial);

			std::sort(vMaterials.begin(), vMaterials.end(), [&](const auto& a, const auto& b) -> bool
			{
				if (FNV1A::Hash32(a.m_sName.c_str()) == FNV1A::Hash32Const("None"))
					return FNV1A::Hash32(b.m_sName.c_str()) != FNV1A::Hash32Const("None");
				if (FNV1A::Hash32(b.m_sName.c_str()) == FNV1A::Hash32Const("None"))
					return false;

				if (a.m_bLocked && !b.m_bLocked)
					return true;
				if (!a.m_bLocked && b.m_bLocked)
					return false;

				return a.m_sName < b.m_sName;
			});

			for (auto& tMaterial : vMaterials)
			{
				PushID(tMaterial.m_sName.c_str());
				auto tRow = FBeginListRow("##Material", sCurrentMaterial == tMaterial.m_sName);
				ImDrawList* pDrawList = GetWindowDrawList();
				const float flCenter = floorf((tRow.m_vMin.y + tRow.m_vMax.y) / 2);
				bool bOpen = tRow.m_bClicked, bDelete = false;

				if (tMaterial.m_bLocked)
				{
					ImFont* pIcon = F::Render.IconFont;
					const float flSize = H::Draw.Scale(13);
					pDrawList->AddText(pIcon, flSize, { tRow.m_vMin.x + H::Draw.Scale(10), floorf(flCenter - flSize / 2) }, FadeColor(F::Render.Inactive), ICON_MD_LOCK);
				}
				pDrawList->AddText({ tRow.m_vMin.x + H::Draw.Scale(28), floorf(flCenter - GetTextLineHeight() / 2) }, FadeColor(tMaterial.m_bLocked ? MixColor(F::Render.Inactive, F::Render.Active, 0.5f) : F::Render.Active.Value), TruncateText(tMaterial.m_sName, int(tRow.m_vMax.x - tRow.m_vMin.x - H::Draw.Scale(70))).c_str());

				if (tRow.m_bWithin && !tMaterial.m_bLocked)
				{
					bDelete = FListRowIcon(tRow, 0, ICON_MD_DELETE, "Delete");
					if (bDelete)
						bOpen = false;
				}

				if (tRow.m_bMenu)
					OpenPopup("##Menu");
				if (FBeginContextMenu("##Menu"))
				{
					if (FMenuItem(tMaterial.m_bLocked ? "View" : "Edit", tMaterial.m_bLocked ? ICON_MD_VISIBILITY : ICON_MD_EDIT))
						bOpen = true;
					if (!tMaterial.m_bLocked)
					{
						FMenuSeparator();
						if (FMenuItem("Delete", ICON_MD_DELETE, F::Render.Danger, FMenuItemEnum::TintLabel))
							bDelete = true;
					}
					FEndContextMenu();
				}

				if (bOpen)
				{
					sCurrentMaterial = tMaterial.m_sName;
					bLockedMaterial = tMaterial.m_bLocked;

					tTextEditor.SetText(F::Materials.GetVMT(FNV1A::Hash32(sCurrentMaterial.c_str())));
					tTextEditor.SetReadOnlyEnabled(bLockedMaterial);
				}
				if (bDelete)
					OpenPopup("##Delete");
				if (FConfirmModal("##Delete", "Delete material", std::format("'{}' will be deleted for good.", tMaterial.m_sName).c_str()))
				{
					if (sCurrentMaterial == tMaterial.m_sName)
						sCurrentMaterial.clear();
					F::Materials.RemoveMaterial(tMaterial.m_sName.c_str());
				}

				FEndListRow(tRow);
				PopID();
			}
			DebugDummy({});
		}
		else
			SetScrollY(0);
		EndSection();

		if (bTable)
		{
			TableNextColumn();
			if (!sCurrentMaterial.empty())
			{
				SetCursorPosY(GetScrollY() + GetStyle().WindowPadding.y);
				if (Section(bLockedMaterial ? "Viewer" : "Editor", 0, GetWindowHeight() - GetStyle().WindowPadding.y * 2, true))
				{
					const float flPad = GetStyle().WindowPadding.x;
					const ImVec2 vContent = GetCursorPos();
					SetCursorPos({ GetWindowWidth() - flPad - H::Draw.Scale(26), H::Draw.Scale(4) });
					if (FIconButton(ICON_MD_CLOSE, "Close", H::Draw.Scale(26)))
						sCurrentMaterial.clear();
					if (!bLockedMaterial)
					{
						SetCursorPos({ GetWindowWidth() - flPad - H::Draw.Scale(56), H::Draw.Scale(4) });
						if (FIconButton(ICON_MD_SAVE, "Save", H::Draw.Scale(26)))
						{
							auto sText = tTextEditor.GetText();
							F::Materials.EditMaterial(sCurrentMaterial.c_str(), sText.c_str());
						}
					}
					SetCursorPos(vContent);
					FNote(sCurrentMaterial.c_str());

					tTextEditor.SetLanguage(TextEditor::Language::Cpp());
					TextEditor::Palette tPalette = {{
						F::Render.Active,
						F::Render.Active,
						F::Render.Active,
						F::Render.Active,
						F::Render.Accent,
						F::Render.Inactive,
						F::Render.Inactive,
						F::Render.Inactive,
						F::Render.Inactive,
						F::Render.Inactive,
						F::Render.Background1p5,
						F::Render.Active,
						ImColor(F::Render.Inactive.Value.x, F::Render.Inactive.Value.y, F::Render.Inactive.Value.z, 0.5f),
						ImColor(F::Render.Inactive.Value.x, F::Render.Inactive.Value.y, F::Render.Inactive.Value.z, 0.1f),
						IM_COL32( 70,  70,  70, 255),
						IM_COL32(140, 140, 140, 255),
						IM_COL32(246, 222,  36, 255),
						IM_COL32( 66, 120, 198, 255),
						IM_COL32(213,  96, 213, 255),
						IM_COL32(198,   8,  32, 255),
						F::Render.Inactive,
						F::Render.Active,
					}};
					tTextEditor.SetPalette(tPalette);

					PushFont(F::Render.FontMono);
					tTextEditor.Render("TextEditor");
					PopFont();
				} EndSection();
			}

			EndTable();
		}
		break;
	}

	case 3:
	{
		if (!PageTable("SettingsMisc", 2))
			break;

		TableNextColumn();
		if (Section("Game"))
		{
			if (FButton("Full update", FButtonEnum::Left))
				I::EngineClient->ClientCmd_Unrestricted("cl_fullupdate");
			if (FButton("Retry", FButtonEnum::Right))
				I::EngineClient->ClientCmd_Unrestricted("retry");
			if (FButton("Console", FButtonEnum::Left))
				I::EngineClient->ClientCmd_Unrestricted("toggleconsole");
			if (FButton("Reload materials", FButtonEnum::Right) && F::Materials.m_bLoaded)
				F::Materials.ReloadMaterials();
		} EndSection();
		if (!I::EngineClient->IsConnected())
		{
			if (Section("Achievements"))
			{
				static int iAchievementOp = 0;
				if (FButton("Unlock all", FButtonEnum::Left))
				{
					iAchievementOp = 0;
					OpenPopup("##Unlock");
				}
				if (FButton("Unlock item", FButtonEnum::Right))
				{
					iAchievementOp = 1;
					OpenPopup("##Unlock");
				}
				if (FButton("Lock all", FButtonEnum::Left))
				{
					iAchievementOp = 0;
					OpenPopup("##Lock");
				}
				if (FButton("Lock item", FButtonEnum::Right))
				{
					iAchievementOp = 1;
					OpenPopup("##Lock");
				}
				if (FConfirmModal("##Unlock", "Unlock achievements", std::format("Every {}achievement on this account gets unlocked.", iAchievementOp ? "item " : "").c_str(), "Unlock", false))
				{
					if (iAchievementOp) F::Misc.UnlockItemAchievements();
					else F::Misc.UnlockAchievements();
				}
				if (FConfirmModal("##Lock", "Lock achievements", std::format("Every {}achievement on this account gets locked again.", iAchievementOp ? "item " : "").c_str(), "Lock"))
				{
					if (iAchievementOp) F::Misc.LockItemAchievements();
					else F::Misc.LockAchievements();
				}
			} EndSection();
		}
#ifdef WORLD_DEBUG
		if (Section("World"))
		{
			FDropdown(Vars::World::Faces);
			FDropdown(Vars::World::Draw);
			FSlider(Vars::World::Offset);
			FSlider(Vars::World::Resize);
			FColorPicker(Vars::World::BoxBrush, FColorPickerEnum::Full);
			FColorPicker(Vars::World::PlaneBrush, FColorPickerEnum::Full);
			FColorPicker(Vars::World::Displacement, FColorPickerEnum::Full);
			FColorPicker(Vars::World::Prop, FColorPickerEnum::Full);
			FColorPicker(Vars::World::Entity, FColorPickerEnum::Full);
		} EndSection();
#endif

		TableNextColumn();
		if (Section("Debug"))
		{
			FToggle(Vars::Debug::Info);
			FToggle(Vars::Debug::Logging);
			FToggle(Vars::Debug::Options);
			FToggle(Vars::Debug::CrashLogging);
#ifdef DEBUG_TRACES
			FToggle(Vars::Debug::VisualizeTraces);
			FToggle(Vars::Debug::VisualizeTraceHits);
#endif
			FToggle(Vars::Config::LoadDebugSettings);
			FTooltip("Load/Save debug settings in this config");
		} EndSection();
		if (Vars::Debug::Options.Value && I::EngineClient->IsConnected())
		{
			if (Section("Debug drawing"))
			{
				FToggle(Vars::Debug::DrawHitboxes);
				DebugDummy({ 0, H::Draw.Scale(6) });

				auto fStorage = [](const char* sLabel, auto& vStorage)
				{
					if (FButton(std::format("Restore {}", sLabel).c_str(), FButtonEnum::Left))
					{
						for (auto& tEntry : vStorage)
							tEntry.m_flTime = I::GlobalVars->curtime + 60.f;
					}
					if (FButton(std::format("Clear {}", sLabel).c_str(), FButtonEnum::Right))
						vStorage.clear();
				};
				if (FButton("Restore all", FButtonEnum::Left))
				{
					for (auto& tLine : G::LineStorage)
						tLine.m_flTime = I::GlobalVars->curtime + 60.f;
					for (auto& tPath : G::PathStorage)
						tPath.m_flTime = I::GlobalVars->curtime + 60.f;
					for (auto& tBox : G::BoxStorage)
						tBox.m_flTime = I::GlobalVars->curtime + 60.f;
					for (auto& tSphere : G::SphereStorage)
						tSphere.m_flTime = I::GlobalVars->curtime + 60.f;
					for (auto& tSwept : G::SweptStorage)
						tSwept.m_flTime = I::GlobalVars->curtime + 60.f;
					for (auto& tTriangle : G::TriangleStorage)
						tTriangle.m_flTime = I::GlobalVars->curtime + 60.f;
				}
				if (FButton("Clear all", FButtonEnum::Right))
				{
					G::LineStorage.clear();
					G::PathStorage.clear();
					G::BoxStorage.clear();
					G::SphereStorage.clear();
					G::SweptStorage.clear();
					G::TriangleStorage.clear();
				}
				fStorage("lines", G::LineStorage);
				fStorage("paths", G::PathStorage);
				fStorage("boxes", G::BoxStorage);
				fStorage("spheres", G::SphereStorage);
				fStorage("swept", G::SweptStorage);
				fStorage("triangles", G::TriangleStorage);
			} EndSection();
		}
#ifdef DEBUG_HOOKS
		if (Section("Hooks"))
		{
			auto SetAll = [](bool bEnable)
			{
				for (auto& pBase : G::Vars)
				{
					if (std::string(pBase->Name()).find("Vars::Hooks::") == std::string::npos)
						continue;

					switch (FNV1A::Hash32(pBase->Name()))
					{
					case FNV1A::Hash32Const("Vars::Hooks::Direct3DDevice9_Present"):
					case FNV1A::Hash32Const("Vars::Hooks::Direct3DDevice9_Reset"):
						break;
					default:
						pBase->As<bool>()->Map[DEFAULT_BIND] = bEnable;
					}
				}
			};
			if (FButton("Enable all", FButtonEnum::Left))
				SetAll(true);
			if (FButton("Disable all", FButtonEnum::Right))
				SetAll(false);

			for (auto& pBase : G::Vars)
			{
				if (std::string(pBase->Name()).find("Vars::Hooks::") == std::string::npos)
					continue;

				FToggle(*pBase->As<bool>());
			}
		} EndSection();
#endif

		EndTable();
		break;
	}
	}
}

#pragma endregion

#pragma region Draggables
static inline void SquareConstraints(ImGuiSizeCallbackData* data)
{

	data->DesiredSize.x = data->DesiredSize.y = (data->DesiredSize.x + data->DesiredSize.y) / 2;
}

static inline void OptionalConstraints(ImGuiSizeCallbackData* data)
{
	if (U::KeyHandler.Down(VK_SHIFT))
		SquareConstraints(data);
}

struct DragBoxStorage_t
{
	DragBox_t m_tDragBox;
	float m_flScale;
};
static std::unordered_map<uint32_t, DragBoxStorage_t> s_mDragBoxStorage = {};
void CMenu::AddDraggable(const char* sLabel, ConfigVar<DragBox_t>& tVar, bool bShouldDraw, ImVec2 vSize)
{
	using namespace ImGui;

	if (!bShouldDraw)
		return;

	auto tDragBox = FGet(tVar, true);
	auto uHash = FNV1A::Hash32(sLabel);

	bool bContains = s_mDragBoxStorage.contains(uHash);
	auto& tStorage = s_mDragBoxStorage[uHash];

	SetNextWindowSize(vSize, ImGuiCond_Always);
	if (!bContains || tDragBox != tStorage.m_tDragBox || H::Draw.Scale() != tStorage.m_flScale)
		SetNextWindowPos({ float(tDragBox.x - vSize.x / 2), float(tDragBox.y) }, ImGuiCond_Always);

	PushStyleColor(ImGuiCol_WindowBg, F::Render.AccentDim.Value);
	PushStyleVar(ImGuiStyleVar_WindowRounding, H::Draw.Scale(6));
	PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	PushStyleVar(ImGuiStyleVar_WindowMinSize, vSize);
	if (Begin(sLabel, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
	{
		ImVec2 vWindowPos = GetWindowPos();

		tDragBox.x = vWindowPos.x + vSize.x / 2, tDragBox.y = vWindowPos.y;
		tStorage = { tDragBox, H::Draw.Scale() };
		FSet(tVar, tDragBox);

		const ImVec2 vLabelSize = FCalcTextSize(sLabel);
		GetWindowDrawList()->AddText(vWindowPos + ImVec2(floorf((vSize.x - vLabelSize.x) * 0.5f), floorf((vSize.y - vLabelSize.y) * 0.5f)), FadeColor(F::Render.Active), sLabel);

		End();
	}
	PopStyleVar(3);
	PopStyleColor();
}

struct WindowBoxStorage_t
{
	WindowBox_t m_tWindowBox;
	float m_flScale;
};
static std::unordered_map<uint32_t, WindowBoxStorage_t> s_mWindowBoxStorage = {};
void CMenu::AddResizableDraggable(const char* sLabel, ConfigVar<WindowBox_t>& tVar, bool bShouldDraw, ImGuiSizeCallback fCustomCallback, ImVec2 vMinSize, ImVec2 vMaxSize)
{
	using namespace ImGui;

	if (!bShouldDraw)
		return;

	auto tWindowBox = FGet(tVar, true);
	auto uHash = FNV1A::Hash32(sLabel);

	bool bContains = s_mWindowBoxStorage.contains(uHash);
	auto& tStorage = s_mWindowBoxStorage[uHash];

	SetNextWindowSizeConstraints(vMinSize, vMaxSize, fCustomCallback);
	if (!bContains || tWindowBox != tStorage.m_tWindowBox || H::Draw.Scale() != tStorage.m_flScale)
	{
		SetNextWindowPos({ float(tWindowBox.x - tWindowBox.w / 2), float(tWindowBox.y) }, ImGuiCond_Always);
		SetNextWindowSize({ float(tWindowBox.w), float(tWindowBox.h) }, ImGuiCond_Always);
	}

	PushStyleColor(ImGuiCol_WindowBg, F::Render.AccentDim.Value);
	PushStyleVar(ImGuiStyleVar_WindowRounding, H::Draw.Scale(6));
	PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	if (Begin(sLabel, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
	{
		ImVec2 vWindowPos = GetWindowPos();
		ImVec2 vWinSize = GetWindowSize();

		tWindowBox.w = vWinSize.x, tWindowBox.h = vWinSize.y;
		tWindowBox.x = vWindowPos.x + tWindowBox.w / 2, tWindowBox.y = vWindowPos.y;
		tStorage = { tWindowBox, H::Draw.Scale() };
		FSet(tVar, tWindowBox);

		const ImVec2 vLabelSize = FCalcTextSize(sLabel);
		GetWindowDrawList()->AddText(vWindowPos + ImVec2(floorf((vWinSize.x - vLabelSize.x) * 0.5f), floorf((vWinSize.y - vLabelSize.y) * 0.5f)), FadeColor(F::Render.Active), sLabel);

		End();
	}
	PopStyleVar(2);
	PopStyleColor();
}

struct BindInfo_t
{
	const char* sName;
	std::string sInfo;
	std::string sState;

	int iBind;
	Bind_t& tBind;
};
void CMenu::DrawBinds()
{
	using namespace ImGui;

	if (!F::Binds.m_bDisplay)
		return;

	std::vector<BindInfo_t> vInfo;
	std::function<void(int)> fGetBinds = [&](int iParent)
	{
		for (int iBind = 0; iBind < F::Binds.m_vBinds.size(); iBind++)
		{
			auto& tBind = F::Binds.m_vBinds[iBind];
			if (iParent != tBind.m_iParent || !tBind.m_bEnabled && !m_bIsOpen)
				continue;

			if (tBind.m_iVisibility == BindVisibilityEnum::Always || tBind.m_iVisibility == BindVisibilityEnum::WhileActive && tBind.m_bActive || m_bIsOpen)
			{
				std::string sType; std::string sInfo;
				switch (tBind.m_iType)
				{
				case BindEnum::Key:
					switch (tBind.m_iInfo)
					{
					case BindEnum::KeyEnum::Hold: { sType = "hold"; break; }
					case BindEnum::KeyEnum::Toggle: { sType = "toggle"; break; }
					case BindEnum::KeyEnum::DoubleClick: { sType = "double"; break; }
					}
					sInfo = U::KeyHandler.String(tBind.m_iKey);
					break;
				case BindEnum::Class:
					sType = "class";
					switch (tBind.m_iInfo)
					{
					case BindEnum::ClassEnum::Scout: { sInfo = "scout"; break; }
					case BindEnum::ClassEnum::Soldier: { sInfo = "soldier"; break; }
					case BindEnum::ClassEnum::Pyro: { sInfo = "pyro"; break; }
					case BindEnum::ClassEnum::Demoman: { sInfo = "demoman"; break; }
					case BindEnum::ClassEnum::Heavy: { sInfo = "heavy"; break; }
					case BindEnum::ClassEnum::Engineer: { sInfo = "engineer"; break; }
					case BindEnum::ClassEnum::Medic: { sInfo = "medic"; break; }
					case BindEnum::ClassEnum::Sniper: { sInfo = "sniper"; break; }
					case BindEnum::ClassEnum::Spy: { sInfo = "spy"; break; }
					}
					break;
				case BindEnum::WeaponType:
					sType = "weapon";
					switch (tBind.m_iInfo)
					{
					case BindEnum::WeaponTypeEnum::Hitscan: { sInfo = "hitscan"; break; }
					case BindEnum::WeaponTypeEnum::Projectile: { sInfo = "projectile"; break; }
					case BindEnum::WeaponTypeEnum::Melee: { sInfo = "melee"; break; }
					case BindEnum::WeaponTypeEnum::Throwable: { sInfo = "throwable"; break; }
					}
					break;
				case BindEnum::ItemSlot:
					sType = "slot";
					sInfo = std::format("{}", tBind.m_iInfo + 1);
					break;
				case BindEnum::Misc:
					switch (tBind.m_iInfo)
					{
					case BindEnum::MiscEnum::Spectated:
					case BindEnum::MiscEnum::SpectatedFirst:
					case BindEnum::MiscEnum::SpectatedThird:
						sType = "spectated";
						switch (tBind.m_iInfo)
						{
						case BindEnum::MiscEnum::Spectated: { sInfo = "any"; break; }
						case BindEnum::MiscEnum::SpectatedFirst: { sInfo = "1st"; break; }
						case BindEnum::MiscEnum::SpectatedThird: { sInfo = "3rd"; break; }
						}
						break;
					case BindEnum::MiscEnum::Zoomed:
					case BindEnum::MiscEnum::Aiming:
						sType = "cond";
						switch (tBind.m_iInfo)
						{
						case BindEnum::MiscEnum::Zoomed: { sInfo = "zoomed"; break; }
						case BindEnum::MiscEnum::Aiming: { sInfo = "aiming"; break; }
						}
						break;
					}
					break;
				}
				if (tBind.m_bNot && (tBind.m_iType != BindEnum::Key || tBind.m_iInfo == BindEnum::KeyEnum::Hold))
					sInfo = std::format("not {}", sInfo);

				vInfo.emplace_back(tBind.m_sName.c_str(), sType, sInfo, iBind, tBind);
			}

			if (tBind.m_bActive || m_bIsOpen)
				fGetBinds(iBind);
		}
	};
	fGetBinds(DEFAULT_BIND);
	if (vInfo.empty())
		return;

	static DragBox_t tOld = { -2147483648, -2147483648 };
	DragBox_t tDragBox = m_bIsOpen ? FGet(Vars::Menu::BindsDisplay, true) : Vars::Menu::BindsDisplay.Value;
	if (tDragBox != tOld)
		SetNextWindowPos({ float(tDragBox.x), float(tDragBox.y) }, ImGuiCond_Always);

	ImFont* pSmall = F::Render.FontSmall, *pRegular = F::Render.FontRegular;
	auto fTextWidth = [&](ImFont* pFont, const char* sText) { return pFont->CalcTextSizeA(pFont->LegacySize, FLT_MAX, 0.f, sText).x; };
	float flNameWidth = 0, flTypeWidth = 0, flChipWidth = 0;
	for (auto& [sName, sType, sKey, iBind, tBind] : vInfo)
	{
		flNameWidth = std::max(flNameWidth, fTextWidth(pRegular, sName));
		flTypeWidth = std::max(flTypeWidth, fTextWidth(pSmall, sType.c_str()));
		flChipWidth = std::max(flChipWidth, OverlayChipWidth(sKey.c_str()));
	}

	const bool bTitle = Vars::Menu::BindWindowTitle.Value;
	const float flPad = H::Draw.Scale(12), flHeader = bTitle ? H::Draw.Scale(32) : 0.f;
	const float flRow = H::Draw.Scale(26), flTop = flHeader + H::Draw.Scale(6), flIcon = H::Draw.Scale(22);
	const float flIcons = m_bIsOpen ? flIcon * 4 + H::Draw.Scale(6) : 0.f;
	const float flWidth = std::max(flPad * 2 + flNameWidth + H::Draw.Scale(18) + flTypeWidth + H::Draw.Scale(10) + flChipWidth + flIcons, H::Draw.Scale(bTitle ? 170 : 60));
	const float flHeight = flTop + flRow * vInfo.size() + H::Draw.Scale(6);
	SetNextWindowSize({ flWidth, flHeight }, ImGuiCond_Always);
	PushStyleVar(ImGuiStyleVar_WindowMinSize, { H::Draw.Scale(40), H::Draw.Scale(40) });
	if (Begin("Binds", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
	{
		const ImVec2 vWindowPos = GetWindowPos();

		RenderOverlayWindow(flHeader, ICON_MD_KEYBOARD, "Binds");

		tDragBox.x = vWindowPos.x; tDragBox.y = vWindowPos.y; tOld = tDragBox;
		if (m_bIsOpen)
			FSet(Vars::Menu::BindsDisplay, tDragBox);

		ImDrawList* pDrawList = GetWindowDrawList();
		int i = 0; for (auto& [sName, sType, sKey, iBind, tBind] : vInfo)
		{
			const float flY = flTop + flRow * i;
			const float flCenterY = vWindowPos.y + flY + flRow / 2;
			const float flAlpha = m_bIsOpen && !F::Binds.WillBeEnabled(iBind) ? 0.5f : 1.f;

			pDrawList->AddText(pRegular, pRegular->LegacySize, { vWindowPos.x + flPad, floorf(flCenterY - pRegular->LegacySize / 2) }, FadeColor(tBind.m_bActive ? F::Render.Active.Value : MixColor(F::Render.Inactive, F::Render.Active, 0.55f), flAlpha), sName);
			pDrawList->AddText(pSmall, pSmall->LegacySize, { vWindowPos.x + flPad + flNameWidth + H::Draw.Scale(18), floorf(flCenterY - pSmall->LegacySize / 2) }, FadeColor(F::Render.Inactive, flAlpha), sType.c_str());
			DrawOverlayChip(pDrawList, vWindowPos.x + flWidth - flPad - flIcons, flCenterY, sKey.c_str(), tBind.m_bActive, F::Render.Accent.Value, flAlpha, flChipWidth);

			if (m_bIsOpen)
			{
				PushID(iBind);
				auto fIcon = [&](int iSlot, const char* sIcon, float flIconAlpha = 1.f)
				{
					SetCursorPos({ flWidth - H::Draw.Scale(8) - flIcon * (iSlot + 1), flY + floorf((flRow - flIcon) / 2) });
					PushStyleVar(ImGuiStyleVar_Alpha, flAlpha * flIconAlpha);
					const bool bReturn = IconButton(sIcon, flIcon);
					PopStyleVar();
					return bReturn;
				};
				const bool bDelete = fIcon(0, ICON_MD_DELETE);
				const bool bNot = fIcon(1, !tBind.m_bNot ? ICON_MD_CODE : ICON_MD_CODE_OFF);
				const bool bVisibility = fIcon(2, tBind.m_iVisibility == BindVisibilityEnum::Always ? ICON_MD_VISIBILITY : ICON_MD_VISIBILITY_OFF, tBind.m_iVisibility == BindVisibilityEnum::Hidden ? 0.5f : 1.f);
				const bool bEnable = fIcon(3, tBind.m_bEnabled ? ICON_MD_TOGGLE_ON : ICON_MD_TOGGLE_OFF);

				bool bRemove = false;
				if (bEnable)
					tBind.m_bEnabled = !tBind.m_bEnabled;
				else if (bVisibility)
					tBind.m_iVisibility = (tBind.m_iVisibility + 1) % 3;
				else if (bNot)
					tBind.m_bNot = !tBind.m_bNot;
				else if (bDelete)
				{
					if (tBind.m_vVars.size() <= 1 && !F::Binds.HasChildren(iBind) || U::KeyHandler.Down(VK_SHIFT))
						bRemove = true;
					else
						OpenPopup("##Delete");
				}

				PushFont(F::Render.FontRegular);
				if (FConfirmModal("##Delete", "Delete bind", std::format("'{}'{} and the values it holds will be removed.", tBind.m_sName, F::Binds.HasChildren(iBind) ? ", its children" : "").c_str()))
					bRemove = true;
				PopFont();
				PopID();

				if (bRemove)
				{
					F::Binds.RemoveBind(iBind);
					break;
				}
			}

			i++;
		}

		End();
	}
	PopStyleVar();
}
#pragma endregion

static inline void ManageVars()
{
	Vars::ESP::ActiveGroups.m_vValues = {};
	for (auto& tGroup : F::Groups.m_vGroups)
		Vars::ESP::ActiveGroups.m_vValues.push_back(tGroup.m_sName.c_str());
}

static inline std::string GetNoticePath()
{
	return F::Configs.m_sConfigPath + "notice_accepted.txt";
}

static inline bool IsRussianLocale()
{
	const auto fnIsRussian = [](LANGID iLang) { return PRIMARYLANGID(iLang) == LANG_RUSSIAN; };
	return fnIsRussian(GetUserDefaultUILanguage()) || fnIsRussian(GetUserDefaultLangID()) || fnIsRussian(GetSystemDefaultLangID());
}

void CMenu::DrawNotice()
{
	using namespace ImGui;

	static const bool bRussian = IsRussianLocale();
	const auto fnText = [](const char8_t* sEnglish, const char8_t* sRussian) { return reinterpret_cast<const char*>(bRussian ? sRussian : sEnglish); };
	constexpr const char* sRepo = "https://github.com/GatoPotato658/Unibox";
	constexpr const char* sPopup = "##UniboxNotice";

	if (!IsPopupOpen(sPopup))
		OpenPopup(sPopup);

	SetNextWindowSize({ H::Draw.Scale(480), 0 });
	if (FBeginPopupModal(sPopup))
	{
		static float flShown = 0.f;
		if ((flShown += GetIO().DeltaTime) >= 30.f)
		{
			m_bNoticePending = false;
			CloseCurrentPopup();
			I::MatSystemSurface->SetCursorAlwaysVisible(m_bIsOpen = false);
		}

		PushStyleColor(ImGuiCol_Text, F::Render.Accent.Value);
		PushFont(F::Render.IconFont);
		TextUnformatted(ICON_MD_WARNING);
		PopFont();
		PopStyleColor();
		SameLine(0, H::Draw.Scale(8));
		PushFont(F::Render.FontLarge);
		TextUnformatted(fnText(u8"important notice", u8"\u0432\u0430\u0436\u043D\u043E\u0435 \u0443\u0432\u0435\u0434\u043E\u043C\u043B\u0435\u043D\u0438\u0435"));
		PopFont();
		Dummy({ 0, H::Draw.Scale(10) });

		PushTextWrapPos(GetWindowWidth() - GetStyle().WindowPadding.x);
		PushStyleColor(ImGuiCol_Text, MixColor(F::Render.Inactive, F::Render.Active, 0.75f));

		TextUnformatted(fnText(
			u8"If you got this DLL directly from the official GitHub repository, you should be fine. Unibox is free and open-source software, which means anyone can download the source code, modify it, and redistribute their own version\u2014including versions with malicious code added.",
			u8"\u0415\u0441\u043B\u0438 \u0432\u044B \u043F\u043E\u043B\u0443\u0447\u0438\u043B\u0438 \u044D\u0442\u0443 DLL \u043D\u0430\u043F\u0440\u044F\u043C\u0443\u044E \u0438\u0437 \u043E\u0444\u0438\u0446\u0438\u0430\u043B\u044C\u043D\u043E\u0433\u043E \u0440\u0435\u043F\u043E\u0437\u0438\u0442\u043E\u0440\u0438\u044F GitHub, \u0432\u0441\u0451 \u0432 \u043F\u043E\u0440\u044F\u0434\u043A\u0435. Unibox \u2014 \u0431\u0435\u0441\u043F\u043B\u0430\u0442\u043D\u043E\u0435 \u043F\u0440\u043E\u0433\u0440\u0430\u043C\u043C\u043D\u043E\u0435 \u043E\u0431\u0435\u0441\u043F\u0435\u0447\u0435\u043D\u0438\u0435 \u0441 \u043E\u0442\u043A\u0440\u044B\u0442\u044B\u043C \u0438\u0441\u0445\u043E\u0434\u043D\u044B\u043C \u043A\u043E\u0434\u043E\u043C, \u0430 \u0437\u043D\u0430\u0447\u0438\u0442, \u043B\u044E\u0431\u043E\u0439 \u043C\u043E\u0436\u0435\u0442 \u0441\u043A\u0430\u0447\u0430\u0442\u044C \u0438\u0441\u0445\u043E\u0434\u043D\u044B\u0439 \u043A\u043E\u0434, \u0438\u0437\u043C\u0435\u043D\u0438\u0442\u044C \u0435\u0433\u043E \u0438 \u0440\u0430\u0441\u043F\u0440\u043E\u0441\u0442\u0440\u0430\u043D\u044F\u0442\u044C \u0441\u043E\u0431\u0441\u0442\u0432\u0435\u043D\u043D\u0443\u044E \u0432\u0435\u0440\u0441\u0438\u044E, \u0432 \u0442\u043E\u043C \u0447\u0438\u0441\u043B\u0435 \u0441 \u0434\u043E\u0431\u0430\u0432\u043B\u0435\u043D\u043D\u044B\u043C \u0432\u0440\u0435\u0434\u043E\u043D\u043E\u0441\u043D\u044B\u043C \u043A\u043E\u0434\u043E\u043C."));
		Dummy({ 0, H::Draw.Scale(8) });
		TextUnformatted(fnText(
			u8"If you paid for Unibox or downloaded a pre-built DLL from anywhere other than the official GitHub repository, there\u2019s a chance it may have been modified, so you should treat it as potentially unsafe.",
			u8"\u0415\u0441\u043B\u0438 \u0432\u044B \u0437\u0430\u043F\u043B\u0430\u0442\u0438\u043B\u0438 \u0437\u0430 Unibox \u0438\u043B\u0438 \u0441\u043A\u0430\u0447\u0430\u043B\u0438 \u0433\u043E\u0442\u043E\u0432\u0443\u044E DLL \u043E\u0442\u043A\u0443\u0434\u0430-\u043B\u0438\u0431\u043E, \u043A\u0440\u043E\u043C\u0435 \u043E\u0444\u0438\u0446\u0438\u0430\u043B\u044C\u043D\u043E\u0433\u043E \u0440\u0435\u043F\u043E\u0437\u0438\u0442\u043E\u0440\u0438\u044F GitHub, \u043E\u043D\u0430 \u043C\u043E\u0433\u043B\u0430 \u0431\u044B\u0442\u044C \u0438\u0437\u043C\u0435\u043D\u0435\u043D\u0430, \u043F\u043E\u044D\u0442\u043E\u043C\u0443 \u0441\u0447\u0438\u0442\u0430\u0439\u0442\u0435 \u0435\u0451 \u043F\u043E\u0442\u0435\u043D\u0446\u0438\u0430\u043B\u044C\u043D\u043E \u043D\u0435\u0431\u0435\u0437\u043E\u043F\u0430\u0441\u043D\u043E\u0439."));
		PopStyleColor();
		Dummy({ 0, H::Draw.Scale(14) });

		PushFont(F::Render.FontSmall);
		PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
		TextUnformatted(fnText(u8"Official repository:", u8"\u041E\u0444\u0438\u0446\u0438\u0430\u043B\u044C\u043D\u044B\u0439 \u0440\u0435\u043F\u043E\u0437\u0438\u0442\u043E\u0440\u0438\u0439:"));
		PopStyleColor();
		PopFont();
		Dummy({ 0, H::Draw.Scale(2) });
		PushStyleColor(ImGuiCol_Text, F::Render.Accent.Value);
		TextUnformatted(sRepo);
		PopStyleColor();
		if (IsItemHovered())
		{
			SetMouseCursor(ImGuiMouseCursor_Hand);
			GetWindowDrawList()->AddLine({ GetItemRectMin().x, GetItemRectMax().y }, GetItemRectMax(), F::Render.Accent, H::Draw.Scale());
			if (IsMouseReleased(ImGuiMouseButton_Left))
				ShellExecuteA(NULL, "open", sRepo, NULL, NULL, SW_SHOWNORMAL);
		}
		PopTextWrapPos();

		Dummy({ 0, H::Draw.Scale(16) });
		if (FButton(fnText(u8"ok i understand.", u8"\u043E\u043A, \u044F \u043F\u043E\u043D\u0438\u043C\u0430\u044E."), FButtonEnum::Primary))
		{
			std::ofstream{ GetNoticePath() };
			m_bNoticePending = false;
			CloseCurrentPopup();
		}

		FEndPopupModal();
	}
}

void CMenu::Render()
{
	using namespace ImGui;

	if (!(GetIO().DisplaySize.x > 160.f && GetIO().DisplaySize.y > 28.f))
		return;

	m_bInKeybind = false;
	if (m_bIsOpen)
	{
		for (short iKey = 1; iKey < 255; iKey++)
			U::KeyHandler.StoreKey(iKey);
	}
	else
	{
		U::KeyHandler.StoreKey(Vars::Menu::PrimaryKey.Value);
		U::KeyHandler.StoreKey(Vars::Menu::SecondaryKey.Value);
	}
	if (U::KeyHandler.Pressed(Vars::Menu::PrimaryKey.Value) || U::KeyHandler.Pressed(Vars::Menu::SecondaryKey.Value))
		I::MatSystemSurface->SetCursorAlwaysVisible(m_bIsOpen = !m_bIsOpen);

	static bool bNoticeChecked = false;
	if (!bNoticeChecked)
	{
		bNoticeChecked = true;
		std::error_code ec;
		const ImVec2 vDisplaySize = GetIO().DisplaySize;
		if (m_bNoticePending = vDisplaySize.x >= 800.f && vDisplaySize.y >= 700.f && !std::filesystem::exists(GetNoticePath(), ec))
			I::MatSystemSurface->SetCursorAlwaysVisible(m_bIsOpen = true);
	}

	PushFont(F::Render.FontRegular);

	ProcessDeferredNotifications();

	if (m_bIsOpen)
	{
		DrawNotifications();
		ManageVars();

		AddDraggable("Ticks", Vars::Menu::TicksDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::Ticks);
		AddDraggable("Crit hack", Vars::Menu::CritsDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::CritHack);
		AddDraggable("Ping", Vars::Menu::PingDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::Ping);
		AddDraggable("Conditions", Vars::Menu::ConditionsDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::Conditions);
		AddDraggable("Seed prediction", Vars::Menu::SeedPredictionDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::SeedPrediction);
		AddDraggable("Nav bot", Vars::Menu::NavBotDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::NavBot);
		AddResizableDraggable("Camera", Vars::Visuals::Simulation::ProjectileWindow, FGet(Vars::Visuals::Simulation::ProjectileCamera), OptionalConstraints);

		DrawMenu();
		if (m_bNoticePending)
			DrawNotice();

		F::Render.Cursor = GetMouseCursor();
		m_bWindowHovered = IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

		if (!DisabledVec.empty())
		{
			IM_ASSERT_USER_ERROR(0, "Calling PopDisabled() too little times: stack overflow.");
			Disabled = false;
			DisabledVec.clear();
		}
		if (!TransparentVec.empty())
		{
			IM_ASSERT_USER_ERROR(0, "Calling PopTransparent() too little times: stack overflow.");
			Transparent = false;
			TransparentVec.clear();
		}
	}
	else
	{
		ActiveMap.clear();
		m_bWindowHovered = false;
	}

	if (!SDK::TakingScreenshot())
	{
		DrawBinds();
		F::Notifications.Draw();
	}
	PopFont();
}

void CMenu::AddOutput(const char* sFunction, const char* sLog, Color_t tColor)
{
	m_vOutput.push_front({ sFunction, sLog, FNV1A::Hash32Const(sLog), tColor });
	if (m_vOutput.size() > m_iMaxOutputSize)
		m_vOutput.pop_back();
}

void CMenu::ShowNotification(const char* sTitle, const char* sMessage)
{
	Notification_t notification;
	notification.m_sTitle = sTitle;
	notification.m_sMessage = sMessage;
	notification.m_bVisible = true;

	m_vNotifications.push_back(notification);
}

void CMenu::ShowDeferredNotification(const char* sTitle, const char* sMessage)
{
	Notification_t notification;
	notification.m_sTitle = sTitle;
	notification.m_sMessage = sMessage;
	notification.m_bVisible = true;

	m_vDeferredNotifications.push_back(notification);
}

void CMenu::ProcessDeferredNotifications()
{
	if (m_vDeferredNotifications.empty())
		return;

	for (auto& notification : m_vDeferredNotifications)
	{
		m_vNotifications.push_back(notification);
	}

	m_vDeferredNotifications.clear();
}

void CMenu::DrawNotifications()
{
	using namespace ImGui;

	if (m_vNotifications.empty())
		return;

	const ImVec2 vDisplaySize = GetIO().DisplaySize;
	const float flNotificationWidth = H::Draw.Scale(400);
	const float flPosX = floorf((vDisplaySize.x - flNotificationWidth) * 0.5f);
	float flPosY = H::Draw.Scale(20);

	for (size_t i = 0; i < m_vNotifications.size(); ++i)
	{
		auto& notification = m_vNotifications[i];
		if (!notification.m_bVisible)
			continue;

		PushStyleVar(ImGuiStyleVar_WindowRounding, H::Draw.Scale(8));
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(16), H::Draw.Scale(14) });
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), H::Draw.Scale(6) });
		PushStyleColor(ImGuiCol_WindowBg, F::Render.Popup.Value);

		SetNextWindowPos({ flPosX, flPosY }, ImGuiCond_Always);
		SetNextWindowSize({ flNotificationWidth, 0 }, ImGuiCond_Always);

		char sWindowName[64];
		snprintf(sWindowName, sizeof(sWindowName), "##Notification%zu", i);

		if (Begin(sWindowName, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
		{
			PushStyleColor(ImGuiCol_Text, F::Render.Accent.Value);
			PushFont(F::Render.IconFont);
			TextUnformatted(ICON_MD_WARNING);
			PopFont();
			PopStyleColor();

			SameLine(0, H::Draw.Scale(8));
			PushFont(F::Render.FontBold);
			TextUnformatted(notification.m_sTitle.c_str());
			PopFont();

			PushStyleColor(ImGuiCol_Text, MixColor(F::Render.Inactive, F::Render.Active, 0.75f));
			TextWrapped("%s", notification.m_sMessage.c_str());
			PopStyleColor();

			flPosY += GetWindowHeight() + H::Draw.Scale(10);
		}
		End();

		PopStyleColor();
		PopStyleVar(3);
	}
}
#endif
