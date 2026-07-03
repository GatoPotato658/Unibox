#include "Menu.h"

#ifndef TEXTMODE

#include "Components.h"
#include "../Notifications/Notifications.h"
#include "../../Configs/Configs.h"
#include "../../Binds/Binds.h"
#include "../../Visuals/Groups/Groups.h"
#include "../../Players/PlayerUtils.h"
#include "../../Players/SteamProfileCache.h"
#include "../../Spectate/Spectate.h"
#include "../../Resolver/Resolver.h"
#include "../../Visuals/Visuals.h"
#include "../../Misc/Misc.h"
#include "../../Output/Output.h"
#include "../../World/World.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"

#include <wrl/client.h>

struct CachedAvatar_t
{
	Microsoft::WRL::ComPtr<IDirect3DTexture9> pTexture;
	double flNextAttempt = 0.0;
	bool bLoggedCreateFailure = false;
	bool bLoggedCreateSuccess = false;
};

static std::unordered_map<uint32_t, CachedAvatar_t> g_mAvatarTextures;

static void ResetAvatarTexture(uint32_t uAccountID)
{
	if (!uAccountID)
		return;
	g_mAvatarTextures.erase(uAccountID);
}

static ImTextureID GetAvatarTexture(uint32_t uAccountID)
{
	if (!uAccountID)
		return static_cast<ImTextureID>(0);

	auto* pDevice = F::Render.GetDevice();
	if (!pDevice)
		return static_cast<ImTextureID>(0);

	auto& tCache = g_mAvatarTextures[uAccountID];
	if (tCache.pTexture)
		return reinterpret_cast<ImTextureID>(tCache.pTexture.Get());

	const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
	constexpr Color_t tLogColor = { 175, 150, 255, 255 };
	constexpr Color_t tErrorColor = { 255, 150, 150, 255 };

	auto CreateTexture = [&](const uint8_t* pData, uint32_t uWidth, uint32_t uHeight) -> Microsoft::WRL::ComPtr<IDirect3DTexture9>
		{
			if (!pData || !uWidth || !uHeight)
				return {};

			auto LogFailure = [&](const char* sStage, HRESULT hr, D3DPOOL pool)
				{
					if (tCache.bLoggedCreateFailure)
						return;
					tCache.bLoggedCreateFailure = true;
					const std::string sMessage = std::format("Failed to {} D3D texture for avatar {} (pool={}, hr=0x{:08X}).",
															 sStage,
															 uSteamID64,
															 pool == D3DPOOL_MANAGED ? "MANAGED" : "DEFAULT",
															 static_cast<uint32_t>(hr));
					SDK::Output("steamwebapi", sMessage.c_str(), tErrorColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
				};

			auto TryCreate = [&](D3DPOOL pool, DWORD usage) -> Microsoft::WRL::ComPtr<IDirect3DTexture9>
				{
					Microsoft::WRL::ComPtr<IDirect3DTexture9> pTexture;
					const HRESULT hrCreate = pDevice->CreateTexture(uWidth, uHeight, 1, usage, D3DFMT_A8R8G8B8, pool, &pTexture, nullptr);
					if (FAILED(hrCreate))
					{
						LogFailure("create", hrCreate, pool);
						return {};
					}

					D3DLOCKED_RECT tRect = {};
					const DWORD lockFlags = (usage & D3DUSAGE_DYNAMIC) ? D3DLOCK_DISCARD : 0;
					const HRESULT hrLock = pTexture->LockRect(0, &tRect, nullptr, lockFlags);
					if (FAILED(hrLock))
					{
						LogFailure("lock", hrLock, pool);
						return {};
					}

					uint8_t* pDst = static_cast<uint8_t*>(tRect.pBits);
					for (uint32_t y = 0; y < uHeight; y++)
					{
						auto* pRowDst = pDst + y * static_cast<uint32_t>(tRect.Pitch);
						std::memcpy(pRowDst, pData + y * uWidth * 4, uWidth * 4);
					}
					pTexture->UnlockRect(0);
					return pTexture;
				};

			if (auto pManaged = TryCreate(D3DPOOL_MANAGED, 0))
			{
				tCache.bLoggedCreateFailure = false;
				return pManaged;
			}
			if (auto pDefault = TryCreate(D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC))
			{
				tCache.bLoggedCreateFailure = false;
				return pDefault;
			}
			return {};
		};

	auto StoreTexture = [&](const uint8_t* pData, uint32_t uWidth, uint32_t uHeight) -> ImTextureID
		{
			if (auto pTexture = CreateTexture(pData, uWidth, uHeight))
			{
				tCache.pTexture = pTexture;
				tCache.flNextAttempt = 0.0;
				if (!tCache.bLoggedCreateSuccess)
				{
					const std::string sMessage = std::format("ImGui now displaying avatar for {} ({}x{}).", uSteamID64, uWidth, uHeight);
					SDK::Output("steamwebapi", sMessage.c_str(), tLogColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
					tCache.bLoggedCreateSuccess = true;
				}
				return reinterpret_cast<ImTextureID>(tCache.pTexture.Get());
			}
			tCache.bLoggedCreateSuccess = false;
			return static_cast<ImTextureID>(0);
		};

	// Always consume any finished remote download immediately, even if we're in the retry cooldown.
	CSteamProfileCache::AvatarImage_t tImage;
	if (F::SteamProfileCache.TryGetAvatarImage(uAccountID, tImage) && tImage.HasData())
	{
		if (ImTextureID pRemoteTex = StoreTexture(tImage.m_pPixels->data(), tImage.m_uWidth, tImage.m_uHeight))
			return pRemoteTex;
	}

	const double flNow = SDK::PlatFloatTime();
	if (flNow < tCache.flNextAttempt)
		return static_cast<ImTextureID>(0);

	if (I::SteamFriends && I::SteamUtils)
	{
		const CSteamID steamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual);
		const int nAvatar = I::SteamFriends->GetMediumFriendAvatar(steamID);
		uint32_t uWidth = 0, uHeight = 0;
		if (nAvatar && I::SteamUtils->GetImageSize(nAvatar, &uWidth, &uHeight) && uWidth && uHeight)
		{
			std::vector<uint8_t> vRgba(static_cast<size_t>(uWidth) * static_cast<size_t>(uHeight) * 4);
			if (I::SteamUtils->GetImageRGBA(nAvatar, vRgba.data(), static_cast<int>(vRgba.size())))
			{
				std::vector<uint8_t> vBgra(vRgba.size());
				for (uint32_t y = 0; y < uHeight; y++)
				{
					for (uint32_t x = 0; x < uWidth; x++)
					{
						const size_t uIndex = (static_cast<size_t>(y) * uWidth + x) * 4;
						vBgra[uIndex + 0] = vRgba[uIndex + 2];
						vBgra[uIndex + 1] = vRgba[uIndex + 1];
						vBgra[uIndex + 2] = vRgba[uIndex + 0];
						vBgra[uIndex + 3] = vRgba[uIndex + 3];
					}
				}
				if (ImTextureID pFriendTex = StoreTexture(vBgra.data(), uWidth, uHeight))
					return pFriendTex;
			}
		}
	}

	tCache.flNextAttempt = flNow + 5.0;
	return static_cast<ImTextureID>(0);
}

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

	std::string sPreview = "";
	for (int iRole : vRoles)
	{
		if (auto pTag = F::PlayerUtils.GetTag(iRole))
			sPreview += std::format("{}, ", pTag->m_sName);
	}
	if (sPreview.length() > 1)
	{
		sPreview.pop_back();
		sPreview.pop_back();
	}
	else
		sPreview = "All";

	bool bReturn = false;
	const ImVec2 vSize = { GetWindowWidth() - GetStyle().WindowPadding.x * 2.f, H::Draw.Scale(40) };
	const ImVec2 vOriginalPos = GetCursorPos();
	DebugShift({ 0, GetStyle().WindowPadding.y });
	PushStyleVar(ImGuiStyleVar_FramePadding, { 0.f, H::Draw.Scale(13.5f) });
	PushItemWidth(vSize.x);

	bool bActive = BeginCombo(std::format("##{}", sLabel).c_str(), "", ImGuiComboFlags_CustomPreview | ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge);
	if (bActive)
	{
		DebugDummy({ 0, H::Draw.Scale(8) });
		PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, H::Draw.Scale(19) });
		for (int i = 0; i < vEntries.size(); i++)
		{
			const int iRole = vValidRoles[i];
			const bool bActiveRole = std::ranges::find(vRoles, iRole) != vRoles.end();
			ImVec2 vEntryPos = GetCursorPos();
			if (FSelectable(std::format("##{}{}", vEntries[i], i).c_str(), nullptr, 0, bActiveRole, ImGuiSelectableFlags_DontClosePopups))
			{
				if (bActiveRole)
					std::erase(vRoles, iRole);
				else
					vRoles.push_back(iRole);
				bReturn = true;
			}

			ImVec2 vNextPos = GetCursorPos();
			SetCursorPos(vEntryPos + ImVec2(H::Draw.Scale(40), 0));
			TextColored(bActiveRole ? F::Render.Active : F::Render.Inactive, vEntries[i]);
			SameLine();
			DebugDummy({ H::Draw.Scale(!GetCurrentWindow()->ScrollbarY ? 16 : 9), 0 });
			FCheckboxIcon(GetDrawPos() + vEntryPos + ImVec2(H::Draw.Scale(18), H::Draw.Scale(3)), bActiveRole);
			SetCursorPos(vNextPos);
		}
		PopStyleVar();
		SetCursorPosY(GetCursorPosY() - H::Draw.Scale(10));
		DebugDummy({});
		EndCombo();
	}
	if (!Disabled && IsItemHovered())
		SetMouseCursor(ImGuiMouseCursor_Hand);
	if (BeginComboPreview())
	{
		ImVec2 vPreviewPos = GetCursorPos();
		SetCursorPos(vPreviewPos + ImVec2(H::Draw.Scale(12), H::Draw.Scale(-6)));
		PushFont(F::Render.FontSmall);
		TextColored(F::Render.Inactive, TruncateText(StripDoubleHash(sLabel), vSize.x - H::Draw.Scale(45)).c_str());
		PopFont();
		SetCursorPos(vPreviewPos + ImVec2(H::Draw.Scale(12), H::Draw.Scale(8)));
		TextUnformatted(TruncateText(sPreview, vSize.x - H::Draw.Scale(45)).c_str());
		SetCursorPos(vPreviewPos + ImVec2(vSize.x - H::Draw.Scale(24), H::Draw.Scale(-1)));
		IconImage(bActive ? ICON_MD_KEYBOARD_ARROW_UP : ICON_MD_KEYBOARD_ARROW_DOWN);
		EndComboPreview();
	}

	PopItemWidth();
	PopStyleVar();
	SetCursorPos(vOriginalPos);
	AddRowSize(vOriginalPos, vSize + ImVec2(0, GetStyle().WindowPadding.y));
	DebugDummy({ vSize.x, GetRowSize(vSize.y + GetStyle().WindowPadding.y) });
	return bReturn;
}

void CMenu::DrawMenu()
{
	using namespace ImGui;

	static bool bSetPosition = false;
	static bool bDraggingMenu = false;
	static ImVec2 vMenuTargetPos = {};
	static ImVec2 vMenuAnimPos = {};
	static ImVec2 vMenuDragOffset = {};
	ImVec2 vDefaultMenuSize = { H::Draw.Scale(750), H::Draw.Scale(500) };
	if (!bSetPosition)
	{
		vMenuTargetPos = (GetIO().DisplaySize - vDefaultMenuSize) / 2;
		vMenuAnimPos = vMenuTargetPos;
		bSetPosition = true;
	}
	float flDragDelta = std::clamp(GetIO().DeltaTime * 18.f, 0.f, 1.f);
	vMenuAnimPos = ImLerp(vMenuAnimPos, vMenuTargetPos, flDragDelta);
	SetNextWindowPos(vMenuAnimPos, ImGuiCond_Always);
	SetNextWindowSize(vDefaultMenuSize, ImGuiCond_FirstUseEver);

	PushStyleVar(ImGuiStyleVar_WindowMinSize, { H::Draw.Scale(750), H::Draw.Scale(500) });
	if (Begin("Main", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove))
	{
		ImVec2 vWindowSize = GetWindowSize();
		ImVec2 vDrawPos = GetDrawPos();
		ImVec2 vMousePos = GetMousePos();
		auto pDrawList = GetWindowDrawList();
		float flInset = H::Draw.Scale();
		float flNavHeight = H::Draw.Scale(42);
		float flSubTabHeight = H::Draw.Scale(34);
		const std::vector<std::vector<const char*>> vSubTabs = {
			{ "GENERAL", "DRAW" },
			{ "ESP", "MISC##", "MENU" },
			{ "MAIN" },
			{ "MAIN", "BOT", "QUEUEING" },
			{ "CHEATERS", "DETECTION" },
			{ "PLAYERLIST", "SETTINGS##", "OUTPUT" },
			{ "CONFIG", "BINDS", "MATERIALS", "MISC##" }
		};
		static int iTab = 0, iAimbotTab = 0, iVisualsTab = 0, iHvHTab = 0, iMiscTab = 0, iAnticheatTab = 0, iLogsTab = 0, iSettingsTab = 0;
		bool bHasSubTabs = !vSubTabs[iTab].empty();
		float flHeaderHeight = flNavHeight + (bHasSubTabs ? flSubTabHeight : 0.f);
		float flBrandWidth = H::Draw.Scale(140);
		float flSearchWidth = H::Draw.Scale(150);

		Bind_t tBind;
		if (!F::Binds.GetBind(CurrentBind, &tBind))
			CurrentBind = DEFAULT_BIND;

		pDrawList->AddRectFilled(vDrawPos, vDrawPos + ImVec2(vWindowSize.x, vWindowSize.y), F::Render.Background1, H::Draw.Scale(4));
		pDrawList->AddRectFilled(vDrawPos, vDrawPos + ImVec2(vWindowSize.x, flNavHeight), F::Render.Background0, H::Draw.Scale(4), ImDrawFlags_RoundCornersTop);
		pDrawList->AddRectFilled(vDrawPos + ImVec2(0, flNavHeight - flInset), vDrawPos + ImVec2(vWindowSize.x, flNavHeight), F::Render.Background2);
		pDrawList->AddRect(vDrawPos + ImVec2(flInset, flInset), vDrawPos + vWindowSize - ImVec2(flInset, flInset), F::Render.Background2, H::Draw.Scale(4), ImDrawFlags_None, H::Draw.Scale());

		if (CurrentBind != DEFAULT_BIND)
		{
			SetCursorPos({ H::Draw.Scale(12), H::Draw.Scale(6) });
			FText("editing bind", {}, 0, F::Render.FontRegular);
			SetCursorPos({ H::Draw.Scale(12), H::Draw.Scale(22) });
			PushStyleColor(ImGuiCol_Text, F::Render.Accent.Value);
			FText(TruncateText(tBind.m_sName, flBrandWidth - H::Draw.Scale(46)).c_str(), {}, 0, F::Render.FontRegular);
			PopStyleColor();

			SetCursorPos({ flBrandWidth - H::Draw.Scale(31), H::Draw.Scale(6) });
			if (IconButton(ICON_MD_CANCEL))
				CurrentBind = DEFAULT_BIND;
		}
		else if (!Vars::Menu::CheatTitle.Value.empty())
		{
			SetCursorPos({ H::Draw.Scale(12), H::Draw.Scale(13) });
			PushStyleColor(ImGuiCol_Text, F::Render.Accent.Value);
			FText(TruncateText(Vars::Menu::CheatTitle.Value, flBrandWidth - H::Draw.Scale(24), F::Render.FontBold).c_str(), {}, 0, F::Render.FontBold);
			PopStyleColor();
		}

		PushFont(F::Render.FontBold);
		FTabs(
			{
				{ "AIMBOT" },
				{ "VISUALS" },
				{ "HVH" },
				{ "MISC" },
				{ "ANTICHEAT" },
				{ "LOGS" },
				{ "SETTINGS" }
			},
			{ &iTab },
			{ H::Draw.Scale(28), H::Draw.Scale(34) },
			{ flBrandWidth, H::Draw.Scale(4) },
			FTabsEnum::Horizontal | FTabsEnum::HorizontalIcons | FTabsEnum::AlignLeft | FTabsEnum::BarBottom | FTabsEnum::Fit,
			{ { ICON_MD_PERSON }, { ICON_MD_VISIBILITY }, { ICON_MD_SECURITY }, { ICON_MD_ARTICLE }, { ICON_MD_GPP_MAYBE }, { ICON_MD_IMPORT_CONTACTS }, { ICON_MD_SETTINGS } },
			{ H::Draw.Scale(10), 0 }, {},
			{}, {}, H::Draw.Scale(8), 0.f
		);
		PopFont();

		std::vector<int*> vSubVars = { &iAimbotTab, &iVisualsTab, &iHvHTab, &iMiscTab, &iAnticheatTab, &iLogsTab, &iSettingsTab };
		if (bHasSubTabs)
		{
			PushFont(F::Render.FontBold);
			FTabs(
				vSubTabs[iTab],
				vSubVars[iTab],
				{ H::Draw.Scale(24), H::Draw.Scale(24) },
				{ H::Draw.Scale(18), flNavHeight + H::Draw.Scale(5) },
				FTabsEnum::Horizontal | FTabsEnum::AlignLeft | FTabsEnum::BarBottom | FTabsEnum::Fit,
				{}, { H::Draw.Scale(8), 0 }, {}, {}, {}, H::Draw.Scale(16), 0.f
			);
			PopFont();
		}

		static std::string sSearch = "";
		SetCursorPos({ vWindowSize.x - flSearchWidth - H::Draw.Scale(8), H::Draw.Scale(5) });
		FInputText("Search...", sSearch, flSearchWidth, ImGuiInputTextFlags_None);
		bool bSearch = !sSearch.empty();
		if (!bSearch || FCalcTextSize(sSearch.c_str()).x < flSearchWidth - H::Draw.Scale(37))
		{
			SetCursorPos({ vWindowSize.x - H::Draw.Scale(31), H::Draw.Scale(11) });
			IconImage(ICON_MD_SEARCH);
		}
		if (bSearch && IsMouseReleased(ImGuiMouseButton_Left) && IsMouseWithin(vDrawPos.x, vDrawPos.y + flHeaderHeight, vWindowSize.x, vWindowSize.y - flHeaderHeight))
			sSearch = "";

		if (!IsMouseDown(ImGuiMouseButton_Left))
			bDraggingMenu = false;
		if (bDraggingMenu)
			vMenuTargetPos = vMousePos - vMenuDragOffset;
		bool bCanDragMenu = IsMouseWithin(vDrawPos.x, vDrawPos.y, vWindowSize.x, flNavHeight) && !IsAnyItemHovered() && !IsAnyItemActive() && !IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
		if (bCanDragMenu && IsMouseClicked(ImGuiMouseButton_Left))
		{
			bDraggingMenu = true;
			vMenuDragOffset = vMousePos - vMenuTargetPos;
		}

		PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(8), H::Draw.Scale(8) });
		SetCursorPos({ 0, flHeaderHeight });
		if (BeginChild("Page", { vWindowSize.x, vWindowSize.y - flHeaderHeight }, ImGuiChildFlags_AlwaysUseWindowPadding))
		{
			if (!bSearch)
			{
				switch (iTab)
				{
				case 0: MenuAimbot(iAimbotTab); break;
				case 1: MenuVisuals(iVisualsTab >= 1 ? iVisualsTab + 1 : iVisualsTab); break;
				case 2: MenuHvH(iHvHTab); break;
				case 3: MenuMisc(iMiscTab); break;
				case 4: MenuAnticheat(iAnticheatTab); break;
				case 5: MenuLogs(iLogsTab); break;
				case 6: MenuSettings(iSettingsTab); break;
				}
			}
			else
				MenuSearch(sSearch);
		} EndChild();
		PopStyleVar(2);

		End();
	}
	PopStyleVar();
}

#pragma region Tabs
void CMenu::MenuAimbot(int iTab)
{
	using namespace ImGui;

	switch (iTab)
	{
	// General
	case 0:
		{
		if (BeginTable("AimbotTable", 2))
		{
			/* Column 1 */
			TableNextColumn();
			{
				if (Section("General"))
				{
					FDropdown(Vars::Aimbot::General::AimType, FDropdownEnum::Left);
					FDropdown(Vars::Aimbot::General::TargetSelection, FDropdownEnum::Right);
					FDropdown(Vars::Aimbot::General::Target, FDropdownEnum::Left);
					FDropdown(Vars::Aimbot::General::Ignore, FDropdownEnum::Right);
					FDropdown(Vars::Aimbot::General::SmoothCurve, FDropdownEnum::Left);
					FDropdown(Vars::Aimbot::General::BypassIgnore, FDropdownEnum::Right);
					FSlider(Vars::Aimbot::General::AimFOV, FSliderEnum::Left);
					FSlider(Vars::Aimbot::General::MaxTargets, FSliderEnum::Right);
					FSlider(Vars::Aimbot::General::SmoothCurveAmount, FSliderEnum::Left);
					FSlider(Vars::Aimbot::General::AssistStrength, FSliderEnum::Right);
					PushTransparent(!(Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invisible));
					{
						FSlider(Vars::Aimbot::General::IgnoreInvisible, FSliderEnum::Left);
					}
					PopTransparent();
					PushTransparent(!(Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated));
					{
						FSlider(Vars::Aimbot::General::TickTolerance, FSliderEnum::Right);
					}
					PopTransparent();
					PushTransparent(!Vars::Aimbot::General::FOVCircle.Value);
					{
						FColorPicker(Vars::Colors::FOVCircle);
					}
					PopTransparent();
					FToggle(Vars::Aimbot::General::AutoShoot, FToggleEnum::Left);
					FToggle(Vars::Aimbot::General::FOVCircle, FToggleEnum::Right);
					FToggle(Vars::Aimbot::General::PrioritizeNavbot, FToggleEnum::Left);
					FToggle(Vars::Aimbot::General::PrioritizeFollowbot, FToggleEnum::Right);

					Divider();
					FText("Crithack");
					Divider();
					FToggle(Vars::CritHack::ForceCrits, FToggleEnum::Left);
					FToggle(Vars::CritHack::AvoidRandomCrits, FToggleEnum::Right);
					FToggle(Vars::CritHack::AlwaysMeleeCrit, FToggleEnum::Left);
					FToggle(Vars::CritHack::CritEffects, FToggleEnum::Right);

					Divider();
					FText("Misc");
					Divider();
					FToggle(Vars::Aimbot::General::LeadAndRestrict, FToggleEnum::Left);
					FToggle(Vars::Aimbot::General::NoSpread);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Aimbot"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }, -8))
						{
							FDropdown(Vars::Aimbot::General::AimHoldsFire);
							FSlider(Vars::Aimbot::General::NoSpreadOffset);
							FSlider(Vars::Aimbot::General::NoSpreadAverage);
							FSlider(Vars::Aimbot::General::NoSpreadInterval);
							FSlider(Vars::Aimbot::General::NoSpreadBackupInterval);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Backtrack", 8))
				{
					FSlider(Vars::Backtrack::Latency, FSliderEnum::Left);
					FSlider(Vars::Backtrack::Interp, FSliderEnum::Right);
					FSlider(Vars::Backtrack::Window);
					//FToggle(Vars::Backtrack::PreferOnShot, FToggleEnum::Right);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Backtrack"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }))
						{
							FSlider(Vars::Backtrack::Offset);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Healing", 8))
				{
					FDropdown(Vars::Aimbot::Healing::HealPriority, FDropdownEnum::Left);
					FDropdown(Vars::Aimbot::Healing::DangerIgnore, FDropdownEnum::Right);
					FToggle(Vars::Aimbot::Healing::AutoHeal, FToggleEnum::Left);
					FToggle(Vars::Aimbot::Healing::AutoArrow, FToggleEnum::Right);
					FToggle(Vars::Aimbot::Healing::AutoSandvich, FToggleEnum::Left);
					FToggle(Vars::Aimbot::Healing::AutoVaccinator, FToggleEnum::Right);
					FToggle(Vars::Aimbot::Healing::ActivateOnVoice, FToggleEnum::Left);
					FToggle(Vars::Aimbot::Healing::ActivateFriendsOnly, FToggleEnum::Right);
					FSlider(Vars::Aimbot::Healing::ActivationHealthPercent, FSliderEnum::None, Vars::Aimbot::Healing::ActivationHealthPercent[DEFAULT_BIND] <= 0.f ? "Off" : "%g%%");

					PushTransparent(!Vars::Aimbot::Healing::AutoArrow.Value);
					{
						FToggleSlider(Vars::Aimbot::Healing::AutoSwitch, Vars::Aimbot::Healing::AutoSwitchHealth);
						FTooltip("Automatically switch to crossbow and heal if target is below the selected health percent and has resistance or invulnerability or medigun does not have enough charges.");
					}
					PopTransparent();
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Healing"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }))
						{
							FSlider(Vars::Aimbot::Healing::AutoVaccinatorBulletScale);
							FSlider(Vars::Aimbot::Healing::AutoVaccinatorBlastScale);
							FSlider(Vars::Aimbot::Healing::AutoVaccinatorFireScale);
							FToggle(Vars::Aimbot::Healing::AutoVaccinatorFlamethrowerDamageOnly);

							EndPopup();
						}
					} EndSection();
				}
			}

			/* Column 2 */
			TableNextColumn();
			{
				if (Section("Hitscan"))
				{
					FDropdown(Vars::Aimbot::Hitscan::Hitboxes, FDropdownEnum::Left);
					FDropdown(Vars::Aimbot::Hitscan::MultipointHitboxes, FDropdownEnum::Right);
					FSlider(Vars::Aimbot::Hitscan::MultipointScale);
					FDropdown(Vars::Aimbot::Hitscan::Modifiers);
					PushTransparent(!(Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::Tapfire));
					{
						FSlider(Vars::Aimbot::Hitscan::TapfireDistance);
					}
					PopTransparent();
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Hitscan"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }, -8))
						{
							FDropdown(Vars::Aimbot::Hitscan::PeekCheck, FDropdownEnum::None, 0, &Hovered); FTooltip("This should stay as doubletap only or off if you want to be able to target hitboxes other than the highest priority one", Hovered);
							FSlider(Vars::Aimbot::Hitscan::PeekAmount);
							FSlider(Vars::Aimbot::Hitscan::BoneSizeSubtract);
							FSlider(Vars::Aimbot::Hitscan::BoneSizeMinimumScale);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Projectile", 8))
				{
					FDropdown(Vars::Aimbot::Projectile::StrafePrediction, FDropdownEnum::Left);
					FDropdown(Vars::Aimbot::Projectile::SplashPrediction, FDropdownEnum::Right);
					FDropdown(Vars::Aimbot::Projectile::Hitboxes, FDropdownEnum::Left);
					FDropdown(Vars::Aimbot::Projectile::Modifiers, FDropdownEnum::Right);
					FSlider(Vars::Aimbot::Projectile::MaxSimulationTime, FSliderEnum::Left);
					PushTransparent(!Vars::Aimbot::Projectile::StrafePrediction.Value);
					{
						FSlider(Vars::Aimbot::Projectile::HitChance, FSliderEnum::Right);
					}
					PopTransparent();
					FDropdown(Vars::Aimbot::Projectile::AutoDetonate, FDropdownEnum::Left);
					FDropdown(Vars::Aimbot::Projectile::AutoAirblast, FDropdownEnum::Right);
					FSlider(Vars::Aimbot::Projectile::AutodetRadius, FSliderEnum::Left);
					FSlider(Vars::Aimbot::Projectile::SplashRadius, FSliderEnum::Right);
					PushTransparent(!Vars::Aimbot::Projectile::AutoRelease.Value);
					{
						FSlider(Vars::Aimbot::Projectile::AutoRelease);
					}
					PopTransparent();
					FToggle(Vars::Aimbot::Projectile::GrapplingHookAim);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Projectile"))
					{
						FText("General", { 5, 5 });
						if (FPopupButton("General", { 0, -5 }))
						{
							FSlider(Vars::Aimbot::Projectile::VelocityAverageCount, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::VerticalShift, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::DragOverride, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::TimeOverride, FSliderEnum::Right);
							FToggle(Vars::Aimbot::Projectile::LobAnglesUnderpredict);

							Divider();
							FSlider(Vars::Aimbot::Projectile::HuntsmanLerp, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::HuntsmanLerpLow, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::HuntsmanAdd, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::HuntsmanAddLow, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::HuntsmanClamp, FSliderEnum::Left);
							FToggle(Vars::Aimbot::Projectile::HuntsmanPullPoint, FSliderEnum::Left);
							FToggle(Vars::Aimbot::Projectile::HuntsmanPullNoZ, FToggleEnum::Right);

							EndPopup();
						}

						FText("Splash", { 5, 5 });
						if (FPopupButton("Splash", { 0, -5 }, -8))
						{
							FDropdown(Vars::Aimbot::Projectile::SplashMode);
							PushTransparent(Vars::Aimbot::Projectile::SplashMode.Value != Vars::Aimbot::Projectile::SplashModeEnum::Trace);
							{
								FSlider(Vars::Aimbot::Projectile::SplashPointsDirect, FSliderEnum::Left);
								FSlider(Vars::Aimbot::Projectile::SplashPointsArc, FSliderEnum::Right);
								FSlider(Vars::Aimbot::Projectile::SplashRotateX, FSliderEnum::Left, Vars::Aimbot::Projectile::SplashRotateX[DEFAULT_BIND] < 0.f ? "random" : "%g");
								FSlider(Vars::Aimbot::Projectile::SplashRotateY, FSliderEnum::Right, Vars::Aimbot::Projectile::SplashRotateY[DEFAULT_BIND] < 0.f ? "random" : "%g");
							}
							PopTransparent();
							PushTransparent(Vars::Aimbot::Projectile::SplashMode.Value != Vars::Aimbot::Projectile::SplashModeEnum::Face);
							{
								FSlider(Vars::Aimbot::Projectile::SplashDensityDirect, FSliderEnum::Left);
								FSlider(Vars::Aimbot::Projectile::SplashDensityArc, FSliderEnum::Right);
								FSlider(Vars::Aimbot::Projectile::SplashSamplesCutoff);
							}
							PopTransparent();
							FSlider(Vars::Aimbot::Projectile::SplashAirCount, FSliderEnum::None, !Vars::Aimbot::Projectile::SplashAirCount[DEFAULT_BIND] ? "random" : "%i");

							Divider();
							FSlider(Vars::Aimbot::Projectile::SplashRestrictDirect);
							FSlider(Vars::Aimbot::Projectile::SplashRestrictArc);
							FSlider(Vars::Aimbot::Projectile::SplashRestrictFirst);

							Divider();
							FSlider(Vars::Aimbot::Projectile::DirectTraceInterval);
							FSlider(Vars::Aimbot::Projectile::SplashTraceInterval);
							FSlider(Vars::Aimbot::Projectile::LobTraceInterval);
							FToggle(Vars::Aimbot::Projectile::IntervalRetest);

							EndPopup();
						}

						FText("Ground", { 5, 5 });
						if (FPopupButton("Ground", { 0, -5 }))
						{
							FSlider(Vars::Aimbot::Projectile::GroundSamples, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::GroundStraightFuzzyValue, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::GroundLowMinimumSamples, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::GroundHighMinimumSamples, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::GroundLowMinimumDistance, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::GroundHighMinimumDistance, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::GroundMaxChanges, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::GroundMaxChangeTime, FSliderEnum::Right);

							EndPopup();
						}

						FText("Air", { 5, 5 });
						if (FPopupButton("Air", { 0, -5 }))
						{
							FSlider(Vars::Aimbot::Projectile::AirSamples, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::AirStraightFuzzyValue, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::AirLowMinimumSamples, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::AirHighMinimumSamples, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::AirLowMinimumDistance, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::AirHighMinimumDistance, FSliderEnum::Right);
							FSlider(Vars::Aimbot::Projectile::AirMaxChanges, FSliderEnum::Left);
							FSlider(Vars::Aimbot::Projectile::AirMaxChangeTime, FSliderEnum::Right);

							EndPopup();
						}

						FText("Misc", { 5, 5 });
						if (FPopupButton("Misc", { 0, -5 }))
						{
							FSlider(Vars::Aimbot::Projectile::DeltaCount, FSliderEnum::Left);
							FDropdown(Vars::Aimbot::Projectile::DeltaMode, FDropdownEnum::Right);
							FDropdown(Vars::Aimbot::Projectile::MovesimFrictionFlags);
							FToggle(Vars::Aimbot::Projectile::AutodetAccountPing);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Melee", 8))
				{
					FToggle(Vars::Aimbot::Melee::AutoBackstab, FToggleEnum::Left);
					FToggle(Vars::Aimbot::Melee::IgnoreRazorback, FToggleEnum::Right);
					FToggle(Vars::Aimbot::Melee::SwingPrediction, FToggleEnum::Left);
					FToggle(Vars::Aimbot::Melee::WhipTeam, FToggleEnum::Right);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Melee"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }))
						{
							FSlider(Vars::Aimbot::Melee::SwingTicks, FSliderEnum::Left);
							FToggle(Vars::Aimbot::Melee::SwingPredictLag, FToggleEnum::Right);
							FDropdown(Vars::Aimbot::Melee::SwingValidateMode, FDropdownEnum::Left);
							FDropdown(Vars::Aimbot::Melee::BackstabFlags, FDropdownEnum::Right);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Auto Engie", 8))
				{
					FDropdown(Vars::Aimbot::AutoEngie::AutoRepair);
					FDropdown(Vars::Aimbot::AutoEngie::AutoUpgrade);
					FSlider(Vars::Aimbot::AutoEngie::AutoUpgradeSentryLVL);
					FSlider(Vars::Aimbot::AutoEngie::AutoUpgradeDispenserLVL);
					FSlider(Vars::Aimbot::AutoEngie::AutoUpgradeTeleporterLVL);
				} EndSection();
			}
			EndTable();
		}
		break;
	}
	case 1:
	{
		MenuVisuals(1);
		break;
	}
	}
}

void CMenu::MenuVisuals(int iTab)
{
	using namespace ImGui;

	switch (iTab)
	{
	// ESP
	case 0:
	{
		// fake angle/viewmodel visuals, pickup timers?
		static size_t iCurrentGroup = 0;

		if (Section("Groups"))
		{
			static std::string sStaticName;

			PushDisabled(F::Groups.m_vGroups.size() >= sizeof(int) * 8); // for active groups flags
			{
				FSDropdown("Name", &sStaticName, {}, FDropdownEnum::Left | FSDropdownEnum::AutoUpdate, -H::Draw.Unscale(FCalcTextSize("CREATE").x) - 36);

				PushDisabled(Disabled || sStaticName.empty());
				{
					if (FButton("Create", FButtonEnum::Fit | FButtonEnum::SameLine, { 0, 40 }))
					{
						F::Groups.m_vGroups.emplace_back(sStaticName);
						sStaticName.clear();

						iCurrentGroup = F::Groups.m_vGroups.size() - 1;
					}
				}
				PopDisabled();
			}
			PopDisabled();

			FDropdown(Vars::ESP::ActiveGroups, FDropdownEnum::Right | FDropdownEnum::Multi);

			PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
			SetCursorPos({ H::Draw.Scale(13), H::Draw.Scale(80) });
			FText("Groups");
			SetCursorPosY(GetCursorPosY() - H::Draw.Scale(5));
			PopStyleColor();

			auto fPositionToIndex = [](ImVec2 vPos)
			{
				int iIndex = floorf((vPos.y - GetCursorPosY() - H::Draw.Scale(4)) / H::Draw.Scale(36)) * 2
					+ (vPos.x > GetWindowWidth() / 2 ? 1 : 0);
				iIndex = std::clamp(iIndex, 0, int(F::Groups.m_vGroups.size() - 1));
				return iIndex;
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

			for (auto it = F::Groups.m_vGroups.begin(); it < F::Groups.m_vGroups.end();)
			{
				int iGroup = std::distance(F::Groups.m_vGroups.begin(), it);
				auto& tGroup = *it;

				ImVec2 vOriginalPos = !(iGroup % 2)
					? ImVec2(GetStyle().WindowPadding.x, GetCursorPosY() + H::Draw.Scale(8))
					: ImVec2(GetWindowWidth() / 2 + GetStyle().WindowPadding.x / 2, GetCursorPosY() - H::Draw.Scale(28));

				// background
				float flWidth = GetWindowWidth() / 2 - GetStyle().WindowPadding.x * 1.5f;
				float flHeight = H::Draw.Scale(28);
				ImColor tColor = ColorByteToFloat(tGroup.m_tColor.Lerp(Vars::Menu::Theme::Background.Value, 0.5f, LerpEnum::NoAlpha));
				ImVec2 vDrawPos = GetDrawPos() + vOriginalPos;
				if (iCurrentGroup != iGroup)
					GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + ImVec2(flWidth, flHeight), tColor, H::Draw.Scale(4));
				else
				{
					ImColor tColor2 = { tColor.Value.x * 1.1f, tColor.Value.y * 1.1f, tColor.Value.z * 1.1f, tColor.Value.w };
					GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + ImVec2(flWidth, flHeight), tColor2, H::Draw.Scale(4));

					tColor2 = ColorByteToFloat(tGroup.m_tColor.Lerp(Vars::Menu::Theme::Background.Value, 0.25f, LerpEnum::NoAlpha));
					float flInset = H::Draw.Scale(0.5f) - 0.5f;
					GetWindowDrawList()->AddRect(vDrawPos + ImVec2(flInset, flInset), vDrawPos + ImVec2(flWidth - flInset, flHeight - flInset), tColor2, H::Draw.Scale(4), ImDrawFlags_None, H::Draw.Scale());
				}

				// text + icons
				float flTextWidth = flWidth - H::Draw.Scale(36);
				SetCursorPos(vOriginalPos + ImVec2(H::Draw.Scale(9), H::Draw.Scale(7)));
				PushTransparent(!(Vars::ESP::ActiveGroups.Value & 1 << iGroup), true);
				{
					FText(TruncateText(tGroup.m_sName, flTextWidth).c_str());
				}
				PopTransparent(1, 1);

				SetCursorPos(vOriginalPos + ImVec2(flWidth - H::Draw.Scale(26), H::Draw.Scale(2)));
				bool bDelete = IconButton(ICON_MD_DELETE), bDuplicate = false;

				SetCursorPos(vOriginalPos);
				bool bClicked = Button(std::format("##{}", iGroup).c_str(), { flWidth, flHeight });
				bool bPopup = IsItemClicked(ImGuiMouseButton_Right);

				if (bClicked)
					iCurrentGroup = iGroup;
				else if (bPopup)
					OpenPopup(std::format("RightClicked{}", iGroup).c_str());
				else if (iDragging == -1 && IsItemHovered() && IsMouseDown(ImGuiMouseButton_Left))
					iDragging = iGroup;

				if (FBeginPopup(std::format("RightClicked{}", iGroup).c_str()))
				{
					PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), 0 });

					{
						static std::string sInput = "";

						bool bEnter = FInputText("Name...", sInput, H::Draw.Scale(284), ImGuiInputTextFlags_EnterReturnsTrue);
						if (!IsItemFocused())
							sInput = tGroup.m_sName;
						if (bEnter)
							tGroup.m_sName = sInput;
					}

					PushDisabled(F::Groups.m_vGroups.size() >= sizeof(int) * 8);
					{
						bDuplicate = FButton("Duplicate");
					}
					PopDisabled();

					PopStyleVar();
					EndPopup();
				}

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
		} EndSection();

		if (!F::Groups.m_vGroups.empty()
			&& BeginTable("VisualsESPTable", 2))
		{
			iCurrentGroup = std::clamp(iCurrentGroup, 0ui64, F::Groups.m_vGroups.size() - 1);
			auto& tGroup = F::Groups.m_vGroups[iCurrentGroup];

			/* Column 1 */
			TableNextColumn();
			{
				if (Section("Color", 8))
				{
					FColorPicker("Group color", &tGroup.m_tColor, FColorPickerEnum::Left);
					FToggle("Tags override color", &tGroup.m_bTagsOverrideColor, FToggleEnum::Right);
				} EndSection();
				if (Section("Targets"))
				{
					FDropdown("Targets", &tGroup.m_iTargets, { "Players", "Buildings", "Projectiles", "Ragdolls", "Objective", "NPCs", "Health", "Ammo", "Money", "Powerups", "Spellbook", "Bombs", "Gargoyle", "##Divider", "Fake angle", "Viewmodel weapon", "Viewmodel hands" }, {}, FDropdownEnum::Multi);
				} EndSection();
				if (Section("Conditions"))
				{
					FDropdown("Conditions", &tGroup.m_iConditions, { "Enemy", "Team", "BLU", "RED", "##Divider", "Local", "Friends", "Party", "Priority", "Target", "##Divider", "Dormant" }, {}, FDropdownEnum::Multi);
					Divider(H::Draw.Scale(8), H::Draw.Scale(-1));
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
			}

			/* Column 2 */
			TableNextColumn();
			{
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
						FSlider("Draw start## ESP", &tGroup.m_tESP.Start, 0.f, 2048.f, 128.f, "%.fHU", FSliderEnum::Left | FSliderEnum::Clamp);
						FSlider("Draw end## ESP", &tGroup.m_tESP.End, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Right | FSliderEnum::Min);
						FToggle("Distance to alpha## ESP", &tGroup.m_tESP.SmoothAlpha);
						PushTransparent(Transparent || !(tGroup.m_tESP.Draw & ESPEnum::NameBackground));
						{
							int iOpacity = tGroup.m_tESP.BackgroundOpacity;
							FSlider("Background opacity", &iOpacity, 0, 255, 5, "%i", FSliderEnum::Right | FSliderEnum::Clamp);
							tGroup.m_tESP.BackgroundOpacity = byte(iOpacity); // good
						}
						PopTransparent();
					}
					PopTransparent();
				} EndSection();
				if (Section("Chams"))
				{
					if (!tGroup.m_iTargets || tGroup.m_iTargets & TargetsEnum::Occluded)
					{
						FMDropdown("Visible material", &tGroup.m_tChams.Visible, FDropdownEnum::Left);
						FMDropdown("Occluded material", &tGroup.m_tChams.Occluded, FDropdownEnum::Right);
					}
					else
						FMDropdown("Material", &tGroup.m_tChams.Visible);
				} EndSection();
				if (Section("Glow", 8))
				{
					PushTransparent(!tGroup.m_tGlow.Stencil);
					{
						FSlider("Stencil scale", &tGroup.m_tGlow.Stencil, 0, 10, 1, "%i", FSliderEnum::Left | FSliderEnum::Min);
					}
					PopTransparent();
					PushTransparent(!tGroup.m_tGlow.Blur);
					{
						FSlider("Blur scale", &tGroup.m_tGlow.Blur, 0.f, 10.f, 1.f, "%g", FSliderEnum::Right | FSliderEnum::Min | FSliderEnum::Precision);
					}
					PopTransparent();

					FSlider("Render start## Glow", &tGroup.m_tGlow.Start, 0.f, 2048.f, 128, "%.fHU", FSliderEnum::Left | FSliderEnum::Clamp);
					FSlider("Render end## Glow", &tGroup.m_tGlow.End, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Right | FSliderEnum::Min);
					FToggle("Distance to alpha## Glow", &tGroup.m_tGlow.SmoothAlpha);
				} EndSection();
				if (Section("Misc", 8))
				{
					FToggle("Offscreen arrows", &tGroup.m_bOffscreenArrows, FToggleEnum::Left);
					if (FPopupButton("OffscreenArrows"))
					{
						FSlider("Offset", &tGroup.m_iOffscreenArrowsOffset, 0, 1000, 25, "%i", FSliderEnum::Precision);
						FSlider("Max distance", &tGroup.m_flOffscreenArrowsMaxDistance, 0.f, 5000.f, 50.f, "%g", FSliderEnum::Min | FSliderEnum::Precision);

						EndPopup();
					}

					FToggle("Pickup timer", &tGroup.m_bPickupTimer);

					FToggle("Backtrack", &tGroup.m_iBacktrack, BacktrackEnum::Enabled, FToggleEnum::Left);
					if (FPopupButton("Backtrack", {}, -8))
					{
						FDropdown("##Draw", &tGroup.m_iBacktrack, { "Last", "First", "##Divider", "Always" }, { BacktrackEnum::Last, BacktrackEnum::First, BacktrackEnum::Always }, FDropdownEnum::Multi | FDropdownEnum::NoSanitization, 0, "All");

						FMDropdown("Visible material", &tGroup.m_tBacktrackChams.Visible, FDropdownEnum::Left);
						FMDropdown("Occluded material", &tGroup.m_tBacktrackChams.Occluded, FDropdownEnum::Right);

						SetCursorPosY(GetCursorPosY() + H::Draw.Scale(8));
						PushTransparent(!tGroup.m_tBacktrackGlow.Stencil);
						{
							FSlider("Stencil scale## Backtrack", &tGroup.m_tBacktrackGlow.Stencil, 0, 10, 1, "%i", FSliderEnum::Left | FSliderEnum::Min);
						}
						PopTransparent();
						PushTransparent(!tGroup.m_tBacktrackGlow.Blur);
						{
							FSlider("Blur scale## Backtrack", &tGroup.m_tBacktrackGlow.Blur, 0.f, 10.f, 1.f, "%g", FSliderEnum::Right | FSliderEnum::Min | FSliderEnum::Precision);
						}
						PopTransparent();
						
						FSlider("Render start## Backtrack", &tGroup.m_tBacktrackGlow.Start, 0.f, 2048.f, 128, "%.fHU", FSliderEnum::Left | FSliderEnum::Min);
						FSlider("Render end## Backtrack", &tGroup.m_tBacktrackGlow.End, 512.f, 8192.f, 128.f, "%.fHU", FSliderEnum::Right | FSliderEnum::Min);
						FToggle("Distance to alpha## Backtrack", &tGroup.m_tBacktrackGlow.SmoothAlpha);
						EndPopup();
					}

					FToggle("Trajectory", &tGroup.m_iTrajectory, TrajectoryEnum::Enabled, FToggleEnum::Left);
					if (FPopupButton("Trajectory", {}, -8))
					{
						FDropdown("Flags", &tGroup.m_iTrajectory, { "Predict", "##Divider", "Radius", "Trace", "Sphere", "##Divider", "Path" }, { TrajectoryEnum::Predict, TrajectoryEnum::Radius, TrajectoryEnum::Trace, TrajectoryEnum::Sphere, TrajectoryEnum::Path }, FDropdownEnum::Multi | FDropdownEnum::NoSanitization);
						FToggle("Ignore Z", &tGroup.m_iTrajectory, SightlinesEnum::IgnoreZ);

						EndPopup();
					}

					FToggle("Sightlines", &tGroup.m_iSightlines, SightlinesEnum::Enabled, FToggleEnum::Left);
					if (FPopupButton("Sightlines"))
					{
						FToggle("Ignore Z", &tGroup.m_iSightlines, SightlinesEnum::IgnoreZ);

						EndPopup();
					}
				} EndSection();
			}
			EndTable();
		}
		break;
	}
	// Draw
	case 1:
	{
		if (BeginTable("DrawTable", 2))
		{
			/* Column 1 */
			TableNextColumn();
			{
				if (Section("Line", 8))
				{
					FColorPicker(Vars::Colors::LineIgnoreZ, FColorPickerEnum::None, { 0, H::Draw.Scale(6) }, { H::Draw.Scale(12), H::Draw.Scale(6) });
					FColorPicker(Vars::Colors::Line, FColorPickerEnum::None, {}, { H::Draw.Scale(12), H::Draw.Scale(6) });
					FToggle(Vars::Visuals::Line::TracersEnabled);
					FSlider(Vars::Visuals::Line::DrawDuration);
				} EndSection();
				if (Section("Hitbox"))
				{
					FDropdown(Vars::Visuals::Hitbox::BonesEnabled, FDropdownEnum::None, -20);
					FColorPicker(Vars::Colors::TargetHitboxEdgeIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(30) }, { H::Draw.Scale(10), H::Draw.Scale(10) });
					FColorPicker(Vars::Colors::TargetHitboxEdge, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-10) }, { H::Draw.Scale(10), H::Draw.Scale(10) });
					FColorPicker(Vars::Colors::BoneHitboxEdgeIgnoreZ, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-10) }, { H::Draw.Scale(10), H::Draw.Scale(10) });
					FColorPicker(Vars::Colors::BoneHitboxEdge, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-10) }, { H::Draw.Scale(10), H::Draw.Scale(10) });
					FColorPicker(Vars::Colors::TargetHitboxFaceIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(30) }, { H::Draw.Scale(10), H::Draw.Scale(10) });
					FColorPicker(Vars::Colors::TargetHitboxFace, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-10) }, { H::Draw.Scale(10), H::Draw.Scale(10) });
					FColorPicker(Vars::Colors::BoneHitboxFaceIgnoreZ, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-10) }, { H::Draw.Scale(10), H::Draw.Scale(10) });
					FColorPicker(Vars::Colors::BoneHitboxFace, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-10) }, { H::Draw.Scale(10), H::Draw.Scale(10) });
					SameLine(); DebugDummy({ 2, H::Draw.Scale(48) });

					FDropdown(Vars::Visuals::Hitbox::BoundsEnabled, FDropdownEnum::None, -20);
					FColorPicker(Vars::Colors::BoundHitboxEdgeIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					FColorPicker(Vars::Colors::BoundHitboxEdge, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					FColorPicker(Vars::Colors::BoundHitboxFaceIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(20) }, {H::Draw.Scale(10), H::Draw.Scale(20)});
					FColorPicker(Vars::Colors::BoundHitboxFace, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					SameLine(); DebugDummy({ 0, H::Draw.Scale(48) });

					FSlider(Vars::Visuals::Hitbox::DrawDuration);
				} EndSection();
			}
			/* Column 2 */
			TableNextColumn();
			{
				if (Section("Prediction"))
				{
					FDropdown(Vars::Visuals::Prediction::PlayerPath, FDropdownEnum::Left, -10);
					FColorPicker(Vars::Colors::PlayerPathIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					FColorPicker(Vars::Colors::PlayerPath, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					SameLine(); DebugDummy({ 0, H::Draw.Scale(48) });
					FDropdown(Vars::Visuals::Prediction::ProjectilePath, FDropdownEnum::Right, -10);
					FColorPicker(Vars::Colors::ProjectilePathIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					FColorPicker(Vars::Colors::ProjectilePath, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					SameLine(); DebugDummy({ 0, H::Draw.Scale(48) });
					FToggle(Vars::Visuals::Prediction::SwingLines);
					FSlider(Vars::Visuals::Prediction::PlayerDrawDuration, FSliderEnum::Left, !Vars::Visuals::Prediction::PlayerDrawDuration[DEFAULT_BIND] ? "timed" : "%g");
					FSlider(Vars::Visuals::Prediction::ProjectileDrawDuration, FSliderEnum::Right, !Vars::Visuals::Prediction::ProjectileDrawDuration[DEFAULT_BIND] ? "timed" : "%g");
				} EndSection();
				if (Section("Simulation"))
				{
					FDropdown(Vars::Visuals::Simulation::TrajectoryPath, FDropdownEnum::Left, -10);
					FColorPicker(Vars::Colors::TrajectoryPathIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					FColorPicker(Vars::Colors::TrajectoryPath, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					SameLine(); DebugDummy({ 0, H::Draw.Scale(48) });
					FDropdown(Vars::Visuals::Simulation::ShotPath, FDropdownEnum::Right, -10);
					FColorPicker(Vars::Colors::ShotPathIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					FColorPicker(Vars::Colors::ShotPath, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					SameLine(); DebugDummy({ 0, H::Draw.Scale(48) });
					FDropdown(Vars::Visuals::Simulation::SplashRadius, FDropdownEnum::None, -10);
					FColorPicker(Vars::Colors::SplashRadiusIgnoreZ, FColorPickerEnum::SameLine, { 0, H::Draw.Scale(20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					FColorPicker(Vars::Colors::SplashRadius, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(-20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });
					SameLine(); DebugDummy({ 0, H::Draw.Scale(48) });
					FToggle(Vars::Visuals::Simulation::ProjectileCamera, FToggleEnum::Left);
					FToggle(Vars::Visuals::Simulation::Box, FToggleEnum::Right);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug"))
					{
						FText("Extra", { 5, 5 });
						if (FPopupButton("Extra", { 0, -5 }, -8))
						{
							FDropdown(Vars::Visuals::Prediction::RealPath, FDropdownEnum::None, -10);
							FColorPicker(Vars::Colors::RealPath, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(20) });
							FColorPicker(Vars::Colors::RealPathIgnoreZ, FColorPickerEnum::SameLine, { H::Draw.Scale(-10), H::Draw.Scale(20) }, { H::Draw.Scale(10), H::Draw.Scale(20) });

							FSlider(Vars::Visuals::Path::SeparatorSpacing, FSliderEnum::Left);
							FSlider(Vars::Visuals::Path::SeparatorLength, FSliderEnum::Right);

							EndPopup();
						}

						FText("Simulation", { 5, 5 });
						if (FPopupButton("Simulation", { 0, -5 }))
						{
							FToggle(Vars::Visuals::Trajectory::Override);
							bool bApply = FButton("Apply current");
							FSDropdown(Vars::Visuals::Trajectory::Type);
							FSlider(Vars::Visuals::Trajectory::OffsetX);
							FSlider(Vars::Visuals::Trajectory::OffsetY);
							FSlider(Vars::Visuals::Trajectory::OffsetZ);
							FSlider(Vars::Visuals::Trajectory::ForwardRedirect);
							FSlider(Vars::Visuals::Trajectory::ForwardCutoff);
							FSlider(Vars::Visuals::Trajectory::Hull);
							FSlider(Vars::Visuals::Trajectory::Speed);
							FSlider(Vars::Visuals::Trajectory::Gravity);
							FSlider(Vars::Visuals::Trajectory::LifeTime);
							FSlider(Vars::Visuals::Trajectory::UpVelocity);
							FSlider(Vars::Visuals::Trajectory::AngularVelocityX);
							FSlider(Vars::Visuals::Trajectory::AngularVelocityY);
							FSlider(Vars::Visuals::Trajectory::AngularVelocityZ);
							FSlider(Vars::Visuals::Trajectory::Drag);
							FSlider(Vars::Visuals::Trajectory::DragX);
							FSlider(Vars::Visuals::Trajectory::DragY);
							FSlider(Vars::Visuals::Trajectory::DragZ);
							FSlider(Vars::Visuals::Trajectory::AngularDragX);
							FSlider(Vars::Visuals::Trajectory::AngularDragY);
							FSlider(Vars::Visuals::Trajectory::AngularDragZ);
							FSlider(Vars::Visuals::Trajectory::MaxVelocity);
							FSlider(Vars::Visuals::Trajectory::MaxAngularVelocity);

							if (bApply)
							{
								auto pLocal = H::Entities.GetLocal();
								auto pWeapon = H::Entities.GetWeapon();
								if (pLocal && pWeapon)
								{
									ProjectileInfo tProjInfo = {};
									bool bOriginal = Vars::Visuals::Trajectory::Override.Value;
									Vars::Visuals::Trajectory::Override.Value = false;
									bool bSetup = F::ProjSim.GetInfo(pLocal, pWeapon, {}, tProjInfo, ProjSimEnum::Interp)
											   && F::ProjSim.Initialize(tProjInfo, false);
									Vars::Visuals::Trajectory::Override.Value = bOriginal;
									if (bSetup)
									{
										Vec3 vLocalEye = pLocal->GetEyePosition();
										Vec3 vOffset = tProjInfo.m_vPos - vLocalEye; vOffset.y *= -1;
										float flForwardRedirect = 0.f, flForwardCutoff = 0.f;
										switch (tProjInfo.m_uType)
										{
										case FNV1A::Hash32Const("models/weapons/w_models/w_rocket.mdl"):
										case FNV1A::Hash32Const("models/weapons/w_models/w_drg_ball.mdl"):
										case FNV1A::Hash32Const("models/weapons/w_models/w_flaregun_shell.mdl"):
										case FNV1A::Hash32Const("models/weapons/w_models/w_arrow.mdl"):
										case FNV1A::Hash32Const("models/weapons/w_models/w_syringe_proj.mdl"):
										case FNV1A::Hash32Const("models/weapons/w_models/w_repair_claw.mdl"):
											flForwardRedirect = 2000.f, flForwardCutoff = 0.1f; break;
										case FNV1A::Hash32Const("models/weapons/c_models/c_flameball/c_flameball.mdl"):
											flForwardRedirect = H::ConVars.FindVar("tf_fireball_distance")->GetFloat(), flForwardCutoff = 1.f;
										}
										physics_performanceparams_t params = {}; F::ProjSim.m_pEnv->GetPerformanceSettings(&params);
										Vec3 vVelocity, vAngVelocity; F::ProjSim.m_pObj->GetVelocity(&vVelocity, &vAngVelocity);

										Vars::Visuals::Trajectory::OffsetX[DEFAULT_BIND] = vOffset.x;
										Vars::Visuals::Trajectory::OffsetY[DEFAULT_BIND] = vOffset.y;
										Vars::Visuals::Trajectory::OffsetZ[DEFAULT_BIND] = vOffset.z;
										Vars::Visuals::Trajectory::ForwardRedirect[DEFAULT_BIND] = flForwardRedirect;
										Vars::Visuals::Trajectory::ForwardCutoff[DEFAULT_BIND] = flForwardCutoff;
										Vars::Visuals::Trajectory::Hull[DEFAULT_BIND] = tProjInfo.m_vHull.x;
										Vars::Visuals::Trajectory::Speed[DEFAULT_BIND] = vVelocity.x;
										Vars::Visuals::Trajectory::Gravity[DEFAULT_BIND] = tProjInfo.m_flGravity;
										Vars::Visuals::Trajectory::LifeTime[DEFAULT_BIND] = tProjInfo.m_flLifetime;
										Vars::Visuals::Trajectory::UpVelocity[DEFAULT_BIND] = vVelocity.z;
										Vars::Visuals::Trajectory::AngularVelocityX[DEFAULT_BIND] = vAngVelocity.x;
										Vars::Visuals::Trajectory::AngularVelocityY[DEFAULT_BIND] = vAngVelocity.y;
										Vars::Visuals::Trajectory::AngularVelocityZ[DEFAULT_BIND] = vAngVelocity.z;
										Vars::Visuals::Trajectory::Drag[DEFAULT_BIND] = F::ProjSim.m_pObj->m_dragCoefficient;
										Vars::Visuals::Trajectory::DragX[DEFAULT_BIND] = F::ProjSim.m_pObj->m_dragBasis.x;
										Vars::Visuals::Trajectory::DragY[DEFAULT_BIND] = F::ProjSim.m_pObj->m_dragBasis.y;
										Vars::Visuals::Trajectory::DragZ[DEFAULT_BIND] = F::ProjSim.m_pObj->m_dragBasis.z;
										Vars::Visuals::Trajectory::AngularDragX[DEFAULT_BIND] = F::ProjSim.m_pObj->m_angDragBasis.x;
										Vars::Visuals::Trajectory::AngularDragY[DEFAULT_BIND] = F::ProjSim.m_pObj->m_angDragBasis.y;
										Vars::Visuals::Trajectory::AngularDragZ[DEFAULT_BIND] = F::ProjSim.m_pObj->m_angDragBasis.z;
										Vars::Visuals::Trajectory::MaxVelocity[DEFAULT_BIND] = params.maxVelocity;
										Vars::Visuals::Trajectory::MaxAngularVelocity[DEFAULT_BIND] = params.maxAngularVelocity;
									}
								}
							}

							EndPopup();
						}
					} EndSection();
				}
			}
			EndTable();
		}
		break;
	}
	// Misc
	case 2:
	{
		if (BeginTable("VisualsMiscTable", 2))
		{
			/* Column 1 */
			TableNextColumn();
			{
				if (Section("UI"))
				{
					FDropdown(Vars::Visuals::UI::StreamerMode, FDropdownEnum::Left);
					FDropdown(Vars::Visuals::UI::ChatTags, FDropdownEnum::Right, -10);
					FColorPicker(Vars::Colors::Local, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });
					PushTransparent(!Vars::Visuals::UI::FieldOfView.Value);
					{
						FSlider(Vars::Visuals::UI::FieldOfView);
					}
					PopTransparent();
					PushTransparent(!Vars::Visuals::UI::ZoomFieldOfView.Value);
					{
						FSlider(Vars::Visuals::UI::ZoomFieldOfView);
					}
					PopTransparent();
					PushTransparent(!Vars::Visuals::UI::AspectRatio.Value);
					{
						FSlider(Vars::Visuals::UI::AspectRatio);
					}
					PopTransparent();
					FToggle(Vars::Visuals::UI::RevealScoreboard, FToggleEnum::Left);
					FToggle(Vars::Visuals::UI::ScoreboardUtility, FToggleEnum::Right);
					FToggle(Vars::Visuals::UI::ScoreboardColors, FToggleEnum::Left);
					FToggle(Vars::Visuals::UI::CleanScreenshots, FToggleEnum::Right);
				} EndSection();
				if (Section("Thirdperson", 8))
				{
					FToggle(Vars::Visuals::Thirdperson::Enabled, FToggleEnum::Left);
					FToggle(Vars::Visuals::Thirdperson::Crosshair, FToggleEnum::Right);
					FToggle(Vars::Visuals::Thirdperson::Collision, FToggleEnum::Left);
					FSlider(Vars::Visuals::Thirdperson::Distance);
					FSlider(Vars::Visuals::Thirdperson::Right);
					FSlider(Vars::Visuals::Thirdperson::Up);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Thirdperson"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }))
						{
							FToggle(Vars::Visuals::Thirdperson::Scale, FToggleEnum::Left);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Effects"))
				{
					// https://developer.valvesoftware.com/wiki/Team_Fortress_2/Particles
					// https://forums.alliedmods.net/showthread.php?t=127111
					FSDropdown(Vars::Visuals::Effects::BulletTracer, FDropdownEnum::Left);
					FSDropdown(Vars::Visuals::Effects::CritTracer, FDropdownEnum::Right);
					FSDropdown(Vars::Visuals::Effects::MedigunBeam, FDropdownEnum::Left);
					FSDropdown(Vars::Visuals::Effects::MedigunCharge, FDropdownEnum::Right);
					FSDropdown(Vars::Visuals::Effects::ProjectileTrail, FDropdownEnum::Left);
					FDropdown(Vars::Visuals::Effects::SpellFootsteps, FDropdownEnum::Right, -10);
					FColorPicker(Vars::Colors::SpellFootstep, FColorPickerEnum::SameLine | FColorPickerEnum::NoTooltip, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });
					FDropdown(Vars::Visuals::Effects::RagdollEffects);
					FToggle(Vars::Visuals::Effects::DrawIconsThroughWalls);
					FToggle(Vars::Visuals::Effects::DrawDamageNumbersThroughWalls);
				} EndSection();
			}
			/* Column 2 */
			TableNextColumn();
			{
				if (Section("Removals", 8))
				{
					FToggle(Vars::Visuals::Removals::Interpolation, FToggleEnum::Left);
					PushTransparent(Vars::Visuals::Removals::Interpolation.Value);
					{
						FToggle(Vars::Visuals::Removals::Lerp, FToggleEnum::Right);
					}
					PopTransparent();
					FToggle(Vars::Visuals::Removals::Disguises, FToggleEnum::Left);
					FToggle(Vars::Visuals::Removals::Taunts, FToggleEnum::Right);
					FToggle(Vars::Visuals::Removals::Scope, FToggleEnum::Left);
					FToggle(Vars::Visuals::Removals::PostProcessing, FToggleEnum::Right);
					FToggle(Vars::Visuals::Removals::ScreenOverlays, FToggleEnum::Left);
					FToggle(Vars::Visuals::Removals::ScreenEffects, FToggleEnum::Right);
					FToggle(Vars::Visuals::Removals::ViewPunch, FToggleEnum::Left);
					FToggle(Vars::Visuals::Removals::AngleForcing, FToggleEnum::Right);
					FToggle(Vars::Visuals::Removals::Ragdolls, FToggleEnum::Left);
					FToggle(Vars::Visuals::Removals::Gibs, FToggleEnum::Right);
					FToggle(Vars::Visuals::Removals::MOTD, FToggleEnum::Left);
				} EndSection();
				if (Section("Viewmodel", 8))
				{
					FToggle(Vars::Visuals::Viewmodel::CrosshairAim, FToggleEnum::Left);
					FToggle(Vars::Visuals::Viewmodel::ViewmodelAim, FToggleEnum::Right);
					FSlider(Vars::Visuals::Viewmodel::OffsetX, FSliderEnum::Left);
					FSlider(Vars::Visuals::Viewmodel::Pitch, FSliderEnum::Right);
					FSlider(Vars::Visuals::Viewmodel::OffsetY, FSliderEnum::Left);
					FSlider(Vars::Visuals::Viewmodel::Yaw, FSliderEnum::Right);
					FSlider(Vars::Visuals::Viewmodel::OffsetZ, FSliderEnum::Left);
					FSlider(Vars::Visuals::Viewmodel::Roll, FSliderEnum::Right);
					PushTransparent(!Vars::Visuals::Viewmodel::SwayScale.Value || !Vars::Visuals::Viewmodel::SwayInterp.Value);
					{
						FSlider(Vars::Visuals::Viewmodel::SwayScale, FSliderEnum::Left);
						FSlider(Vars::Visuals::Viewmodel::SwayInterp, FSliderEnum::Right);
					}
					PopTransparent();
				} EndSection();
				if (Section("World"))
				{
					FDropdown(Vars::Visuals::World::Modulations);
					FSDropdown(Vars::Visuals::World::WorldTexture, FDropdownEnum::Left);
					FSDropdown(Vars::Visuals::World::SkyboxChanger, FDropdownEnum::Right);
					PushTransparent(!(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::World));
					{
						FColorPicker(Vars::Colors::WorldModulation, FColorPickerEnum::Left);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Sky));
					{
						FColorPicker(Vars::Colors::SkyModulation, FColorPickerEnum::Right);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Prop));
					{
						FColorPicker(Vars::Colors::PropModulation, FColorPickerEnum::Left);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Particle));
					{
						FColorPicker(Vars::Colors::ParticleModulation, FColorPickerEnum::Right);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Fog));
					{
						FColorPicker(Vars::Colors::FogModulation, FColorPickerEnum::Left);
					}
					PopTransparent();
					FToggle(Vars::Visuals::World::NearPropFade, FToggleEnum::Left);
					FToggle(Vars::Visuals::World::NoPropFade, FToggleEnum::Right);
					FDropdown(Vars::Visuals::World::ShowTriggers);
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::Hurt));
					{
						FColorPicker(Vars::Colors::HurtTrigger, FColorPickerEnum::Left);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::Ignite));
					{
						FColorPicker(Vars::Colors::IgniteTrigger, FColorPickerEnum::Right);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::Push));
					{
						FColorPicker(Vars::Colors::PushTrigger, FColorPickerEnum::Left);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::Regenerate));
					{
						FColorPicker(Vars::Colors::RegenerateTrigger, FColorPickerEnum::Right);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::RespawnRoom));
					{
						FColorPicker(Vars::Colors::RespawnRoomTrigger, FColorPickerEnum::Left);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::CaptureArea));
					{
						FColorPicker(Vars::Colors::CaptureAreaTrigger, FColorPickerEnum::Right);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::Catapult));
					{
						FColorPicker(Vars::Colors::CatapultTrigger, FColorPickerEnum::Left);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::ApplyImpulse));
					{
						FColorPicker(Vars::Colors::ApplyImpulseTrigger, FColorPickerEnum::Right);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::ShowAngles));
					{
						FColorPicker(Vars::Colors::TriggerAngle, FColorPickerEnum::Left);
					}
					PopTransparent();
					PushTransparent(!(Vars::Visuals::World::ShowTriggers.Value & Vars::Visuals::World::ShowTriggersEnum::ShowSurfaceCenters));
					{
						FColorPicker(Vars::Colors::TriggerSurfaceCenter, FColorPickerEnum::Right);
					}
					PopTransparent();
				} EndSection();
				
				if (Section("Other"))
				{
					FToggle(Vars::Visuals::Other::KillstreakWeapons);
				} EndSection();
			}
			EndTable();
		}
		break;
	}
	// Menu
	case 3:
	{
		if (BeginTable("MenuTable", 2))
		{
			/* Column 1 */
			TableNextColumn();
			{
				if (Section("Settings", 8))
				{
					FColorPicker(Vars::Menu::Theme::Accent, FColorPickerEnum::Left);
					FColorPicker(Vars::Menu::Theme::Background, FColorPickerEnum::Right);
					FColorPicker(Vars::Menu::Theme::Active, FColorPickerEnum::Left);
					FColorPicker(Vars::Menu::Theme::Inactive, FColorPickerEnum::Right);

					FSDropdown(Vars::Menu::CheatTitle, FDropdownEnum::Left);
					FSDropdown(Vars::Menu::CheatTag, FDropdownEnum::Right);
					FKeybind(Vars::Menu::PrimaryKey, FButtonEnum::Left, { Vars::Menu::SecondaryKey[DEFAULT_BIND], VK_LBUTTON, VK_RBUTTON });
					FKeybind(Vars::Menu::SecondaryKey, FButtonEnum::Right | FButtonEnum::SameLine, { Vars::Menu::PrimaryKey[DEFAULT_BIND], VK_LBUTTON, VK_RBUTTON });
				} EndSection();
			}

			/* Column 2 */
			TableNextColumn();
			{
				if (Section("Indicators"))
				{
					FDropdown(Vars::Menu::Indicators);
					if (FSlider(Vars::Menu::Scale))
						H::Fonts.Reload();
					if (FToggle(Vars::Menu::CheapText))
						H::Fonts.Reload();
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }))
						{
							FColorPicker(Vars::Colors::IndicatorGood, FColorPickerEnum::Left);
							FColorPicker(Vars::Colors::IndicatorTextGood, FColorPickerEnum::Right);
							FColorPicker(Vars::Colors::IndicatorBad, FColorPickerEnum::Left);
							FColorPicker(Vars::Colors::IndicatorTextBad, FColorPickerEnum::Right);
							FColorPicker(Vars::Colors::IndicatorMid, FColorPickerEnum::Left);
							FColorPicker(Vars::Colors::IndicatorTextMid, FColorPickerEnum::Right);
							FColorPicker(Vars::Colors::IndicatorMisc, FColorPickerEnum::Left);
							FColorPicker(Vars::Colors::IndicatorTextMisc, FColorPickerEnum::Right);

							EndPopup();
						}
					}
					EndSection();
				}
			}
			EndTable();
		}
		break;
	}
	}
}

void CMenu::MenuHvH(int iTab)
{
	using namespace ImGui;

	switch (iTab)
	{
	// Main
	case 0:
	{
		if (BeginTable("HvHTable", 2))
		{
			/* Column 1 */
			TableNextColumn();
			{
				if (Section("Doubletap", 8))
				{
					FToggle(Vars::Doubletap::Doubletap, FToggleEnum::Left);
					FToggle(Vars::Doubletap::Warp, FToggleEnum::Right);
					FToggle(Vars::Doubletap::RechargeTicks, FToggleEnum::Left);
					FToggle(Vars::Doubletap::AntiWarp, FToggleEnum::Right);
					FSlider(Vars::Doubletap::TickLimit, FSliderEnum::Left);
					FSlider(Vars::Doubletap::WarpRate, FSliderEnum::Right);
					FSlider(Vars::Doubletap::RechargeLimit, FSliderEnum::Left);
					FSlider(Vars::Doubletap::PassiveRecharge, FSliderEnum::Right);
				} EndSection();
				if (Section("Fakelag"))
				{
					FDropdown(Vars::Fakelag::Fakelag, FSliderEnum::Left);
					FDropdown(Vars::Fakelag::Options, FDropdownEnum::Right);
					PushTransparent(Vars::Fakelag::Fakelag.Value != Vars::Fakelag::FakelagEnum::Plain);
					{
						FSlider(Vars::Fakelag::PlainTicks, FSliderEnum::Left);
					}
					PopTransparent();
					PushTransparent(Vars::Fakelag::Fakelag.Value != Vars::Fakelag::FakelagEnum::Random);
					{
						FSlider(Vars::Fakelag::RandomTicks, FSliderEnum::Right);
					}
					PopTransparent();
					FToggle(Vars::Fakelag::UnchokeOnAttack, FToggleEnum::Left);
					FToggle(Vars::Fakelag::RetainBlastJump, FToggleEnum::Right);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Antiaim"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }))
						{
							FToggle(Vars::AntiAim::AntiAimLines);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Antiaim", 8))
				{
					FToggle(Vars::AntiAim::Enabled);
					FDropdown(Vars::AntiAim::PitchReal, FDropdownEnum::Left);
					FDropdown(Vars::AntiAim::PitchFake, FDropdownEnum::Right);
					FDropdown(Vars::AntiAim::YawReal, FDropdownEnum::Left);
					FDropdown(Vars::AntiAim::YawFake, FDropdownEnum::Right);
					FDropdown(Vars::AntiAim::RealYawBase, FDropdownEnum::Left);
					FDropdown(Vars::AntiAim::FakeYawBase, FDropdownEnum::Right);
					FSlider(Vars::AntiAim::RealYawOffset, FSliderEnum::Left);
					FSlider(Vars::AntiAim::FakeYawOffset, FSliderEnum::Right);
					PushTransparent(Vars::AntiAim::YawReal.Value != Vars::AntiAim::YawEnum::Edge && Vars::AntiAim::YawReal.Value != Vars::AntiAim::YawEnum::Jitter);
					{
						FSlider(Vars::AntiAim::RealYawValue, FSliderEnum::Left);
					}
					PopTransparent();
					PushTransparent(Vars::AntiAim::YawFake.Value != Vars::AntiAim::YawEnum::Edge && Vars::AntiAim::YawFake.Value != Vars::AntiAim::YawEnum::Jitter);
					{
						FSlider(Vars::AntiAim::FakeYawValue, FSliderEnum::Right);
					}
					PopTransparent();
					PushTransparent(Vars::AntiAim::YawFake.Value != Vars::AntiAim::YawEnum::Spin
						&& Vars::AntiAim::YawFake.Value != Vars::AntiAim::YawEnum::Tornado
						&& Vars::AntiAim::YawFake.Value != Vars::AntiAim::YawEnum::Helix
						&& Vars::AntiAim::YawReal.Value != Vars::AntiAim::YawEnum::Spin
						&& Vars::AntiAim::YawReal.Value != Vars::AntiAim::YawEnum::Tornado
						&& Vars::AntiAim::YawReal.Value != Vars::AntiAim::YawEnum::Helix);
					{
						FSlider(Vars::AntiAim::SpinSpeed, FSliderEnum::Left);
					}
					PopTransparent();
					SetCursorPos({ GetWindowWidth() / 2 + GetStyle().WindowPadding.x / 2, GetRowPos() + H::Draw.Scale(8) });
					FToggle(Vars::AntiAim::MinWalk, FToggleEnum::Left);
					FToggle(Vars::AntiAim::AntiOverlap, FToggleEnum::Left);
					FToggle(Vars::AntiAim::HidePitchOnShot, FToggleEnum::Right);
					FToggle(Vars::AntiAim::TauntSpin);
				} EndSection();
			}
			/* Column 2 */
			TableNextColumn();
			{
				if (Section("Resolver", 8))
				{
					FToggle(Vars::Resolver::Enabled, FToggleEnum::Left);
					PushTransparent(!Vars::Resolver::Enabled.Value);
					{
						FToggle(Vars::Resolver::AutoResolve, FToggleEnum::Right);
						PushTransparent(Transparent || !Vars::Resolver::AutoResolve.Value);
						{
							FToggle(Vars::Resolver::AutoResolveCheatersOnly, FToggleEnum::Left);
							FToggle(Vars::Resolver::AutoResolveHeadshotOnly, FToggleEnum::Right);
							PushTransparent(Transparent || !Vars::Resolver::AutoResolveYawAmount.Value);
							{
								FSlider(Vars::Resolver::AutoResolveYawAmount, FSliderEnum::Left);
							}
							PopTransparent();
							PushTransparent(Transparent || !Vars::Resolver::AutoResolvePitchAmount.Value);
							{
								FSlider(Vars::Resolver::AutoResolvePitchAmount, FSliderEnum::Right);
							}
							PopTransparent();
						}
						PopTransparent();
						FSlider(Vars::Resolver::CycleYaw, FSliderEnum::Left);
						FSlider(Vars::Resolver::CyclePitch, FSliderEnum::Right);
						FToggle(Vars::Resolver::CycleView, FToggleEnum::Left);
						FToggle(Vars::Resolver::CycleMinwalk, FToggleEnum::Right);
					}
					PopTransparent();
				} EndSection();
				if (Section("Auto Peek", 8))
				{
					FToggle(Vars::AutoPeek::Enabled);
				} EndSection();
				if (Section("Speedhack", 8))
				{
					PushTransparent(Vars::Speedhack::Scale.Value == 1);
					{
						FSlider(Vars::Speedhack::Scale);
					}
					PopTransparent();
				} EndSection();
			}
			EndTable();
		}
		break;
	}
	}
}

void CMenu::MenuMisc(int iTab)
{
	using namespace ImGui;

	switch (iTab)
	{
	// Main
	case 0:
	{
		if (BeginTable("MiscTable", 2))
		{
			/* Column 1 */
			TableNextColumn();
			{
				if (Section("Movement"))
				{
					FDropdown(Vars::Misc::Movement::AutoStrafe);
					PushTransparent(Vars::Misc::Movement::AutoStrafe.Value != Vars::Misc::Movement::AutoStrafeEnum::Directional);
					{
						FSlider(Vars::Misc::Movement::AutoStrafeTurnScale, FSliderEnum::Left);
						FSlider(Vars::Misc::Movement::AutoStrafeMaxDelta, FSliderEnum::Right);
					}
					PopTransparent();
					
					FDropdown(Vars::Misc::Movement::AutoEdgebug, FDropdownEnum::None, -10);
					FColorPicker(Vars::Colors::EdgebugPath, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });
					FToggle(Vars::Misc::Movement::Bunnyhop, FToggleEnum::Left);
					FToggle(Vars::Misc::Movement::EdgeJump, FToggleEnum::Right);
					FToggle(Vars::Misc::Movement::AutoJumpbug, FToggleEnum::Left); // this is unreliable without setups, do not depend on it!
					FToggle(Vars::Misc::Movement::BreakJump, FToggleEnum::Right);
					FToggle(Vars::Misc::Movement::AutoRocketJump, FToggleEnum::Left);
					FToggle(Vars::Misc::Movement::AutoCTap, FToggleEnum::Right);
					FToggle(Vars::Misc::Movement::AutoFaNJump, FToggleEnum::Left);
					FToggle(Vars::Misc::Movement::AutoRevJump, FToggleEnum::Right);
					FToggle(Vars::Misc::Movement::FastStop, FToggleEnum::Left);
					FToggle(Vars::Misc::Movement::FastAccelerate, FToggleEnum::Right);
					FToggle(Vars::Misc::Movement::DuckSpeed, FToggleEnum::Left);
					FToggle(Vars::Misc::Movement::DuckSpeedNavbotCompat, FToggleEnum::Right);
					FToggle(Vars::Misc::Movement::DuckSpeedForward, FToggleEnum::Left);
					FToggle(Vars::Misc::Movement::MovementLock, FToggleEnum::Right);
					FToggle(Vars::Misc::Movement::ShieldTurnRate, FToggleEnum::Left);
					FToggle(Vars::Misc::Movement::NoPush, FToggleEnum::Right);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Movement"))
					{
						FText("Debug rocket jump", { 5, 5 });
						if (FPopupButton("Debug rocket jump", { 0, -5 }))
						{
							FSlider(Vars::Misc::Movement::AutoRocketJumpChokeGrounded, FToggleEnum::Left);
							FSlider(Vars::Misc::Movement::AutoRocketJumpChokeAir, FToggleEnum::Right);
							FSlider(Vars::Misc::Movement::AutoRocketJumpSkipGround, FToggleEnum::Left);
							FSlider(Vars::Misc::Movement::AutoRocketJumpSkipAir, FToggleEnum::Right);
							FSlider(Vars::Misc::Movement::AutoRocketJumpTimingOffset, FToggleEnum::Left);
							FSlider(Vars::Misc::Movement::AutoRocketJumpApplyAbove, FToggleEnum::Right);
							
							EndPopup();
						}
						
						FText("Debug FaN jump", { 5, 5 });
						if (FPopupButton("Debug FaN jump", { 0, -5 }))
						{
							FSlider(Vars::Misc::Movement::AutoFaNJumpOnSolidTicks, FSliderEnum::Left);
							FToggle(Vars::Misc::Movement::AutoFaNJumpCheckCeiling, FToggleEnum::Right);
							
							EndPopup();
						}
						
						FText("Debug edgebug", { 5, 5 });
						if (FPopupButton("Debug edgebug", { 0, -5 }))
						{
							FSlider(Vars::Misc::Movement::AutoEdgebugStrafeSamples, FSliderEnum::Left);
							FSlider(Vars::Misc::Movement::AutoEdgebugStrafeMaxDelta, FSliderEnum::Right);
							FToggle(Vars::Misc::Movement::AutoEdgebugTryNegativeDir, FToggleEnum::Left);
							FToggle(Vars::Misc::Movement::AutoEdgebugTryRandomMove, FToggleEnum::Right);
							
							EndPopup();
						}
					} EndSection();
				}
				if (Section("Exploits"))
				{
					FToggle(Vars::Misc::Exploits::PureBypass, FToggleEnum::Left);
					FToggle(Vars::Misc::Exploits::CheatsBypass, FToggleEnum::Right);
					FToggle(Vars::Misc::Exploits::UnlockCVars, FToggleEnum::Left);
					FToggle(Vars::Misc::Exploits::EquipRegionUnlock, FToggleEnum::Right);
					FToggle(Vars::Misc::Exploits::BackpackExpander, FToggleEnum::Left);
					FToggle(Vars::Misc::Exploits::BreakShootSound, FToggleEnum::Right);
					FTooltip("breaks weapon shoot sound by switching weapons (soldier only)");
					FToggleSlider(Vars::Misc::Exploits::PingReducer, Vars::Misc::Exploits::PingTarget);
				} EndSection();
				if (Section("Mann vs. Machine", 14))
				{
					FToggle(Vars::Misc::MannVsMachine::InstantRespawn, FToggleEnum::Left);
					FToggle(Vars::Misc::MannVsMachine::InstantRevive, FToggleEnum::Right);
					FToggle(Vars::Misc::MannVsMachine::AllowInspect, FToggleEnum::Left);
					FToggle(Vars::Misc::MannVsMachine::AutoMvmReadyUp, FToggleEnum::Right);
					FToggle(Vars::Misc::MannVsMachine::AutoAbandonMannUp, FToggleEnum::Left);
					FToggleSlider(Vars::Misc::MannVsMachine::BuyBot, Vars::Misc::MannVsMachine::MaxCash);
					FToggle(Vars::Misc::MannVsMachine::BuyBotAutoClass, FToggleEnum::Left);
					PushTransparent(!Vars::Misc::MannVsMachine::BuyBotAutoClass.Value);
					{
						FDropdown(Vars::Misc::MannVsMachine::BuyBotClass, { "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Sniper", "Spy" }, { 1, 3, 7, 4, 6, 9, 2, 8 }, FDropdownEnum::Right);
					}
					PopTransparent();
					FTooltip("WARNING: Works only on Mann Up missions with enough starting cash (600$) before the 1st wave!\nRequirements:\n1. Be a Vaccinator Medic\n2. Ping must be below 80ms\n3. Walk to the upgrade station\nPerforms MVM upgrade station exploit for extra cash.");
				} EndSection();
			}

			/* Column 2 */
			TableNextColumn();
			{
				if (Section("Automation"))
				{
					FDropdown(Vars::Misc::Automation::AntiBackstab); // pitch/fake _might_ slip up some auto backstabs
					FToggle(Vars::Misc::Automation::AcceptItemDrops);
					FToggle(Vars::Misc::Automation::AntiAFK, FToggleEnum::Left);
					FDropdown(Vars::Misc::Automation::AntiAutobalance, FDropdownEnum::Right);
					FToggleSlider(Vars::Misc::Automation::AutoRetry, Vars::Misc::Automation::AutoRetryHealth);
					FToggle(Vars::Misc::Automation::KartControl, FToggleEnum::Left);
					FToggle(Vars::Misc::Automation::AutoReport, FToggleEnum::Right);
					FToggle(Vars::Misc::Automation::AutoDisguise, FToggleEnum::Left);
					FToggle(Vars::Misc::Automation::JoinSpam, FToggleEnum::Left); // i think it doesnt work anymore but dh wanted it so here it is
				} EndSection();
				if (Section("Voting", 8))
				{
					FDropdown(Vars::Misc::Automation::AutoVote);
					PushTransparent(!(Vars::Misc::Automation::AutoVote.Value & Vars::Misc::Automation::AutoVoteEnum::Defend) 
						&& !(Vars::Misc::Automation::AutoVote.Value & Vars::Misc::Automation::AutoVoteEnum::Assist));
					{
						FToggle(Vars::Misc::Automation::AutoVoteDelay, FToggleEnum::Left);
						PushTransparent(!Vars::Misc::Automation::AutoVoteDelay.Value);
						{
							FSlider(Vars::Misc::Automation::AutoVoteDelayInterval, FSliderEnum::Right);
						}
						PopTransparent();
					}
					PopTransparent();
					FToggleSlider(Vars::Misc::Automation::AutoVoteMap, Vars::Misc::Automation::AutoVoteMapOption);
				} EndSection();
				if (Section("Taunts", 8))
				{
					FToggle(Vars::Misc::Automation::TauntControl);
					FToggleSlider(Vars::Misc::Automation::AutoTaunt, Vars::Misc::Automation::AutoTauntChance);
					PushTransparent(!Vars::Misc::Automation::AutoTaunt.Value);
					{
						FSlider(Vars::Misc::Automation::AutoTauntSlot, FSliderEnum::None, Vars::Misc::Automation::AutoTauntSlot.Value == 0 ? "weapon" : "%i");
					}
					PopTransparent();
				} EndSection();
				if (Section("Game", 8))
				{
					FToggle(Vars::Misc::Game::NetworkFix, FToggleEnum::Left);
					FToggle(Vars::Misc::Game::SetupBonesOptimization, FToggleEnum::Right);
					FToggle(Vars::Misc::Game::InsecureBypass, FToggleEnum::Left);
					FToggle(Vars::Misc::Game::AntiCheatCompatibility, FToggleEnum::Right);
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug Anti cheat"))
					{
						FText("Debug", { 5, 5 });
						if (FPopupButton("Debug", { 0, -5 }))
						{
							FToggle(Vars::Misc::Game::AntiCheatCritHack);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Sound"))
				{
					FDropdown(Vars::Misc::Sound::Block);
					FToggle(Vars::Misc::Sound::HitsoundAlways, FToggleEnum::Left);
					FToggle(Vars::Misc::Sound::RemoveDSP, FToggleEnum::Right);
					FToggle(Vars::Misc::Sound::GiantWeaponSounds);
				} EndSection();
			}
			EndTable();
		}
		break;
	}
	// Navbot
	case 1:
	{
		if (BeginTable("NavbotTable", 2))
		{
			/* Column 1 */
			TableNextColumn();
			{
				if (Section("Nav Engine"))
				{
					FToggle(Vars::Misc::Movement::NavEngine::Enabled, FToggleEnum::Left);
					FToggle(Vars::Misc::Movement::NavEngine::PathInSetup, FToggleEnum::Right);
					FToggle(Vars::Misc::Movement::NavBot::SmartJump, FToggleEnum::Left);
					PushTransparent(!Vars::Misc::Movement::NavEngine::Enabled.Value);
					{
						FDropdown(Vars::Misc::Movement::NavEngine::Draw, FDropdownEnum::Multi, -60);
						FColorPicker(Vars::Colors::NavbotPath, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });
						FColorPicker(Vars::Colors::NavbotPossiblePath, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });
						// debug only and it crashes
						// FColorPicker(Vars::Colors::NavbotWalkablePath, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });
						FColorPicker(Vars::Colors::NavbotArea, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });
						FColorPicker(Vars::Colors::NavbotBlacklist, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });

						FSlider(Vars::Misc::Movement::NavEngine::StickyIgnoreTime, FSliderEnum::Left);
						FSlider(Vars::Misc::Movement::NavEngine::StuckDetectTime, FSliderEnum::Right);
						FSlider(Vars::Misc::Movement::NavEngine::StuckBlacklistTime, FSliderEnum::Left);
						FSlider(Vars::Misc::Movement::NavEngine::StuckExpireTime, FSliderEnum::Right);
						FSlider(Vars::Misc::Movement::NavEngine::StuckTime, FSliderEnum::None);
						FToggle(Vars::Misc::Movement::NavEngine::VischeckEnabled);
						FSlider(Vars::Misc::Movement::NavEngine::VischeckTime, FSliderEnum::Left);
						FSlider(Vars::Misc::Movement::NavEngine::VischeckCacheTime, FSliderEnum::Right);
					}
					PopTransparent();
				} EndSection();
				if (Section("Followbot"))
				{
					FToggle(Vars::Misc::Movement::FollowBot::Enabled);
					PushTransparent(!Vars::Misc::Movement::NavEngine::Enabled.Value);
						FDropdown(Vars::Misc::Movement::FollowBot::UseNav, FDropdownEnum::Left);
						FTooltip("Use nav engine when unable to reach current target.\nNormal - runs in case the target is NOT dormant\nNormal + Dormant - runs regardless of dormancy\nNOTE:\n'+ Dormant' does not make the followbot target players across the whole map. \nIt only prevents followbot from losing current target in case it goes dormant");
					PopTransparent();
					FDropdown(Vars::Misc::Movement::FollowBot::Targets, FDropdownEnum::Right);
					FDropdown(Vars::Misc::Movement::FollowBot::Prefer, FDropdownEnum::Left);
					FDropdown(Vars::Misc::Movement::FollowBot::LookAtPath, FDropdownEnum::Right);
					PushTransparent(!Vars::Misc::Movement::FollowBot::LookAtPath.Value);
						FDropdown(Vars::Misc::Movement::FollowBot::LookAtPathMode, FDropdownEnum::Left);
						FTooltip("Look at path mode:\nPath - look at current path node.\nCopy - use saved target viewangles.\nCopy immediate - use current target viewangles.\nAt target - look at current target.");
						FToggle(Vars::Misc::Movement::FollowBot::LookAtPathNoSnap);
					PopTransparent();
					FSlider(Vars::Misc::Movement::FollowBot::MaxNodes, FSliderEnum::Left);
					FTooltip("Allowed amount of path nodes.\nExceeding that abandons the target.");
					FSlider(Vars::Misc::Movement::FollowBot::MinPriority, FSliderEnum::Right);
					FTooltip("Minimal priority at which followbot starts to target a player.\nOverrides playerlist follow priority minimum allowing to change which targets followbot selects depending on this config");
					FSlider(Vars::Misc::Movement::FollowBot::ActivationDistance, FSliderEnum::Left);
					FTooltip("Distance at which followbot starts to follow.\nDoesn't affect anything with nav being active.");
					FSlider(Vars::Misc::Movement::FollowBot::FollowDistance, FSliderEnum::Right);
					FTooltip("Distance to target which followbot tries to maintain.");
					FSlider(Vars::Misc::Movement::FollowBot::AbandonDistance, FSliderEnum::Left);
					FTooltip("Distance at which followbot abandons the target (will try to use nav instead).");
					PushTransparent(!Vars::Misc::Movement::FollowBot::UseNav.Value);
						FSlider(Vars::Misc::Movement::FollowBot::NavAbandonDistance, FSliderEnum::Right);
						FTooltip("Distance at which followbot abandons the target completely.");
					PopTransparent();
					FToggle(Vars::Misc::Movement::FollowBot::DrawPath, FToggleEnum::Left);
					FColorPicker(Vars::Colors::FollowbotPathLine, FColorPickerEnum::SameLine);
					FColorPicker(Vars::Colors::FollowbotPathBox, FColorPickerEnum::SameLine);
				} EndSection();
				if (Section("Bot Utils"))
				{
					PushTransparent(!Vars::Misc::Movement::NavEngine::Enabled.Value);
					{
						FDropdown(Vars::Misc::Movement::NavEngine::LookAtPath);
					}
					PopTransparent();
					PushTransparent(!Vars::Misc::Movement::NavEngine::LookAtPath.Value && !Vars::Misc::Movement::FollowBot::LookAtPath.Value);
					{
						FSlider(Vars::Misc::Movement::BotUtils::LookAtPathSpeed, FSliderEnum::None);
						FTooltip("Specifies how smooth the viewangles will change when using 'Look at path' in nav engine or followbot");
						FToggle(Vars::Misc::Movement::BotUtils::LookAtPathDebug, FToggleEnum::Left);
					}
					PopTransparent();
					FToggle(Vars::Misc::Automation::RandomClass, FToggleEnum::Right);
					PushTransparent(!Vars::Misc::Automation::RandomClass.Value);
					{
						FDropdown(Vars::Misc::Automation::RandomClassExclude, FDropdownEnum::Multi);
						FSlider(Vars::Misc::Automation::RandomClassInterval, FSliderEnum::None);
					}
					PopTransparent();
					FDropdown(Vars::Misc::Automation::ForceClass, { "Off", "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy" }, {0,1,3,7,4,6,9,5,2,8}, FDropdownEnum::Left);
					FDropdown(Vars::Misc::Movement::BotUtils::WeaponSlot, FDropdownEnum::Right);
					FDropdown(Vars::Misc::Movement::BotUtils::AutoScope);
					PushTransparent(!Vars::Misc::Movement::BotUtils::AutoScope.Value);
					{
						FSlider(Vars::Misc::Movement::BotUtils::AutoScopeCancelTime, FSliderEnum::None);
					}
					PopTransparent();
				} EndSection();
				if (Vars::Debug::Options.Value)
				{
					if (Section("##Debug"))
					{
						FText("Debug Bot Utils", { 5, 5 });
						if (FPopupButton("Debug Bot Utils", { 0, -5 }))
						{
							FToggle(Vars::Misc::Movement::BotUtils::AutoScopeUseCachedResults);
							FTooltip("should increase performance of autoscope by only checking every 2nd tick");

							EndPopup();
						}
						
						FText("Debug Navbot", { 5, 5 });
						if (FPopupButton("Debug Navbot", { 0, -5 }))
						{
							FSlider(Vars::Misc::Movement::NavBot::StickyDangerRange);
							FSlider(Vars::Misc::Movement::NavBot::ProjectileDangerRange);

							EndPopup();
						}
					} EndSection();
				}
				if (Section("Navbot"))
				{
					PushTransparent(!Vars::Misc::Movement::NavEngine::Enabled.Value);
					{
						FToggle(Vars::Misc::Movement::NavBot::Enabled, FToggleEnum::Left);
						PushTransparent(!Vars::Misc::Movement::NavBot::Enabled.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value);
						{
							FDropdown(Vars::Misc::Movement::NavBot::RechargeDT);
							PushTransparent(Transparent || !Vars::Misc::Movement::NavBot::RechargeDT.Value);
								FSlider(Vars::Misc::Movement::NavBot::RechargeDTDelay, FSliderEnum::None);
							PopTransparent();
							FDropdown(Vars::Misc::Movement::NavBot::Preferences);
							FDropdown(Vars::Misc::Movement::NavBot::Blacklist);
							PushTransparent(Transparent || !(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::NormalThreats));
							{
								FSlider(Vars::Misc::Movement::NavBot::BlacklistDelay, FSliderEnum::Left);
							}
							PopTransparent();
							PushTransparent(Transparent || !(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::DormantThreats));
							{
								FSlider(Vars::Misc::Movement::NavBot::BlacklistDormantDelay, FSliderEnum::Right);
							}
							PopTransparent();
							PushTransparent(Transparent || !(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::NormalThreats)
											&& !(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::DormantThreats));
							{
								FSlider(Vars::Misc::Movement::NavBot::BlacklistSlightDangerLimit);
							}
							PopTransparent();
							FSlider(Vars::Misc::Movement::NavBot::MeleeTargetRange);
							FToggleSlider(Vars::Misc::Movement::NavBot::DangerOverlay, Vars::Misc::Movement::NavBot::DangerOverlayMaxDist);
						}
						PopTransparent();
					}
					PopTransparent();
				} EndSection();
			}

			/* Column 2 */
			TableNextColumn();
			{
				if (Section("Auto-Item"))
				{
					FDropdown(Vars::Misc::Automation::AutoItem::Enable);
					FTooltip("Allows you to automatically rent and craft items, very useful for bots.");
					FSlider(Vars::Misc::Automation::AutoItem::Interval);
					FSDropdown(Vars::Misc::Automation::AutoItem::Primary, FDropdownEnum::Left);
					FSDropdown(Vars::Misc::Automation::AutoItem::FirstHat, FDropdownEnum::Right);
					FSDropdown(Vars::Misc::Automation::AutoItem::Secondary, FDropdownEnum::Left);
					FSDropdown(Vars::Misc::Automation::AutoItem::SecondHat, FDropdownEnum::Right);
					FSDropdown(Vars::Misc::Automation::AutoItem::Melee, FDropdownEnum::Left);
					FSDropdown(Vars::Misc::Automation::AutoItem::ThirdHat, FDropdownEnum::Right);
				} EndSection();
				if (Section("Chat", 8))
				{
					FToggleSlider(Vars::Misc::Automation::ChatSpam::Enable, Vars::Misc::Automation::ChatSpam::Interval, FSliderEnum::Clamp);
					PushTransparent(!Vars::Misc::Automation::ChatSpam::Enable.Value);
					{
						FToggle(Vars::Misc::Automation::ChatSpam::TeamChat, FToggleEnum::Left);
						FToggle(Vars::Misc::Automation::ChatSpam::Randomize, FToggleEnum::Right);
					}
					PopTransparent();
					FToggle(Vars::Misc::Automation::ChatSpam::AutoReply, FToggleEnum::Left);
					FToggle(Vars::Misc::Automation::ChatSpam::VoteKickReply, FToggleEnum::Left);
					FToggleSlider(Vars::Misc::Automation::KillSay::Enable, Vars::Misc::Automation::KillSay::Chance);
					PushTransparent(!Vars::Misc::Automation::KillSay::Enable.Value);
					{
						FToggle(Vars::Misc::Automation::KillSay::TeamChat);
					}
					PopTransparent();
					FDropdown(Vars::Misc::Automation::VoiceCommandSpam);
					FToggle(Vars::Misc::Automation::Micspam, FToggleEnum::Left);
#if 0
					FToggle(Vars::Misc::Automation::AchievementSpam, FToggleEnum::Right);
					PushTransparent(!Vars::Misc::Automation::AchievementSpam.Value);
					{
						FDropdown(
							Vars::Misc::Automation::AchievementSpamID,
							Vars::Misc::Automation::GetAchievementSpamDropdownEntries(),
							Vars::Misc::Automation::GetAchievementSpamDropdownIDs());
					}
					PopTransparent();
#endif
					FToggle(Vars::Misc::Automation::NoiseSpam, FToggleEnum::Left);
					FToggle(Vars::Misc::Automation::CallVoteSpam, FToggleEnum::Right);
					// if (FButton("HELP", FButtonEnum::Left, { 0, 24 }))
					// 	ShellExecuteA(NULL, "open", (F::Configs.m_sConfigPath + "chathelp.txt").c_str(), NULL, NULL, SW_SHOWNORMAL);
					// if (FButton("OPEN FOLDER", FButtonEnum::Right, { 0, 24 }, 0, nullptr, nullptr))
					// 	ShellExecuteA(NULL, "open", F::Configs.m_sConfigPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
				} EndSection();
			}
			EndTable();
		}
		break;
	}
	case 2:
	{
		if (BeginTable("QueueingTable", 2))
		{
			TableNextColumn();
			{
				if (Section("Matchmaking"))
				{
					FToggle(Vars::Misc::Queueing::AutoCasualQueue, FToggleEnum::Left);
					FToggle(Vars::Misc::Queueing::AutoCasualJoin, FToggleEnum::Right);
					FToggle(Vars::Misc::Queueing::AutoCompetitiveQueue, FToggleEnum::Left);
					FToggle(Vars::Misc::Queueing::AutoMannUpQueue, FToggleEnum::Right);
					FSlider(Vars::Misc::Queueing::QueueDelay);
				} EndSection();
				if (Section("Casual automation", 8))
				{
					FToggle(Vars::Misc::Queueing::MapPopularizing, FToggleEnum::Left);
					FToggle(Vars::Misc::Queueing::MapBarBoost, FToggleEnum::Right);
					PushTransparent(!Vars::Misc::Queueing::AutoCasualQueue.Value);
					{
						FToggle(Vars::Misc::Queueing::AutoAbandonIfNoNavmesh, FToggleEnum::Left);
						FToggleSlider(Vars::Misc::Queueing::AutoDumpProfiles, Vars::Misc::Queueing::AutoDumpDelay);
					}
					PopTransparent();
				} EndSection();
				if (Section("Regions", 8))
				{
					FDropdown(Vars::Misc::Queueing::ForceRegions, FDropdownEnum::Left);
					FDropdown(Vars::Misc::Queueing::ExtendQueue, FDropdownEnum::Right);
				} EndSection();
			}

			TableNextColumn();
			{
				if (Section("Requeue"))
				{
					FToggle(Vars::Misc::Queueing::RQif);
					PushTransparent(!Vars::Misc::Queueing::RQif.Value);
					{
						FSlider(Vars::Misc::Queueing::RQplt, FSliderEnum::Left);
						FSlider(Vars::Misc::Queueing::RQpgt, FSliderEnum::Right);
						FToggle(Vars::Misc::Queueing::RQkick, FToggleEnum::Left);
						FToggle(Vars::Misc::Queueing::RQnoAbandon, FToggleEnum::Right);
						FToggle(Vars::Misc::Queueing::RQIgnoreFriends, FToggleEnum::Left);
						FToggle(Vars::Misc::Queueing::RQLTM, FToggleEnum::Right);
					}
					PopTransparent();
				} EndSection();
				if (Section("Community", 8))
				{
					FToggle(Vars::Misc::Queueing::AutoCommunityQueue);
					PushTransparent(!Vars::Misc::Queueing::AutoCommunityQueue.Value);
					{
						FSlider(Vars::Misc::Queueing::ServerSearchDelay, FSliderEnum::Left);
						FSlider(Vars::Misc::Queueing::MaxTimeOnServer, FSliderEnum::Right);
						FSlider(Vars::Misc::Queueing::MinPlayersOnServer, FSliderEnum::Left);
						FSlider(Vars::Misc::Queueing::MaxPlayersOnServer, FSliderEnum::Right);
						FToggle(Vars::Misc::Queueing::RequireNavmesh, FToggleEnum::Left);
						FToggle(Vars::Misc::Queueing::AvoidPasswordServers, FToggleEnum::Right);
						FToggle(Vars::Misc::Queueing::OnlyNonDedicatedServers, FToggleEnum::Left);
						FToggle(Vars::Misc::Queueing::OnlySteamNetworkingIPs, FToggleEnum::Right);
						FToggle(Vars::Misc::Queueing::PreferSteamNickServers, FToggleEnum::Left);
					}
					PopTransparent();
				} EndSection();
			}
			EndTable();
		}
		break;
	}
	}
}

void CMenu::MenuAnticheat(int iTab)
{
	using namespace ImGui;

	auto fPopupSelectable = [&](const std::string& sIdBase, int& iPopupRow, const std::string& sLabel, const char* sIcon, const ImVec4& tColor, auto&& fn)
	{
		const std::string sId = std::format("##{}{}{}", sIdBase, sLabel, iPopupRow++);
		ImVec2 vContentMin = GetWindowContentRegionMin();
		ImVec2 vContentMax = GetWindowContentRegionMax();
		float flRowWidth = std::max(0.f, vContentMax.x - vContentMin.x);
		if (flRowWidth <= 0.f)
			flRowWidth = GetContentRegionAvail().x;
		if (flRowWidth <= 0.f)
			flRowWidth = H::Draw.Scale(140);
		const float flRowHeight = H::Draw.Scale(32);
		const float flIconSlot = H::Draw.Scale(22);
		const ImVec2 vRowSize = { flRowWidth, flRowHeight };

		PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		bool bActivated = Selectable(sId.c_str(), false, ImGuiSelectableFlags_None, vRowSize);
		PopStyleColor(3);

		ImVec2 vMin = GetItemRectMin();
		ImVec2 vMax = GetItemRectMax();
		const bool bHovered = IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
		ImDrawList* pDrawList = GetWindowDrawList();

		const auto Tint = [&](float flAlpha) -> ImU32
		{
			ImVec4 tTint = tColor;
			tTint.w = flAlpha;
			return ColorConvertFloat4ToU32(tTint);
		};

		ImVec2 vIconMin = { vMin.x + H::Draw.Scale(8), vMin.y + (flRowHeight - flIconSlot) / 2 };
		ImVec2 vIconMax = { vIconMin.x + flIconSlot, vIconMin.y + flIconSlot };
		float flIconAlpha = bHovered ? 0.35f : 0.2f;
		pDrawList->AddRectFilled(vIconMin, vIconMax, Tint(flIconAlpha), H::Draw.Scale(6));

		const float flIconFontSize = F::Render.IconFont->LegacySize;
		const ImVec2 vIconSize = F::Render.IconFont->CalcTextSizeA(flIconFontSize, FLT_MAX, 0.f, sIcon);
		const ImVec2 vIconPos = { vIconMin.x + (flIconSlot - vIconSize.x) / 2, vIconMin.y + (flIconSlot - vIconSize.y) / 2 };
		pDrawList->AddText(F::Render.IconFont, flIconFontSize, vIconPos, ColorConvertFloat4ToU32(tColor), sIcon);

		const ImVec2 vTextPos = { vIconMax.x + H::Draw.Scale(8), vMin.y + (flRowHeight - GetTextLineHeight()) / 2 };
		pDrawList->AddText(vTextPos, GetColorU32(ImGuiCol_Text), sLabel.c_str());

		if (bActivated)
			fn();
	};

	switch (iTab)
	{
	// Cheaters
	case 0:
	{
		if (Section("API key"))
		{
			static std::string sAPIKeyBuffer = Vars::Config::SteamWebAPIKey.Value;
			static bool bEditingApiKey = false;
			static bool bRevealApiKey = false;
			if (!bEditingApiKey && sAPIKeyBuffer != Vars::Config::SteamWebAPIKey.Value)
				sAPIKeyBuffer = Vars::Config::SteamWebAPIKey.Value;

			const float flRowHeight = H::Draw.Scale(36);
			SetCursorPosY(GetCursorPosY() + H::Draw.Scale(4));
			const ImGuiInputTextFlags nApiKeyFlags = (bRevealApiKey ? 0 : ImGuiInputTextFlags_Password) | ImGuiInputTextFlags_EnterReturnsTrue;
			bool bSubmitted = false;
			bool bFinished = false;
			if (BeginTable("APIKeyRow", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadInnerX))
			{
				TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
				TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, H::Draw.Scale(150));
				TableNextRow(ImGuiTableRowFlags_None, flRowHeight);
				TableSetColumnIndex(0);
				SetCursorPosY(GetCursorPosY() + H::Draw.Scale(2));
				const float flInputWidth = std::max(GetContentRegionAvail().x - H::Draw.Scale(6), H::Draw.Scale(160));
				bSubmitted = FInputText("Steam Web API key", sAPIKeyBuffer, flInputWidth, nApiKeyFlags);
				bFinished = bSubmitted || IsItemDeactivatedAfterEdit();
				bEditingApiKey = IsItemActive();

				TableSetColumnIndex(1);
				BeginGroup();
				PushStyleVar(ImGuiStyleVar_FrameRounding, H::Draw.Scale(8));
				PushStyleColor(ImGuiCol_Button, F::Render.Background0.Value);
				PushStyleColor(ImGuiCol_ButtonHovered, F::Render.Background0p5.Value);
				PushStyleColor(ImGuiCol_ButtonActive, F::Render.Background1.Value);
				if (IconButton(bRevealApiKey ? ICON_MD_VISIBILITY : ICON_MD_VISIBILITY_OFF, flRowHeight))
					bRevealApiKey = !bRevealApiKey;
				PopStyleColor(3);
				SameLine(0.f, H::Draw.Scale(6));
				const ImVec2 vGetKeySize = { H::Draw.Scale(90), flRowHeight };
				if (Button("GET KEY", vGetKeySize))
					ShellExecuteA(NULL, "open", "https://steamcommunity.com/dev/apikey", NULL, NULL, SW_SHOWNORMAL);
				PopStyleVar();
				EndGroup();
				EndTable();
			}

			if (bFinished && sAPIKeyBuffer != Vars::Config::SteamWebAPIKey.Value)
			{
				Vars::Config::SteamWebAPIKey.Map[DEFAULT_BIND] = sAPIKeyBuffer;
				Vars::Config::SteamWebAPIKey.Value = sAPIKeyBuffer;
				SDK::Output("steamwebapi", sAPIKeyBuffer.empty() ? "Cleared API key" : "Saved steamwebapi key", { 175, 150, 255, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
			}

			PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
			FText("Used for downloading names and avatars via GetPlayerSummaries.");
			if (Vars::Config::SteamWebAPIKey.Value.empty())
				FText("Set a key to enable lookups for non-friends.");
			PopStyleColor();
		}
		EndSection();
		if (Section("Cheater List"))
		{
			auto vCheaters = F::PlayerUtils.GetCheaterVector();
			if (vCheaters.empty())
			{
				SetCursorPos({ H::Draw.Scale(15), H::Draw.Scale(40) });
				FText("Nothings here...");
				DebugDummy({ 0, H::Draw.Scale(8) });
			}
			else
			{
				auto toLower = [](std::string sText) -> std::string
					{
						std::transform(sText.begin(), sText.end(), sText.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						return sText;
					};

				auto getReasonColor = [&](const CheaterRecord_t& tRecord) -> Color_t
					{
						if (!tRecord.m_bAuto)
							return { 185, 185, 185, 255 };

						const std::string sReason = toLower(tRecord.m_sReason);
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

				std::sort(vCheaters.begin(), vCheaters.end(), [](const auto& a, const auto& b) -> bool
					{
						if (a.second.m_bAuto != b.second.m_bAuto)
							return a.second.m_bAuto > b.second.m_bAuto;
						if (a.second.m_iDetections != b.second.m_iDetections)
							return a.second.m_iDetections > b.second.m_iDetections;
						return a.second.m_sName < b.second.m_sName;
					});

				auto drawCheater = [&](const std::pair<uint32_t, CheaterRecord_t>& tEntry, int x, int y)
					{
						const auto& tRecord = tEntry.second;
						F::SteamProfileCache.TouchAvatar(tEntry.first);
						const std::string sName = tRecord.m_sName.empty() ? std::format("{}", tEntry.first) : tRecord.m_sName;
						std::string sReason = tRecord.m_sReason.empty() ? (tRecord.m_bAuto ? "detected by Amalgam" : "tagged by the player") : tRecord.m_sReason;
						if (!tRecord.m_bAuto)
							sReason = "tagged by the player";
						std::string sDetections = tRecord.m_bAuto ? std::format("Detections: {}", std::max(0, tRecord.m_iDetections)) : "Manual tag";
						const Color_t tCardColor = getReasonColor(tRecord);
						const ImColor tFillColor = ColorByteToFloat(tCardColor.Lerp(Vars::Menu::Theme::Background.Value, 0.55f, LerpEnum::NoAlpha));
						const ImColor tBorderColor = ColorByteToFloat(tCardColor);
						const ImColor tReasonColor = ColorByteToFloat(tCardColor);
						const ImU32 uFillColor = ImGui::ColorConvertFloat4ToU32(tFillColor.Value);
						const ImU32 uBorderColor = ImGui::ColorConvertFloat4ToU32(tBorderColor.Value);
						const ImU32 uReasonColor = ImGui::ColorConvertFloat4ToU32(tReasonColor.Value);
						const float flAvatarSize = H::Draw.Scale(40);
						const float flPadding = H::Draw.Scale(8);
						const float flTextOffset = flPadding * 2 + flAvatarSize;

						ImVec2 vOriginalPos = { !x ? GetStyle().WindowPadding.x : GetWindowWidth() / 2 + GetStyle().WindowPadding.x / 2, H::Draw.Scale(35 + 64 * y) };
						float flWidth = GetWindowWidth() / 2 - GetStyle().WindowPadding.x * 1.5f;
						float flHeight = H::Draw.Scale(56);
						ImVec2 vDrawPos = GetDrawPos() + vOriginalPos;
						auto pDrawList = GetWindowDrawList();
						pDrawList->AddRectFilled(vDrawPos, { vDrawPos.x + flWidth, vDrawPos.y + flHeight }, uFillColor, H::Draw.Scale(4));
						pDrawList->AddRect(vDrawPos, { vDrawPos.x + flWidth, vDrawPos.y + flHeight }, uBorderColor, H::Draw.Scale(4), ImDrawFlags_None, H::Draw.Scale());

						ImVec2 vAvatarPos = { vOriginalPos.x + flPadding, vOriginalPos.y + (flHeight - flAvatarSize) / 2 };
						if (ImTextureID pAvatar = GetAvatarTexture(tEntry.first))
						{
							SetCursorPos(vAvatarPos);
							Image(pAvatar, { flAvatarSize, flAvatarSize });
						}
						else
						{
							ImVec2 vAvatarDrawPos = GetDrawPos() + vAvatarPos;
							ImVec2 vAvatarDrawEnd = { vAvatarDrawPos.x + flAvatarSize, vAvatarDrawPos.y + flAvatarSize };
							const ImColor tAvatarBg = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp(tCardColor, 0.25f, LerpEnum::NoAlpha));
							pDrawList->AddRectFilled(vAvatarDrawPos, vAvatarDrawEnd, ImGui::ColorConvertFloat4ToU32(tAvatarBg.Value), H::Draw.Scale(4));
							char cInitial = '?';
							for (char cChar : sName)
							{
								if (std::isalpha(static_cast<unsigned char>(cChar)))
								{
									cInitial = static_cast<char>(std::toupper(static_cast<unsigned char>(cChar)));
									break;
								}
							}
							const std::string sInitial(1, cInitial);
							const ImVec2 vTextSize = F::Render.FontBold->CalcTextSizeA(F::Render.FontBold->LegacySize, FLT_MAX, 0.f, sInitial.c_str());
							const ImVec2 vTextPos = { vAvatarDrawPos.x + (flAvatarSize - vTextSize.x) / 2, vAvatarDrawPos.y + (flAvatarSize - vTextSize.y) / 2 };
							const auto tBg = Vars::Menu::Theme::Background.Value;
							const float flLuma = 0.2126f * (tBg.r / 255.f) + 0.7152f * (tBg.g / 255.f) + 0.0722f * (tBg.b / 255.f);
							const ImU32 uInitialColor = flLuma < 0.5f ? IM_COL32(240, 240, 240, 230) : IM_COL32(20, 20, 20, 230);
							pDrawList->AddText(F::Render.FontBold, F::Render.FontBold->LegacySize, vTextPos, uInitialColor, sInitial.c_str());
						}

						const float flAvailableWidth = flWidth - flTextOffset - H::Draw.Scale(28);
						SetCursorPos({ vOriginalPos.x + flTextOffset, vOriginalPos.y + H::Draw.Scale(6) });
						FText(TruncateText(sName, flAvailableWidth, F::Render.FontBold).c_str(), {}, 0, F::Render.FontBold);
						SetCursorPos({ vOriginalPos.x + flTextOffset, vOriginalPos.y + H::Draw.Scale(24) });
						PushStyleColor(ImGuiCol_Text, tReasonColor.Value);
						FText(TruncateText(sReason, flAvailableWidth).c_str());
						PopStyleColor();
						SetCursorPos({ vOriginalPos.x + flTextOffset, vOriginalPos.y + H::Draw.Scale(38) });
						PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
						FText(sDetections.c_str());
						PopStyleColor();

						SetCursorPos({ vOriginalPos.x + flWidth - H::Draw.Scale(24), vOriginalPos.y + H::Draw.Scale(4) });
						bool bRemove = IconButton(ICON_MD_DELETE);

						SetCursorPos(vOriginalPos);
						Button(std::format("##Cheater{}", tEntry.first).c_str(), { flWidth, flHeight });

						bool bPopup = IsItemHovered() && IsMouseReleased(ImGuiMouseButton_Right);
						if (bPopup)
							OpenPopup(std::format("CheaterMenu{}", tEntry.first).c_str());

						if (bRemove)
						{
							F::PlayerUtils.RemoveTag(tEntry.first, F::PlayerUtils.TagToIndex(CHEATER_TAG), true, sName.c_str());
							return;
						}

						if (FBeginPopup(std::format("CheaterMenu{}", tEntry.first).c_str()))
						{
							PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(12), H::Draw.Scale(8) });
							PushStyleVar(ImGuiStyleVar_WindowRounding, H::Draw.Scale(8));
							PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(4), H::Draw.Scale(4) });
							PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
							PushStyleColor(ImGuiCol_PopupBg, F::Render.Background0.Value);
							PushStyleColor(ImGuiCol_Border, F::Render.Background2.Value);
							PushStyleColor(ImGuiCol_Header, F::Render.Background1.Value);

							const ImVec4 tAccent = F::Render.Accent.Value;
							const ImVec4 tDanger = ColorByteToFloat(Color_t{ 255, 120, 120, 255 });
							const float flRowHeight = H::Draw.Scale(32);
							const float flIconSlot = H::Draw.Scale(22);
							int iPopupRow = 0;
							auto PopupSelectable = [&](const std::string& sLabel, const char* sIcon, const ImVec4& tColor, auto&& fn)
							{
								const std::string sId = std::format("##CheaterAction{}{}{}", tEntry.first, sLabel, iPopupRow++);
								ImVec2 vContentMin = GetWindowContentRegionMin();
								ImVec2 vContentMax = GetWindowContentRegionMax();
								float flRowWidth = std::max(0.f, vContentMax.x - vContentMin.x);
								if (flRowWidth <= 0.f)
									flRowWidth = GetContentRegionAvail().x;
								if (flRowWidth <= 0.f)
									flRowWidth = H::Draw.Scale(140);
								const ImVec2 vRowSize = { flRowWidth, flRowHeight };

								PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
								PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
								PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
								bool bActivated = Selectable(sId.c_str(), false, ImGuiSelectableFlags_None, vRowSize);
								PopStyleColor(3);

								ImVec2 vMin = GetItemRectMin();
								ImVec2 vMax = GetItemRectMax();
								const bool bHovered = IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
								ImDrawList* pDrawList = GetWindowDrawList();

								const auto Tint = [&](float flAlpha) -> ImU32
								{
									ImVec4 tTint = tColor;
									tTint.w = flAlpha;
									return ColorConvertFloat4ToU32(tTint);
								};

								ImVec2 vIconMin = { vMin.x + H::Draw.Scale(8), vMin.y + (flRowHeight - flIconSlot) / 2 };
								ImVec2 vIconMax = { vIconMin.x + flIconSlot, vIconMin.y + flIconSlot };
								float flIconAlpha = bHovered ? 0.35f : 0.2f;
								pDrawList->AddRectFilled(vIconMin, vIconMax, Tint(flIconAlpha), H::Draw.Scale(6));

								const float flIconFontSize = F::Render.IconFont->LegacySize;
								const ImVec2 vIconSize = F::Render.IconFont->CalcTextSizeA(flIconFontSize, FLT_MAX, 0.f, sIcon);
								const ImVec2 vIconPos = { vIconMin.x + (flIconSlot - vIconSize.x) / 2, vIconMin.y + (flIconSlot - vIconSize.y) / 2 };
								pDrawList->AddText(F::Render.IconFont, flIconFontSize, vIconPos, ColorConvertFloat4ToU32(tColor), sIcon);

								const ImVec2 vTextPos = { vIconMax.x + H::Draw.Scale(8), vMin.y + (flRowHeight - GetTextLineHeight()) / 2 };
								pDrawList->AddText(vTextPos, GetColorU32(ImGuiCol_Text), sLabel.c_str());

								if (bActivated)
									fn();
							};

							PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
							TextUnformatted("Steam");
							PopStyleColor();

							PopupSelectable("Profile", ICON_MD_PERSON, tAccent, [&]
							{
								I::SteamFriends->ActivateGameOverlayToUser("steamid", CSteamID(tEntry.first, k_EUniversePublic, k_EAccountTypeIndividual));
							});
							PopupSelectable("History", ICON_MD_HISTORY, tAccent, [&]
							{
								I::SteamFriends->ActivateGameOverlayToWebPage(std::format("https://steamhistory.net/id/{}", CSteamID(tEntry.first, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64()).c_str());
							});
							PopupSelectable("Refetch profile", ICON_MD_SYNC, tAccent, [&]
							{
								ResetAvatarTexture(tEntry.first);
								F::SteamProfileCache.Invalidate(tEntry.first);
								F::SteamProfileCache.TouchAvatar(tEntry.first);
								const auto uSteamID64 = CSteamID(tEntry.first, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
								SDK::Output("steamwebapi", std::format("Queued profile refetch for {}", uSteamID64).c_str(), { 175, 150, 255, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
							});

							Dummy({ 0, H::Draw.Scale(4) });
							PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
							TextUnformatted("Tags");
							PopStyleColor();

							PopupSelectable("Remove tag", ICON_MD_DELETE, tDanger, [&]
							{
								F::PlayerUtils.RemoveTag(tEntry.first, F::PlayerUtils.TagToIndex(CHEATER_TAG), true, sName.c_str());
							});

							PopStyleColor(3);
							PopStyleVar(4);
							EndPopup();
						}
					};

				int iBlu = 0, iRed = 0;
				for (size_t i = 0; i < vCheaters.size(); i++)
				{
					int x = int(i % 2);
					int y = int(i / 2);
					drawCheater(vCheaters[i], x, y);
					if (!x) iBlu++; else iRed++;
				}
				SetCursorPos({ 0, H::Draw.Scale(36 + 64 * std::max(iBlu, iRed)) });
				DebugDummy({ 0, H::Draw.Scale(8) });
			}
		}
		EndSection();
		{
			PushDisabled(F::PlayerUtils.m_bCheaterLoad);
			{
				SetCursorPosY(GetCursorPosY() - H::Draw.Scale(8));
				if (FButton(ICON_MD_SYNC, FButtonEnum::None, { 30, 30 }, 0, F::Render.IconFont))
					F::PlayerUtils.m_bCheaterLoad = true;

				if (FButton("Export", FButtonEnum::Fit | FButtonEnum::SameLine))
				{
					SDK::SetClipboard(F::PlayerUtils.ExportCheatersToJson());
					SDK::Output("Amalgam", "Copied cheaterlist to clipboard", { 255, 150, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
				}

				if (FButton("Import", FButtonEnum::Fit | FButtonEnum::SameLine))
				{
					if (F::PlayerUtils.ImportCheatersFromJson(SDK::GetClipboard(), true))
						SDK::Output("Amalgam", "Imported cheaterlist", { 255, 150, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
					else
						SDK::Output("Amalgam", "Failed to import cheaterlist", { 255, 150, 150, 127 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
				}
			}
			PopDisabled();

			if (FButton("Folder", FButtonEnum::Fit | FButtonEnum::SameLine))
				ShellExecuteA(NULL, NULL, F::Configs.m_sCorePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		break;
	}
	// Detection
	case 1:
	{
		if (Section("Cheater Detection"))
		{
			FDropdown(Vars::CheatDetection::Methods);
			PushTransparent(!Vars::CheatDetection::DetectionsRequired.Value);
			{
				FSlider(Vars::CheatDetection::DetectionsRequired);
			}
			PopTransparent();
			PushTransparent(!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::PacketChoking));
			{
				FSlider(Vars::CheatDetection::MinChoking);
			}
			PopTransparent();
			PushTransparent(!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::AimFlicking));
			{
				FSlider(Vars::CheatDetection::MinFlick, FSliderEnum::Left);
				FSlider(Vars::CheatDetection::MaxNoise, FSliderEnum::Right);
			}
			PopTransparent();
			PushTransparent(!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::LagCompAbuse));
			{
				FSlider(Vars::CheatDetection::LagCompMinimumDelta, FSliderEnum::Left);
				FSlider(Vars::CheatDetection::LagCompBurstCount, FSliderEnum::Right);
				FSlider(Vars::CheatDetection::LagCompWindow);
			}
			PopTransparent();
			PushTransparent(!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::CritManipulation));
			{
				FSlider(Vars::CheatDetection::CritWindow, FSliderEnum::Left);
				FSlider(Vars::CheatDetection::CritThreshold, FSliderEnum::Right);
			}
			PopTransparent();
		}
		EndSection();
		break;
	}
	}
}

void CMenu::MenuLogs(int iTab)
{
	using namespace ImGui;

	auto fPopupSelectable = [&](const std::string& sIdBase, int& iPopupRow, const std::string& sLabel, const char* sIcon, const ImVec4& tColor, auto&& fn)
	{
		const std::string sId = std::format("##{}{}{}", sIdBase, sLabel, iPopupRow++);
		ImVec2 vContentMin = GetWindowContentRegionMin();
		ImVec2 vContentMax = GetWindowContentRegionMax();
		float flRowWidth = std::max(0.f, vContentMax.x - vContentMin.x);
		if (flRowWidth <= 0.f)
			flRowWidth = GetContentRegionAvail().x;
		if (flRowWidth <= 0.f)
			flRowWidth = H::Draw.Scale(140);
		const float flRowHeight = H::Draw.Scale(32);
		const float flIconSlot = H::Draw.Scale(22);
		const ImVec2 vRowSize = { flRowWidth, flRowHeight };

		PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		bool bActivated = Selectable(sId.c_str(), false, ImGuiSelectableFlags_None, vRowSize);
		PopStyleColor(3);

		ImVec2 vMin = GetItemRectMin();
		ImVec2 vMax = GetItemRectMax();
		const bool bHovered = IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
		ImDrawList* pDrawList = GetWindowDrawList();

		const auto Tint = [&](float flAlpha) -> ImU32
		{
			ImVec4 tTint = tColor;
			tTint.w = flAlpha;
			return ColorConvertFloat4ToU32(tTint);
		};

		ImVec2 vIconMin = { vMin.x + H::Draw.Scale(8), vMin.y + (flRowHeight - flIconSlot) / 2 };
		ImVec2 vIconMax = { vIconMin.x + flIconSlot, vIconMin.y + flIconSlot };
		float flIconAlpha = bHovered ? 0.35f : 0.2f;
		pDrawList->AddRectFilled(vIconMin, vIconMax, Tint(flIconAlpha), H::Draw.Scale(6));

		const float flIconFontSize = F::Render.IconFont->LegacySize;
		const ImVec2 vIconSize = F::Render.IconFont->CalcTextSizeA(flIconFontSize, FLT_MAX, 0.f, sIcon);
		const ImVec2 vIconPos = { vIconMin.x + (flIconSlot - vIconSize.x) / 2, vIconMin.y + (flIconSlot - vIconSize.y) / 2 };
		pDrawList->AddText(F::Render.IconFont, flIconFontSize, vIconPos, ColorConvertFloat4ToU32(tColor), sIcon);

		const ImVec2 vTextPos = { vIconMax.x + H::Draw.Scale(8), vMin.y + (flRowHeight - GetTextLineHeight()) / 2 };
		pDrawList->AddText(vTextPos, GetColorU32(ImGuiCol_Text), sLabel.c_str());

		if (bActivated)
			fn();
	};

	switch (iTab)
	{
	// PlayerList
	case 0:
	{
		if (Section("Players"))
		{
			if (I::EngineClient->IsInGame())
			{
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

				auto fGetTeamColor = [&](int iTeam, bool bAlive)
				{
					switch (iTeam)
					{
					case 3: return Color_t(100, 150, 200, bAlive ? 255 : 127).Lerp(Vars::Menu::Theme::Background.Value, 0.5f, LerpEnum::NoAlpha);
					case 2: return Color_t(255, 100, 100, bAlive ? 255 : 127).Lerp(Vars::Menu::Theme::Background.Value, 0.5f, LerpEnum::NoAlpha);
					}
					return Color_t(127, 127, 127, 255).Lerp(Vars::Menu::Theme::Background.Value, 0.5f, LerpEnum::NoAlpha);
				};
				auto fDrawPlayer = [&](const ListPlayer_t& tPlayer, int x, int y)
				{
					ImVec2 vOriginalPos = { !x ? GetStyle().WindowPadding.x : GetWindowWidth() / 2 + GetStyle().WindowPadding.x / 2, H::Draw.Scale(35 + 36 * y) };

					// background
					float flWidth = GetWindowWidth() / 2 - GetStyle().WindowPadding.x * 1.5f;
					float flHeight = H::Draw.Scale(28);
					ImColor tColor = ColorByteToFloat(fGetTeamColor(tPlayer.m_iTeam, tPlayer.m_bAlive));
					ImVec2 vDrawPos = GetDrawPos() + vOriginalPos;
					GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + ImVec2(flWidth, flHeight), tColor, H::Draw.Scale(4));

					// tag bar
					bool bPopup = false;
					bool bIcon = tPlayer.m_bLocal || F::Spectate.GetTarget(true) == tPlayer.m_iUserID || tPlayer.m_bFriend || tPlayer.m_bParty;
					float flBarWidth = 0.f;

					if (!tPlayer.m_bFake)
					{
						std::vector<PriorityLabel_t> vLabels = {};
						std::vector<std::pair<PriorityLabel_t*, int>> vTags = {};
						if (int iParty = tPlayer.m_iParty)
						{
							auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)];
							if (!--iParty)
								vTags.emplace_back(pTag, 0);
							else
								vLabels.emplace_back(std::format("{}: {}", pTag->m_sName, iParty), pTag->m_tColor.HueShift(iParty * 360.f / iPartyCount));
						}
						if (tPlayer.m_bF2P)
						{
							auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(F2P_TAG)];
							vTags.emplace_back(pTag, 0);
						}
						for (auto& iID : F::PlayerUtils.GetPlayerTags(tPlayer.m_uAccountID))
						{
							if (auto pTag = F::PlayerUtils.GetTag(iID))
								vTags.emplace_back(pTag, iID);
						}

						if (!vLabels.empty() || !vTags.empty())
						{
							PushFont(F::Render.FontSmall);
							flBarWidth = H::Draw.Scale(4);
							for (auto& tTag : vLabels)
								flBarWidth += FCalcTextSize(tTag.m_sName.c_str()).x + H::Draw.Scale(14);
							for (auto& [pTag, iID] : vTags)
								flBarWidth += FCalcTextSize(pTag->m_sName.c_str()).x + H::Draw.Scale(iID ? 29 : 14);
							flBarWidth = std::min(flBarWidth, std::max(flWidth - FCalcTextSize(tPlayer.m_sName.c_str(), F::Render.FontRegular).x - H::Draw.Scale(bIcon ? 33 : 14), flWidth / 2));

							SetCursorPos(vOriginalPos + ImVec2(flWidth - floorf(flBarWidth), 0));
							if (BeginChild(std::format("TagBar{}", tPlayer.m_iUserID).c_str(), { flBarWidth, flHeight }, ImGuiWindowFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
							{
								const auto vDrawPos = GetDrawPos();
								float flTagOffset = H::Draw.Scale(4);
								auto fDrawTag = [&](PriorityLabel_t& tTag, int iID)
								{
									ImColor tTagColor = ColorByteToFloat(tTag.m_tColor);
									float flTagWidth = FCalcTextSize(tTag.m_sName.c_str()).x + H::Draw.Scale(!iID ? 10 : 25);
									float flTagHeight = H::Draw.Scale(20);
									ImVec2 vTagPos = { flTagOffset, H::Draw.Scale(4) };

									GetWindowDrawList()->AddRectFilled(vDrawPos + vTagPos, vDrawPos + vTagPos + ImVec2(flTagWidth, flTagHeight), tTagColor, H::Draw.Scale(4));
									SetCursorPos(vTagPos + ImVec2(H::Draw.Scale(5), H::Draw.Scale(3)));
									TextColored(ColorFloatToByte(tColor).Blend(tTag.m_tColor).IsColorBright() ? ImVec4(0, 0, 0, 1) : ImVec4(1, 1, 1, 1), tTag.m_sName.c_str());
									if (iID)
									{
										SetCursorPos(vTagPos + ImVec2(flTagWidth - H::Draw.Scale(22), H::Draw.Scale(-2)));
										if (IconButton(ICON_MD_CANCEL))
											F::PlayerUtils.RemoveTag(tPlayer.m_uAccountID, iID, true, tPlayer.m_sName.c_str());
									}

									flTagOffset += flTagWidth + H::Draw.Scale(4);
								};

								for (auto& tTag : vLabels)
									fDrawTag(tTag, 0);
								for (auto& [pTag, iID] : vTags)
									fDrawTag(*pTag, iID);
								SetCursorPosX(flTagOffset); DebugDummy({});
							} EndChild();
							PopFont();

							bPopup = IsItemHovered() && IsMouseReleased(ImGuiMouseButton_Right);
						}
					}

					// text + icons
					int lOffset = H::Draw.Scale(10);
					if (bIcon)
					{
						lOffset += H::Draw.Scale(19);
						SetCursorPos(vOriginalPos + ImVec2(H::Draw.Scale(7), H::Draw.Scale(6)));
						if (tPlayer.m_bLocal)
							IconImage(ICON_MD_PERSON);
						else if (F::Spectate.GetTarget(true) == tPlayer.m_iUserID)
							IconImage(ICON_MD_VISIBILITY);
						else if (tPlayer.m_bFriend)
							IconImage(ICON_MD_GROUP);
						else if (tPlayer.m_bParty)
							IconImage(ICON_MD_GROUPS);
					}
					SetCursorPos(vOriginalPos + ImVec2(lOffset, H::Draw.Scale(7)));
					auto sName = TruncateText(tPlayer.m_sName, flWidth - lOffset - flBarWidth);
					FText(sName.c_str());
					lOffset += FCalcTextSize(sName.c_str()).x + H::Draw.Scale(8);

					// buttons
					SetCursorPos(vOriginalPos);
					Button(std::format("##{}", tPlayer.m_iUserID).c_str(), { flWidth, flHeight });
					bPopup |= IsItemHovered() && IsMouseReleased(ImGuiMouseButton_Right);

					// popups
					if (bPopup)
						OpenPopup(std::format("RightClicked{}", tPlayer.m_iUserID).c_str());

					if (FBeginPopup(std::format("RightClicked{}", tPlayer.m_iUserID).c_str()))
					{
						PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), H::Draw.Scale(8) });

						if (!tPlayer.m_bFake)
						{
							if (FSelectable("Profile"))
								I::SteamFriends->ActivateGameOverlayToUser("steamid", CSteamID(tPlayer.m_uAccountID, k_EUniversePublic, k_EAccountTypeIndividual));
							if (FSelectable("History"))
								I::SteamFriends->ActivateGameOverlayToWebPage(std::format("https://steamhistory.net/id/{}", CSteamID(tPlayer.m_uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64()).c_str());
						}

						if (FSelectable(F::Spectate.GetTarget(true) == tPlayer.m_iUserID ? "Unspectate" : "Spectate"))
							F::Spectate.SetTarget(tPlayer.m_iUserID);

						if (!I::EngineClient->IsPlayingDemo() && FBeginMenu("Votekick"))
						{
							if (IsItemHovered() && IsMouseDown(ImGuiMouseButton_Left))
							{
								I::ClientState->SendStringCmd(std::format("callvote Kick \"{}\"", tPlayer.m_iUserID).c_str());
								CloseCurrentPopup();
							}
							if (FSelectable("No reason"))
								I::ClientState->SendStringCmd(std::format("callvote Kick \"{} other\"", tPlayer.m_iUserID).c_str());
							if (FSelectable("Cheating"))
								I::ClientState->SendStringCmd(std::format("callvote Kick \"{} cheating\"", tPlayer.m_iUserID).c_str());
							if (FSelectable("Idle"))
								I::ClientState->SendStringCmd(std::format("callvote Kick \"{} idle\"", tPlayer.m_iUserID).c_str());
							if (FSelectable("Scamming"))
								I::ClientState->SendStringCmd(std::format("callvote Kick \"{} scamming\"", tPlayer.m_iUserID).c_str());

							ImGui::EndMenu();
						}

						if (!tPlayer.m_bFake)
						{
							if (FBeginMenu("Add tag"))
							{
								for (auto it = F::PlayerUtils.m_vTags.begin(); it != F::PlayerUtils.m_vTags.end(); it++)
								{
									int iID = std::distance(F::PlayerUtils.m_vTags.begin(), it);
									auto& tTag = *it;
									if (!tTag.m_bAssignable || F::PlayerUtils.HasTag(tPlayer.m_uAccountID, iID))
										continue;

									ImVec4 tColor = ColorByteToVec(tTag.m_tColor);
									PushStyleColor(ImGuiCol_Text, tColor);
									tColor.x /= 3; tColor.y /= 3; tColor.z /= 3;
									if (FSelectable(tTag.m_sName.c_str(), tColor))
										F::PlayerUtils.AddTag(tPlayer.m_uAccountID, iID, true, tPlayer.m_sName.c_str());
									PopStyleColor();
								}

								ImGui::EndMenu();
							}
							if (FBeginMenu("Alias"))
							{
								bool bHasAlias = F::PlayerUtils.m_mPlayerAliases.contains(tPlayer.m_uAccountID);
								static std::string sInput = "";

								bool bEnter = FInputText("Alias...", sInput, H::Draw.Scale(150), ImGuiInputTextFlags_EnterReturnsTrue);
								if (!IsItemFocused())
									sInput = bHasAlias ? F::PlayerUtils.m_mPlayerAliases[tPlayer.m_uAccountID] : "";
								if (bEnter)
								{
									if (sInput.empty() && bHasAlias)
									{
										F::Output.AliasChanged(tPlayer.m_sName.c_str(), "Removed", F::PlayerUtils.m_mPlayerAliases[tPlayer.m_uAccountID].c_str());

										F::PlayerUtils.m_mPlayerAliases.erase(tPlayer.m_uAccountID);
										F::PlayerUtils.m_bSave = true;
									}
									else if (!sInput.empty())
									{
										F::PlayerUtils.m_mPlayerAliases[tPlayer.m_uAccountID] = sInput;
										F::PlayerUtils.m_bSave = true;

										F::Output.AliasChanged(tPlayer.m_sName.c_str(), bHasAlias ? "Changed" : "Added", sInput.c_str());
									}
								}

								ImGui::EndMenu();
							}
						}

						if (Vars::Resolver::Enabled.Value && !tPlayer.m_bLocal && !I::EngineClient->IsPlayingDemo())
						{
							if (FBeginMenu("Set yaw"))
							{
								static std::vector<std::pair<const char*, float>> vYaws = {
									{ "Auto", 0.f },
									{ "Forward", 0.f },
									{ "Left", 90.f },
									{ "Right", -90.f },
									{ "Backwards", 180.f }
								};
								for (auto& [sYaw, flValue] : vYaws)
								{
									if (FSelectable(sYaw))
									{
										switch (FNV1A::Hash32(sYaw))
										{
										case FNV1A::Hash32Const("Auto"):
											F::Resolver.SetYaw(tPlayer.m_iUserID, 0.f, true);
											break;
										default:
											F::Resolver.SetYaw(tPlayer.m_iUserID, flValue);
										}
									}
								}

								ImGui::EndMenu();
							}
							if (FBeginMenu("Set pitch"))
							{
								static std::vector<std::pair<const char*, float>> vPitches = {
									{ "Auto", 0.f },
									{ "Up", -90.f },
									{ "Down", 90.f },
									{ "Zero", 0.f },
									{ "Inverse", 0.f }
								};
								for (auto& [sPitch, flValue] : vPitches)
								{
									if (FSelectable(sPitch))
									{
										switch (FNV1A::Hash32(sPitch))
										{
										case FNV1A::Hash32Const("Auto"):
											F::Resolver.SetPitch(tPlayer.m_iUserID, 0.f, false, true);
											break;
										case FNV1A::Hash32Const("Inverse"):
											F::Resolver.SetPitch(tPlayer.m_iUserID, 0.f, true);
											break;
										default:
											F::Resolver.SetPitch(tPlayer.m_iUserID, flValue);
										}
									}
								}

								ImGui::EndMenu();
							}
							if (FBeginMenu("Set view"))
							{
								static std::vector<std::pair<const char*, bool>> vPitches = {
									{ "Offset from static view", true },
									{ "Offset from view to local", false }
								};
								for (auto& [sPitch, bValue] : vPitches)
								{
									if (FSelectable(sPitch))
										F::Resolver.SetView(tPlayer.m_iUserID, bValue);
								}

								ImGui::EndMenu();
							}
							if (FBeginMenu("Set minwalk"))
							{
								static std::vector<std::pair<const char*, bool>> vPitches = {
									{ "Minwalk on", true },
									{ "Minwalk off", false }
								};
								for (auto& [sPitch, bValue] : vPitches)
								{
									if (FSelectable(sPitch))
										F::Resolver.SetMinwalk(tPlayer.m_iUserID, bValue);
								}

								ImGui::EndMenu();
							}
						}

						if (mParties.contains(tPlayer.m_iParty))
						{
							Divider(H::Draw.Scale(1), 0);

							TextColored(F::Render.Inactive.Value, "Partied:");
							for (auto& pPlayer2 : mParties[tPlayer.m_iParty])
								TextColored(F::Render.Inactive.Value, pPlayer2->m_sName.c_str());
						}

						if (tPlayer.m_iLevel != -2)
						{
							Divider(H::Draw.Scale(1), 0);

							std::string sLevel = "T? L?";
							if (tPlayer.m_iLevel != -1)
							{
								int iTier = std::max(ceilf(tPlayer.m_iLevel / 150.f), 1.f);
								int iLevel = ((tPlayer.m_iLevel - 1) % 150) + 1;
								sLevel = std::format("T{} L{}", iTier, iLevel);
							}
							TextColored(F::Render.Inactive.Value, sLevel.c_str());
						}

						PopStyleVar();
						EndPopup();
					}
				};

				// display players
				std::vector<ListPlayer_t> vBlu, vRed, vOther;
				for (auto& tPlayer : vPlayers)
				{
					switch (tPlayer.m_iTeam)
					{
					case 3: vBlu.push_back(tPlayer); break;
					case 2: vRed.push_back(tPlayer); break;
					default: vOther.push_back(tPlayer); break;
					}
				}

				int iBlu = 0, iRed = 0;
				for (size_t i = 0; i < vBlu.size(); i++)
				{
					fDrawPlayer(vBlu[i], 0, int(i));
					iBlu++;
				}
				for (size_t i = 0; i < vRed.size(); i++)
				{
					fDrawPlayer(vRed[i], 1, int(i));
					iRed++;
				}
				if (vOther.empty())
				{
					SetCursorPos({ 0, H::Draw.Scale(36 * std::max(iBlu, iRed) - 1) }); DebugDummy({ 0, H::Draw.Scale(28) });
				}
				else
				{
					size_t iMax = std::max(iBlu, iRed);
					for (size_t i = 0; i < vOther.size(); i++)
						fDrawPlayer(vOther[i], i % 2, int(iMax + i / 2));
				}
			}
			else
			{
				SetCursorPos({ H::Draw.Scale(15), H::Draw.Scale(40) });
				FText("Not ingame");
				DebugDummy({ 0, H::Draw.Scale(8) });
			}
		} EndSection();
		if (Section("Tags"))
		{
			static int iID = -1;
			static PriorityLabel_t tTag = {};

			auto vTable = WidgetTable(2, H::Draw.Scale(104));

			if (BeginWidgetTable(0, vTable))
			{
				FSDropdown("Name", &tTag.m_sName, {}, FSDropdownEnum::AutoUpdate, -10);
				FColorPicker("Color", &tTag.m_tColor, FColorPickerEnum::SameLine, {}, { H::Draw.Scale(10), H::Draw.Scale(40) });

				PushDisabled(iID == DEFAULT_TAG || iID == IGNORED_TAG);
				{
					int iLabel = Disabled ? 0 : tTag.m_bLabel;
					FDropdown("Type", &iLabel, { "Priority", "Label" }, {});
					tTag.m_bLabel = iLabel;
					if (Disabled)
						tTag.m_bLabel = false;
				}
				PopDisabled();
			} EndChild();

			if (BeginWidgetTable(1, vTable))
			{
				const int iTag = F::PlayerUtils.IndexToTag(iID);
				PushTransparent(tTag.m_bLabel); // transparent if we want a label, user can still use to sort
				{
					const bool bDisableVotePriority = iID != -1 && (iTag == IGNORED_TAG || iTag == FRIEND_TAG || iTag == PARTY_TAG);

					SetCursorPosY(GetCursorPos().y + H::Draw.Scale(16));
					FSlider("Priority", &tTag.m_iPriority, -10, 10, 1, "%i", FSliderEnum::Left);
					FSlider("Followbot priority", &tTag.m_iFollowPriority, -1, 10, 1, tTag.m_iFollowPriority < 0 ? "ignore" : "%i", FSliderEnum::Right);
					PushDisabled(bDisableVotePriority);
					FDropdown("Vote priority", &tTag.m_iVotePriority, { "Defend", "Ignore", "Kick" }, { -1, 0, 1 }, FDropdownEnum::None, -96);
					PopDisabled();
				}
				PopTransparent();

				// create/modify button
				bool bCreate = false, bClear = false;

				SetCursorPos({ GetWindowWidth() - H::Draw.Scale(96), H::Draw.Scale(56) });
				PushDisabled(tTag.m_sName.empty());
				{
					bCreate = FButton(iID != -1 ? ICON_MD_SETTINGS : ICON_MD_ADD, FButtonEnum::None, { 40, 40 }, 0, F::Render.IconFont);
				}
				PopDisabled();

				// clear button
				SetCursorPos({ GetWindowWidth() - H::Draw.Scale(48), H::Draw.Scale(56) });
				bClear = FButton(ICON_MD_CLEAR, FButtonEnum::None, { 40, 40 }, 0, F::Render.IconFont);

				if (bCreate)
				{
					F::PlayerUtils.m_bSave = true;
					if (iID > -1 || iID < F::PlayerUtils.m_vTags.size())
					{
						const bool bDisableVotePriority = iTag == IGNORED_TAG || iTag == FRIEND_TAG || iTag == PARTY_TAG;

						F::PlayerUtils.m_vTags[iID].m_sName = tTag.m_sName;
						F::PlayerUtils.m_vTags[iID].m_tColor = tTag.m_tColor;
						F::PlayerUtils.m_vTags[iID].m_iPriority = tTag.m_iPriority;
						F::PlayerUtils.m_vTags[iID].m_iFollowPriority = tTag.m_iFollowPriority;
						if (!bDisableVotePriority)
							F::PlayerUtils.m_vTags[iID].m_iVotePriority = tTag.m_iVotePriority;
						F::PlayerUtils.m_vTags[iID].m_bLabel = tTag.m_bLabel;
					}
					else
						F::PlayerUtils.m_vTags.push_back(tTag);
				}
				if (bCreate || bClear)
				{
					iID = -1;
					tTag = {};
				}
			} EndChild();

			auto fDrawTag = [](std::vector<PriorityLabel_t>::iterator it, PriorityLabel_t& _tTag, int y)
			{
				int _iID = std::distance(F::PlayerUtils.m_vTags.begin(), it);

				ImVec2 vOriginalPos = { !_tTag.m_bLabel ? GetStyle().WindowPadding.x : GetWindowWidth() * 2 / 3 + GetStyle().WindowPadding.x / 2, H::Draw.Scale(152 + 36 * y) };

				// background
				float flWidth = GetWindowWidth() * (_tTag.m_bLabel ? 1.f / 3 : 2.f / 3) - GetStyle().WindowPadding.x * 1.5f;
				float flHeight = H::Draw.Scale(28);
				ImColor tColor = ColorByteToFloat(_tTag.m_tColor.Lerp(Vars::Menu::Theme::Background.Value, 0.5f, LerpEnum::NoAlpha));
				ImVec2 vDrawPos = GetDrawPos() + vOriginalPos;
				if (iID != _iID)
					GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + ImVec2(flWidth, flHeight), tColor, H::Draw.Scale(4));
				else
				{
					ImColor tColor2 = { tColor.Value.x * 1.1f, tColor.Value.y * 1.1f, tColor.Value.z * 1.1f, tColor.Value.w };
					GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + ImVec2(flWidth, flHeight), tColor2, H::Draw.Scale(4));

					tColor2 = ColorByteToFloat(_tTag.m_tColor.Lerp(Vars::Menu::Theme::Background.Value, 0.25f, LerpEnum::NoAlpha));
					float flInset = H::Draw.Scale(0.5f) - 0.5f;
					GetWindowDrawList()->AddRect(vDrawPos + ImVec2(flInset, flInset), vDrawPos + ImVec2(flWidth - flInset, flHeight - flInset), tColor2, H::Draw.Scale(4), ImDrawFlags_None, H::Draw.Scale());
				}

				// text
				SetCursorPos(vOriginalPos + ImVec2(H::Draw.Scale(9), H::Draw.Scale(7)));
				FText(TruncateText(_tTag.m_sName, _tTag.m_bLabel ? flWidth - H::Draw.Scale(38) : flWidth / 2 - H::Draw.Scale(20)).c_str());

				if (!_tTag.m_bLabel)
				{
					auto drawQuickInfo = [](ImVec2 vOriginalPos, float& flOffset, float flWidth, int iValue, const char* sIcon, ImVec4 tIconColor = { 1.0f, 1.0f, 1.0f, -1.0f }) -> void
					{
						SetCursorPos(vOriginalPos + ImVec2(flOffset, H::Draw.Scale(7)));
						IconImage(sIcon, tIconColor);
						SetCursorPos(vOriginalPos + ImVec2(flOffset + H::Draw.Scale(20), H::Draw.Scale(7)));
						FText(std::format("{}", iValue).c_str());
						flOffset += flWidth / 8;
					};

					float flOffset = flWidth / 2;
					drawQuickInfo(vOriginalPos, flOffset, flWidth, _tTag.m_iPriority, _tTag.m_iPriority < 0 ? ICON_MD_FAVORITE : ICON_MD_HEART_BROKEN, _tTag.m_iPriority < 0 ? ImVec4(1.0f, 1.0f, 1.0f, -1.0f) : ImVec4(0.867f, 0.18f, 0.267f, 1.f));
					drawQuickInfo(vOriginalPos, flOffset, flWidth, _tTag.m_iFollowPriority, ICON_MD_DIRECTIONS_RUN);
					drawQuickInfo(vOriginalPos, flOffset, flWidth, _tTag.m_iVotePriority, ICON_MD_CAMPAIGN);
				}

				// buttons / icons
				bool bDelete = false;
				if (!_tTag.m_bLocked)
				{
					SetCursorPos(vOriginalPos + ImVec2(flWidth - H::Draw.Scale(26), H::Draw.Scale(2)));
					bDelete = IconButton(ICON_MD_DELETE);
				}
				else
				{
					SetCursorPos(vOriginalPos + ImVec2(flWidth - H::Draw.Scale(22), H::Draw.Scale(6)));
					switch (F::PlayerUtils.IndexToTag(_iID))
					{
					//case DEFAULT_TAG: // no image
					case IGNORED_TAG: IconImage(ICON_MD_DO_NOT_DISTURB); break;
					case CHEATER_TAG: IconImage(ICON_MD_FLAG); break;
					case FRIEND_TAG: IconImage(ICON_MD_GROUP); break;
					case PARTY_TAG: IconImage(ICON_MD_GROUPS); break;
					case F2P_TAG: IconImage(ICON_MD_MONEY_OFF); break;
					}
				}

				SetCursorPos(vOriginalPos);
				bool bClicked = Button(std::format("##{}", _tTag.m_sName).c_str(), { flWidth, flHeight });
				bool bPopup = IsItemClicked(ImGuiMouseButton_Right);

				if (bClicked)
				{
					iID = _iID;
					tTag.m_sName = _tTag.m_sName;
					tTag.m_tColor = _tTag.m_tColor;
					tTag.m_iPriority = _tTag.m_iPriority;
					tTag.m_iFollowPriority = _tTag.m_iFollowPriority;
					tTag.m_iVotePriority = _tTag.m_iVotePriority;
					tTag.m_bLabel = _tTag.m_bLabel;
				}
				else if (bPopup)
					OpenPopup(std::format("RightClicked{}", _iID).c_str());
				else if (bDelete)
					OpenPopup(std::format("DeleteTag{}", _iID).c_str());

				if (FBeginPopup(std::format("RightClicked{}", _iID).c_str()))
				{
					PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), 0 });

					auto& _tTag2 = *it;
					bool bSave = false;

					{
						static std::string sInput = "";

						bool bEnter = FInputText("Name...", sInput, H::Draw.Scale(284), ImGuiInputTextFlags_EnterReturnsTrue);
						if (!IsItemFocused())
							sInput = _tTag2.m_sName;
						if (bEnter)
						{
							_tTag2.m_sName = sInput;
							bSave = true;
						}
					}

					PushDisabled(_iID == DEFAULT_TAG || _iID == IGNORED_TAG);
					{
						int iLabel = Disabled ? 0 : _tTag2.m_bLabel;
						if (FDropdown("Type##", &iLabel, { "Priority", "Label" }))
							bSave = true;
						_tTag2.m_bLabel = iLabel;
						if (Disabled)
							_tTag2.m_bLabel = false;
					}
					PopDisabled();
					if (FSlider("Priority##", &_tTag2.m_iPriority, -10, 10))
						bSave = true;

					if (bSave)
						F::PlayerUtils.m_bSave = true;

					PopStyleVar();
					EndPopup();
				}
				else if (FBeginPopupModal(std::format("DeleteTag{}", _iID).c_str()))
				{
					FText(std::format("Do you really want to delete '{}'?", _tTag.m_sName).c_str());

					if (FButton("Yes", FButtonEnum::Left))
					{
						F::PlayerUtils.m_vTags.erase(it);
						F::PlayerUtils.m_bSave = F::PlayerUtils.m_bSave = true;

						for (auto& vTags : F::PlayerUtils.m_mPlayerTags | std::views::values)
						{
							for (auto it = vTags.begin(); it != vTags.end();)
							{
								if (_iID == *it)
									vTags.erase(it);
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

						CloseCurrentPopup();
					}
					if (FButton("No", FButtonEnum::Right | FButtonEnum::SameLine))
						CloseCurrentPopup();

					EndPopup();
				}
			};

			PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
			SetCursorPos({ H::Draw.Scale(13), H::Draw.Scale(122) }); Divider(); SetCursorPosY(H::Draw.Scale(134)); FText("Priorities");
			SetCursorPos({ GetWindowWidth() * 2 / 3 + H::Draw.Scale(9), H::Draw.Scale(134) }); FText("Labels");
			PopStyleColor();

			std::vector<std::pair<std::vector<PriorityLabel_t>::iterator, PriorityLabel_t>> vPriorities = {}, vLabels = {};
			for (auto it = F::PlayerUtils.m_vTags.begin(); it != F::PlayerUtils.m_vTags.end(); it++)
			{
				auto& _tTag = *it;

				if (!_tTag.m_bLabel)
					vPriorities.emplace_back(it, _tTag);
				else
					vLabels.emplace_back(it, _tTag);
			}

			std::sort(vPriorities.begin(), vPriorities.end(), [&](const auto& a, const auto& b) -> bool
			{
				// override for default tag
				if (std::distance(F::PlayerUtils.m_vTags.begin(), a.first) == DEFAULT_TAG)
					return true;
				if (std::distance(F::PlayerUtils.m_vTags.begin(), b.first) == DEFAULT_TAG)
					return false;

				// sort by priority if unequal
				if (a.second.m_iPriority != b.second.m_iPriority)
					return a.second.m_iPriority > b.second.m_iPriority;

				if (a.second.m_iFollowPriority != b.second.m_iFollowPriority)
					return a.second.m_iFollowPriority > b.second.m_iFollowPriority;

				if (a.second.m_iVotePriority != b.second.m_iVotePriority)
					return a.second.m_iVotePriority > b.second.m_iVotePriority;

				return a.second.m_sName < b.second.m_sName;
			});

			std::sort(vLabels.begin(), vLabels.end(), [&](const auto& a, const auto& b) -> bool
			{
				// sort by priority if unequal
				if (a.second.m_iPriority != b.second.m_iPriority)
					return a.second.m_iPriority > b.second.m_iPriority;

				if (a.second.m_iFollowPriority != b.second.m_iFollowPriority)
					return a.second.m_iFollowPriority > b.second.m_iFollowPriority;

				if (a.second.m_iVotePriority != b.second.m_iVotePriority)
					return a.second.m_iVotePriority > b.second.m_iVotePriority;

				return a.second.m_sName < b.second.m_sName;
			});

			// display tags
			int iPriorities = 0, iLabels = 0;
			for (auto& [it, _tTag] : vPriorities)
			{
				fDrawTag(it, _tTag, iPriorities);
				iPriorities++;
			}
			for (auto& [it, _tTag] : vLabels)
			{
				fDrawTag(it, _tTag, iLabels);
				iLabels++;
			}
			SetCursorPos({ 0, H::Draw.Scale(116 + 36 * std::max(iPriorities, iLabels)) }); DebugDummy({ 0, H::Draw.Scale(28) });
		} EndSection();
		if (Section("Marked Players"))
		{
			auto vMarkedPlayers = F::PlayerUtils.GetMarkedPlayers();
			if (vMarkedPlayers.empty())
			{
				SetCursorPos({ H::Draw.Scale(15), H::Draw.Scale(40) });
				FText("Nothings here...");
				DebugDummy({ 0, H::Draw.Scale(8) });
			}
			else
			{
				auto drawMarkedPlayer = [&](const MarkedPlayer_t& tEntry, int x, int y)
				{
					const std::string sCardId = std::format("MarkedPlayer{}", tEntry.m_uAccountID);
					PushID(sCardId.c_str());

					F::SteamProfileCache.TouchAvatar(tEntry.m_uAccountID);

					const Color_t tCardColor = tEntry.m_tRole.m_tColor;
					const ImColor tFillColor = ColorByteToFloat(tCardColor.Lerp(Vars::Menu::Theme::Background.Value, 0.55f, LerpEnum::NoAlpha));
					const ImColor tBorderColor = ColorByteToFloat(tCardColor);
					const ImU32 uFillColor = ImGui::ColorConvertFloat4ToU32(tFillColor.Value);
					const ImU32 uBorderColor = ImGui::ColorConvertFloat4ToU32(tBorderColor.Value);
					const float flAvatarSize = H::Draw.Scale(40);
					const float flPadding = H::Draw.Scale(8);
					const float flTextOffset = flPadding * 2 + flAvatarSize;

					ImVec2 vOriginalPos = { !x ? GetStyle().WindowPadding.x : GetWindowWidth() / 2 + GetStyle().WindowPadding.x / 2, H::Draw.Scale(35 + 64 * y) };
					float flWidth = GetWindowWidth() / 2 - GetStyle().WindowPadding.x * 1.5f;
					float flHeight = H::Draw.Scale(56);
					ImVec2 vDrawPos = GetDrawPos() + vOriginalPos;
					auto pDrawList = GetWindowDrawList();
					pDrawList->AddRectFilled(vDrawPos, { vDrawPos.x + flWidth, vDrawPos.y + flHeight }, uFillColor, H::Draw.Scale(4));
					pDrawList->AddRect(vDrawPos, { vDrawPos.x + flWidth, vDrawPos.y + flHeight }, uBorderColor, H::Draw.Scale(4), ImDrawFlags_None, H::Draw.Scale());

					ImVec2 vAvatarPos = { vOriginalPos.x + flPadding, vOriginalPos.y + (flHeight - flAvatarSize) / 2 };
					if (ImTextureID pAvatar = GetAvatarTexture(tEntry.m_uAccountID))
					{
						SetCursorPos(vAvatarPos);
						Image(pAvatar, { flAvatarSize, flAvatarSize });
					}
					else
					{
						ImVec2 vAvatarDrawPos = GetDrawPos() + vAvatarPos;
						ImVec2 vAvatarDrawEnd = { vAvatarDrawPos.x + flAvatarSize, vAvatarDrawPos.y + flAvatarSize };
						const ImColor tAvatarBg = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp(tCardColor, 0.25f, LerpEnum::NoAlpha));
						pDrawList->AddRectFilled(vAvatarDrawPos, vAvatarDrawEnd, ImGui::ColorConvertFloat4ToU32(tAvatarBg.Value), H::Draw.Scale(4));
						char cInitial = '?';
						for (char cChar : tEntry.m_sDisplayName)
						{
							if (std::isalpha(static_cast<unsigned char>(cChar)))
							{
								cInitial = static_cast<char>(std::toupper(static_cast<unsigned char>(cChar)));
								break;
							}
						}
						const std::string sInitial(1, cInitial);
						const ImVec2 vTextSize = F::Render.FontBold->CalcTextSizeA(F::Render.FontBold->LegacySize, FLT_MAX, 0.f, sInitial.c_str());
						const ImVec2 vTextPos = { vAvatarDrawPos.x + (flAvatarSize - vTextSize.x) / 2, vAvatarDrawPos.y + (flAvatarSize - vTextSize.y) / 2 };
						const auto tBg = Vars::Menu::Theme::Background.Value;
						const float flLuma = 0.2126f * (tBg.r / 255.f) + 0.7152f * (tBg.g / 255.f) + 0.0722f * (tBg.b / 255.f);
						const ImU32 uInitialColor = flLuma < 0.5f ? IM_COL32(240, 240, 240, 230) : IM_COL32(20, 20, 20, 230);
						pDrawList->AddText(F::Render.FontBold, F::Render.FontBold->LegacySize, vTextPos, uInitialColor, sInitial.c_str());
					}

					const float flAvailableWidth = flWidth - flTextOffset - H::Draw.Scale(28);
					SetCursorPos({ vOriginalPos.x + flTextOffset, vOriginalPos.y + H::Draw.Scale(6) });
					FText(TruncateText(tEntry.m_sDisplayName, flAvailableWidth, F::Render.FontBold).c_str(), {}, 0, F::Render.FontBold);
					SetCursorPos({ vOriginalPos.x + flTextOffset, vOriginalPos.y + H::Draw.Scale(24) });
					PushStyleColor(ImGuiCol_Text, ColorByteToVec(tCardColor));
					FText(TruncateText(std::format("Role: {}", tEntry.m_sRoleName.empty() ? "Unmarked" : tEntry.m_sRoleName), flAvailableWidth).c_str());
					PopStyleColor();
					SetCursorPos({ vOriginalPos.x + flTextOffset, vOriginalPos.y + H::Draw.Scale(38) });
					PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
					FText(TruncateText(std::format("Alias: {}", tEntry.m_sAlias.empty() ? "none" : tEntry.m_sAlias), flAvailableWidth).c_str());
					PopStyleColor();

					SetCursorPos({ vOriginalPos.x + flWidth - H::Draw.Scale(24), vOriginalPos.y + H::Draw.Scale(4) });
					const bool bClearRole = IconButton(ICON_MD_DELETE);

					SetCursorPos(vOriginalPos);
					Button(std::format("##Marked{}", tEntry.m_uAccountID).c_str(), { flWidth, flHeight });
					const bool bCardHovered = IsItemHovered();

					if (bClearRole)
					{
						F::PlayerUtils.SetPlayerRole(tEntry.m_uAccountID, -1, true, tEntry.m_sDisplayName.c_str());
						PopID();
						return;
					}

					if (bCardHovered && IsMouseReleased(ImGuiMouseButton_Right))
						OpenPopup(std::format("MarkedMenu{}", tEntry.m_uAccountID).c_str());

					if (FBeginPopup(std::format("MarkedMenu{}", tEntry.m_uAccountID).c_str()))
					{
						PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(12), H::Draw.Scale(8) });
						PushStyleVar(ImGuiStyleVar_WindowRounding, H::Draw.Scale(8));
						PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(4), H::Draw.Scale(4) });
						PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
						PushStyleColor(ImGuiCol_PopupBg, F::Render.Background0.Value);
						PushStyleColor(ImGuiCol_Border, F::Render.Background2.Value);
						PushStyleColor(ImGuiCol_Header, F::Render.Background1.Value);

						const ImVec4 tAccent = F::Render.Accent.Value;
						const ImVec4 tDanger = ColorByteToFloat(Color_t{ 255, 120, 120, 255 });
						int iPopupRow = 0;

						PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
						TextUnformatted("Steam");
						PopStyleColor();

						fPopupSelectable(std::format("Marked{}", tEntry.m_uAccountID), iPopupRow, "Profile", ICON_MD_PERSON, tAccent, [&]
						{
							I::SteamFriends->ActivateGameOverlayToUser("steamid", CSteamID(tEntry.m_uAccountID, k_EUniversePublic, k_EAccountTypeIndividual));
						});
						fPopupSelectable(std::format("Marked{}", tEntry.m_uAccountID), iPopupRow, "History", ICON_MD_HISTORY, tAccent, [&]
						{
							I::SteamFriends->ActivateGameOverlayToWebPage(std::format("https://steamhistory.net/id/{}", CSteamID(tEntry.m_uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64()).c_str());
						});
						fPopupSelectable(std::format("Marked{}", tEntry.m_uAccountID), iPopupRow, "Refetch profile", ICON_MD_SYNC, tAccent, [&]
						{
							ResetAvatarTexture(tEntry.m_uAccountID);
							F::SteamProfileCache.Invalidate(tEntry.m_uAccountID);
							F::SteamProfileCache.TouchAvatar(tEntry.m_uAccountID);
							const auto uSteamID64 = CSteamID(tEntry.m_uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
							SDK::Output("steamwebapi", std::format("Queued profile refetch for {}", uSteamID64).c_str(), { 175, 150, 255, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
						});

						Dummy({ 0, H::Draw.Scale(4) });
						PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
						TextUnformatted("Alias");
						PopStyleColor();

						static std::unordered_map<uint32_t, std::string> sAliasBuffers;
						auto& sAliasBuffer = sAliasBuffers[tEntry.m_uAccountID];
						if (sAliasBuffer.empty())
							sAliasBuffer = tEntry.m_sAlias;
						bool bAliasSubmit = FInputText("Alias...", sAliasBuffer, H::Draw.Scale(284), ImGuiInputTextFlags_EnterReturnsTrue);
						if (!IsItemFocused() && !IsItemActive())
							sAliasBuffer = tEntry.m_sAlias;
						if (bAliasSubmit)
						{
							auto bHasAlias = F::PlayerUtils.m_mPlayerAliases.contains(tEntry.m_uAccountID);
							if (sAliasBuffer.empty())
							{
								if (bHasAlias)
								{
									F::Output.AliasChanged(tEntry.m_sDisplayName.c_str(), "Removed", F::PlayerUtils.m_mPlayerAliases[tEntry.m_uAccountID].c_str());
									F::PlayerUtils.m_mPlayerAliases.erase(tEntry.m_uAccountID);
									F::PlayerUtils.m_bSave = true;
								}
							}
							else
							{
								F::PlayerUtils.m_mPlayerAliases[tEntry.m_uAccountID] = sAliasBuffer;
								F::PlayerUtils.m_bSave = true;
								F::Output.AliasChanged(tEntry.m_sDisplayName.c_str(), bHasAlias ? "Changed" : "Added", sAliasBuffer.c_str());
							}
						}

						Dummy({ 0, H::Draw.Scale(4) });
						PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
						TextUnformatted("Role");
						PopStyleColor();

						fPopupSelectable(std::format("MarkedRole{}", tEntry.m_uAccountID), iPopupRow, "Clear role", ICON_MD_DELETE, tDanger, [&]
						{
							F::PlayerUtils.SetPlayerRole(tEntry.m_uAccountID, -1, true, tEntry.m_sDisplayName.c_str());
						});

						for (int iID = 0; iID < F::PlayerUtils.m_vTags.size(); iID++)
						{
							auto& tTag = F::PlayerUtils.m_vTags[iID];
							if (!tTag.m_bAssignable || tTag.m_bLabel)
								continue;

							std::string sLabel = tTag.m_sName;
							if (std::find(tEntry.m_vRoleTags.begin(), tEntry.m_vRoleTags.end(), iID) != tEntry.m_vRoleTags.end() && tEntry.m_tRole.m_sName == tTag.m_sName)
								sLabel += " (current)";

							fPopupSelectable(std::format("MarkedRole{}", tEntry.m_uAccountID), iPopupRow, sLabel, ICON_MD_LABEL, ColorByteToVec(tTag.m_tColor), [&]
							{
								F::PlayerUtils.SetPlayerRole(tEntry.m_uAccountID, iID, true, tEntry.m_sDisplayName.c_str());
							});
						}

						PopStyleColor(3);
						PopStyleVar(4);
						EndPopup();
					}

					PopID();
				};

				int iBlu = 0, iRed = 0;
				for (size_t i = 0; i < vMarkedPlayers.size(); i++)
				{
					int x = int(i % 2);
					int y = int(i / 2);
					drawMarkedPlayer(vMarkedPlayers[i], x, y);
					if (!x)
						iBlu++;
					else
						iRed++;
				}
				SetCursorPos({ 0, H::Draw.Scale(36 + 64 * std::max(iBlu, iRed)) });
				DebugDummy({ 0, H::Draw.Scale(8) });
			}
		} EndSection();
		{
			PushDisabled(F::PlayerUtils.m_bLoad);
			{
				SetCursorPosY(GetCursorPosY() - H::Draw.Scale(8));
				if (FButton(ICON_MD_SYNC, FButtonEnum::None, { 30, 30 }, 0, F::Render.IconFont))
					F::PlayerUtils.m_bLoad = true;

				if (FButton(ICON_MD_FOLDER, FButtonEnum::Fit | FButtonEnum::SameLine, { 30, 30 }, 0, F::Render.IconFont))
					ShellExecuteA(NULL, NULL, F::Configs.m_sCorePath.c_str(), NULL, NULL, SW_SHOWNORMAL);

				if (FButton("Export", FButtonEnum::Fit | FButtonEnum::SameLine))
				{
					// this should be up2date anyways
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
						SDK::Output("Amalgam", "Copied playerlist to clipboard", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
					}
				}

				{
					static std::vector<PriorityLabel_t> vTags = {};
					static std::unordered_map<uint32_t, std::vector<int>> mPlayerTags = {};
					static std::unordered_map<uint32_t, std::string> mPlayerAliases = {};
					static std::unordered_map<int, int> mAs = {};

					if (FButton("Import", FButtonEnum::Fit | FButtonEnum::SameLine))
					{
						try
						{
							// will not directly support older tag systems
							boost::property_tree::ptree tRead;
							std::stringstream ssStream;
							ssStream << SDK::GetClipboard();
							read_json(ssStream, tRead);

							mPlayerTags.clear();
							mPlayerAliases.clear();
							mAs.clear();
							vTags = {
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
									if (iID > -1 && iID < vTags.size())
									{
										vTags[iID].m_sName = tTag.m_sName;
										vTags[iID].m_tColor = tTag.m_tColor;
										vTags[iID].m_iPriority = tTag.m_iPriority;
										vTags[iID].m_iFollowPriority = tTag.m_iFollowPriority;
										vTags[iID].m_iVotePriority = tTag.m_iVotePriority;
										vTags[iID].m_bLabel = tTag.m_bLabel;
									}
									else
										vTags.push_back(tTag);
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

										if (!F::PlayerUtils.HasTag(uAccountID, iID, mPlayerTags))
											F::PlayerUtils.AddTag(uAccountID, iID, false, "", mPlayerTags);
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
										mPlayerAliases[uAccountID] = sAlias;
								}
							}

							for (int i = 0; i < vTags.size(); i++)
							{
								if (vTags[i].m_bAssignable)
								{
									if (F::PlayerUtils.IndexToTag(i) <= 0)
										mAs[i] = i;
									else
										mAs[i] = -1;
								}
							}
							OpenPopup("ImportPlayerlist");
						}
						catch (...)
						{
							SDK::Output("Amalgam", "Failed to import playerlist", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
						}
					}

					SetNextWindowSize({ H::Draw.Scale(300), 0 });
					if (FBeginPopupModal("ImportPlayerlist"))
					{
						FText("Import");
						FText("As", {}, FTextEnum::Right | FTextEnum::SameLine);

						for (int i = 0; i < vTags.size(); i++)
						{
							if (!vTags[i].m_bAssignable)
								continue;

							auto& iIDTo = mAs[i];

							ImVec2 vOriginalPos = GetCursorPos();
							PushStyleColor(ImGuiCol_Text, ColorByteToInt(vTags[i].m_tColor));
							SetCursorPos(vOriginalPos + ImVec2(H::Draw.Scale(8), H::Draw.Scale(5)));
							FText(vTags[i].m_sName.c_str());
							PopStyleColor();
							SetCursorPos(vOriginalPos - ImVec2(0, H::Draw.Scale(8))); DebugDummy({ GetWindowWidth() - GetStyle().WindowPadding.x * 2, H::Draw.Scale(32) });

							std::vector<const char*> vEntries = { "None" };
							std::vector<int> vValues = { 0 };
							for (int i = 0; i < F::PlayerUtils.m_vTags.size(); i++)
							{
								if (F::PlayerUtils.m_vTags[i].m_bAssignable)
								{
									vEntries.push_back(F::PlayerUtils.m_vTags[i].m_sName.c_str());
									vValues.push_back(i + 1);
								}
							}
							PushTransparent(iIDTo == -1);
							{
								int iTo = iIDTo + 1;
								FDropdown(std::format("##{}", i).c_str(), &iTo, vEntries, vValues, FSliderEnum::Right);
								iIDTo = iTo - 1;
							}
							PopTransparent();
						}

						if (FButton("Import", FButtonEnum::Left))
						{
							for (auto& [uAccountID, vTags] : mPlayerTags)
							{
								for (auto& iTag : vTags)
								{
									int iID = mAs.contains(iTag) ? mAs[iTag] : -1;
									if (iID != -1 && !F::PlayerUtils.HasTag(uAccountID, iID))
										F::PlayerUtils.AddTag(uAccountID, iID, false);
								}
							}
							for (auto& [uAccountID, sAlias] : mPlayerAliases)
							{
								if (!F::PlayerUtils.m_mPlayerAliases.contains(uAccountID))
									F::PlayerUtils.m_mPlayerAliases[uAccountID] = sAlias;
							}

							F::PlayerUtils.m_bSave = true;
							SDK::Output("Amalgam", "Imported playerlist", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);

							CloseCurrentPopup();
						}
						if (FButton("Cancel", FButtonEnum::Right | FButtonEnum::SameLine))
							CloseCurrentPopup();

						EndPopup();
					}
				}

				if (FButton("Backup", FButtonEnum::Fit | FButtonEnum::SameLine))
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
						SDK::Output("Amalgam", "Saved backup playerlist", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
					}
					catch (...)
					{
						SDK::Output("Amalgam", "Failed to backup playerlist", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
					}
				}
			}
			PopDisabled();
		}
		break;
	}
	// Settings
	case 1:
	{
		if (BeginTable("ConfigSettingsTable", 2))
		{
			/* Column 1 */
			TableNextColumn();
			{
				if (Section("Logging"))
				{
					FDropdown(Vars::Logging::Logs);
					FDropdown(Vars::Logging::NotificationPosition);
					FSlider(Vars::Logging::NotificationTime);
					FSlider(Vars::Logging::MaxNotifications);
				} EndSection();
				if (Section("Cheat Detection"))
				{
					FDropdown(Vars::CheatDetection::Methods);
					PushTransparent(!Vars::CheatDetection::DetectionsRequired.Value);
					{
						FSlider(Vars::CheatDetection::DetectionsRequired);
					}
					PopTransparent();
					PushTransparent(!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::PacketChoking));
					{
						FSlider(Vars::CheatDetection::MinChoking);
					}
					PopTransparent();
					PushTransparent(!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::AimFlicking));
					{
						FSlider(Vars::CheatDetection::MinFlick, FSliderEnum::Left);
						FSlider(Vars::CheatDetection::MaxNoise, FSliderEnum::Right);
					}
					PopTransparent();
				} EndSection();
			}
			/* Column 2 */
			TableNextColumn();
			{
				if (Section("Log options"))
				{
					PushTransparent(!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::VoteStart));
					{
						FDropdown(Vars::Logging::VoteStart::LogTo);
					}
					PopTransparent();
					PushTransparent(!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::VoteCast));
					{
						FDropdown(Vars::Logging::VoteCast::LogTo);
					}
					PopTransparent();
					PushTransparent(!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::ClassChanges));
					{
						FDropdown(Vars::Logging::ClassChange::LogTo);
					}
					PopTransparent();
					PushTransparent(!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Damage));
					{
						FDropdown(Vars::Logging::Damage::LogTo);
					}
					PopTransparent();
					PushTransparent(!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::CheatDetection));
					{
						FDropdown(Vars::Logging::CheatDetection::LogTo);
					}
					PopTransparent();
					PushTransparent(!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Tags));
					{
						FDropdown(Vars::Logging::Tags::LogTo);
					}
					PopTransparent();
					PushTransparent(!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Aliases));
					{
						FDropdown(Vars::Logging::Aliases::LogTo);
					}
					PopTransparent();
					PushTransparent(!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Resolver));
					{
						FDropdown(Vars::Logging::Resolver::LogTo);
					}
					PopTransparent();
				} EndSection();
			}
			EndTable();
		}
		break;
	}
	// Output
	case 2:
	{
		if (Section("##Output", false, GetWindowHeight() - GetStyle().WindowPadding.y * 2))
		{
			bool bClear = false;

			for (auto& tOutput : m_vOutput)
			{
				ImVec2 vOriginalPos = GetCursorPos();
				size_t iLines = 1;

				float flWidth = GetWindowWidth() - GetStyle().WindowPadding.x * 2;
				if (!tOutput.m_sFunction.empty())
				{
					float flTitleWidth = 0.f;

					auto vWrapped = WrapText(tOutput.m_sFunction, flWidth);
					if (!vWrapped.empty())
					{
						PushStyleColor(ImGuiCol_Text, ColorByteToInt(tOutput.tAccent));

						for (auto& sText : vWrapped)
							FText(sText.c_str());
						iLines = vWrapped.size();
						flTitleWidth = FCalcTextSize(vWrapped.back().c_str()).x + H::Draw.Scale(4);

						PopStyleColor();
					}

					vWrapped = WrapText(tOutput.m_sLog, { int(flWidth - flTitleWidth), int(flWidth) });
					if (!vWrapped.empty())
					{
						SameLine(flTitleWidth + GetStyle().WindowPadding.x);
						for (auto& sText : vWrapped)
							FText(sText.c_str());
						iLines += vWrapped.size() - 1;
					}
				}
				else
				{
					PushStyleColor(ImGuiCol_Text, ColorByteToInt(tOutput.tAccent));

					auto vWrapped = WrapText(tOutput.m_sLog, flWidth);
					for (auto& sText : vWrapped)
						FText(sText.c_str());
					iLines = vWrapped.size();

					PopStyleColor();
				}

				SetCursorPos(vOriginalPos); DebugDummy({ flWidth, H::Draw.Scale(13) * iLines + GetStyle().WindowPadding.y });

				if (IsItemHovered() && IsMouseReleased(ImGuiMouseButton_Right))
					OpenPopup(std::format("Output{}", tOutput.m_iID).c_str());
				if (FBeginPopup(std::format("Output{}", tOutput.m_iID).c_str()))
				{
					PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), H::Draw.Scale(8) });

					if (FSelectable("Copy"))
						SDK::SetClipboard(std::format("{}{}{}", tOutput.m_sFunction, tOutput.m_sFunction != "" ? " " : "", tOutput.m_sLog));
					if (FSelectable("Clear"))
						bClear = true;

					PopStyleVar();
					EndPopup();
				}
			}

			if (bClear)
				m_vOutput.clear();
		} EndSection();
		break;
	}
	}
}

void CMenu::MenuSettings(int iTab)
{
	using namespace ImGui;

	switch (iTab)
	{
	// Settings
	case 0:
	{
		if (BeginTable("ConfigSettingsTable", 2))
		{
			/*
			if (Section("Config"))
			{
				static int iCurrentType = 0;
				PushFont(F::Render.FontBold);
				FTabs({ "GENERAL", "VISUAL", }, &iCurrentType, { H::Draw.Scale(20), H::Draw.Scale(28) }, { GetWindowWidth(), 0 }, FTabsEnum::AlignReverse | FTabsEnum::Fit);
				SetCursorPosY(GetCursorPosY() - H::Draw.Scale());
				PopFont();

				switch (iCurrentType)
			*/

			auto fDrawConfigs = [](std::string& sStaticName, bool bVisual = false)
			{
				auto& sPath = !bVisual ? F::Configs.m_sConfigPath : F::Configs.m_sVisualsPath;
				auto& sConfig = !bVisual ? F::Configs.m_sCurrentConfig : F::Configs.m_sCurrentVisuals;
				auto sType = !bVisual ? "Config" : "Visual";
				bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;

				FSDropdown("Name", &sStaticName, {}, FSDropdownEnum::AutoUpdate, -H::Draw.Unscale(FCalcTextSize("CREATE").x + H::Draw.Scale(40)) - 44);
				PushDisabled(sStaticName.empty());
				{
					if (FButton("Create", FButtonEnum::Fit | FButtonEnum::SameLine, { 0, 40 }))
					{
						if (!std::filesystem::exists(sPath + sStaticName))
						{
							if (!bVisual)
								F::Configs.SaveConfig(sStaticName);
							else
								F::Configs.SaveVisual(sStaticName);
						}
						sStaticName.clear();
					}
				}
				PopDisabled();

				if (FButton(ICON_MD_FOLDER, FButtonEnum::Fit | FButtonEnum::SameLine, { 40, 40 }, 0, F::Render.IconFont))
					ShellExecuteA(NULL, NULL, sPath.c_str(), NULL, NULL, SW_SHOWNORMAL);

				pRowSizes->clear();

				std::vector<std::pair<std::filesystem::directory_entry, std::string>> vConfigs = {};
				bool bDefaultFound = false;
				for (auto& tEntry : std::filesystem::directory_iterator(sPath))
				{
					if (!tEntry.is_regular_file() || tEntry.path().extension() != F::Configs.m_sConfigExtension)
						continue;

					std::string sName = tEntry.path().filename().string();
					sName.erase(sName.end() - F::Configs.m_sConfigExtension.size(), sName.end());
					if (FNV1A::Hash32(sName.c_str()) == FNV1A::Hash32Const("default"))
						bDefaultFound = true;

					vConfigs.emplace_back(tEntry, sName);
				}
				if (!bVisual)
				{
					if (!bDefaultFound)
						F::Configs.SaveConfig("default");
					std::sort(vConfigs.begin(), vConfigs.end(), [&](const auto& a, const auto& b) -> bool
					{
						// override for default config
						if (FNV1A::Hash32(a.second.c_str()) == FNV1A::Hash32Const("default"))
							return true;
						if (FNV1A::Hash32(b.second.c_str()) == FNV1A::Hash32Const("default"))
							return false;

						return a.second < b.second;
					});
				}

				for (auto& [entry, sConfigName] : vConfigs)
				{
					bool bCurrentConfig = FNV1A::Hash32(sConfigName.c_str()) == FNV1A::Hash32(sConfig.c_str());
					ImVec2 vOriginalPos = GetCursorPos();

					SetCursorPos(vOriginalPos + ImVec2(H::Draw.Scale(2), H::Draw.Scale(9)));
					bool bLoad = IconButton(bCurrentConfig ? ICON_MD_REFRESH : ICON_MD_DOWNLOAD);
					FTooltip(ICON_MD_ADD ICON_MD_FILE_UPLOAD_OFF, bNoSave && IsItemHovered(), 300.f, F::Render.IconFont);

					SetCursorPos({ H::Draw.Scale(43), vOriginalPos.y + H::Draw.Scale(14) });
					TextColored(bCurrentConfig ? F::Render.Active.Value : F::Render.Inactive.Value, TruncateText(sConfigName, GetWindowWidth() - GetStyle().WindowPadding.x * 2 - H::Draw.Scale(80)).c_str());

					int iOffset = 9;
					SetCursorPos({ GetWindowWidth() - H::Draw.Scale(iOffset += 25), vOriginalPos.y + H::Draw.Scale(9) });
					bool bDelete = IconButton(ICON_MD_DELETE);

					SetCursorPos({ GetWindowWidth() - H::Draw.Scale(iOffset += 25), vOriginalPos.y + H::Draw.Scale(9) });
					bool bSave = IconButton(ICON_MD_SAVE);
					FTooltip(ICON_MD_ADD ICON_MD_FILE_DOWNLOAD_OFF, bNoSave && IsItemHovered(), 300.f, F::Render.IconFont);

					if (bLoad)
					{
						if (!bVisual)
							F::Configs.LoadConfig(sConfigName);
						else
							F::Configs.LoadVisual(sConfigName);
					}
					else if (bSave)
					{
						if (!bCurrentConfig || !bVisual && !F::Configs.m_sCurrentVisuals.empty())
							OpenPopup(std::format("Save{}{}", sType, sConfigName).c_str());
						else if (!bVisual)
							F::Configs.SaveConfig(sConfigName);
						else
							F::Configs.SaveVisual(sConfigName);
					}
					else if (bDelete)
						OpenPopup(std::format("Remove{}{}", sType, sConfigName).c_str());

					if (FBeginPopupModal(std::format("Save{}{}", sType, sConfigName).c_str()))
					{
						FText(std::format("Do you really want to override '{}'?", sConfigName).c_str());

						if (FButton("Yes, override", FButtonEnum::Left))
						{
							if (!bVisual)
								F::Configs.SaveConfig(sConfigName);
							else
								F::Configs.SaveVisual(sConfigName);
							CloseCurrentPopup();
						}
						if (FButton("No", FButtonEnum::Right | FButtonEnum::SameLine))
							CloseCurrentPopup();

						EndPopup();
					}
					else if (FBeginPopupModal(std::format("Remove{}{}", sType, sConfigName).c_str()))
					{
						FText(std::format("Do you really want to remove '{}'?", sConfigName).c_str());

						PushDisabled(!bVisual && FNV1A::Hash32(sConfigName.c_str()) == FNV1A::Hash32Const("default"));
						{
							if (FButton("Yes, delete", FButtonEnum::Fit))
							{
								if (!bVisual)
									F::Configs.DeleteConfig(sConfigName);
								else
									F::Configs.DeleteVisual(sConfigName);
								CloseCurrentPopup();
							}
						}
						PopDisabled();
						if (FButton("Yes, reset", FButtonEnum::Fit | FButtonEnum::SameLine))
						{
							if (!bVisual)
								F::Configs.ResetConfig(sConfigName);
							else
								F::Configs.ResetVisual(sConfigName);
							CloseCurrentPopup();
						}
						if (FButton("No", FButtonEnum::Fit | FButtonEnum::SameLine))
							CloseCurrentPopup();

						EndPopup();
					}

					SetCursorPos(vOriginalPos); DebugDummy({ 0, H::Draw.Scale(28) });
				}
				DebugDummy({ 0, H::Draw.Scale(7) });
			};

			/* Column 1 */
			TableNextColumn();
			if (Section("Config"))
			{
				static std::string sStaticName;

				fDrawConfigs(sStaticName);
			} EndSection();
			PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
			SetCursorPosX(GetCursorPosX() + GetStyle().WindowPadding.x);
			FText("Built @ " __DATE__ ", " __TIME__ ", " __CONFIGURATION__);
			//SetCursorPosX(GetCursorPosX() + GetStyle().WindowPadding.x);
			//FText(std::format("Time @ {}, {}", SDK::GetDate(), SDK::GetTime()).c_str());
			PopStyleColor();

			/* Column 2 */
			TableNextColumn();
			if (Section("Visuals"))
			{
				static std::string sStaticName;

				fDrawConfigs(sStaticName, true);
			} EndSection();

			EndTable();
		}
		break;
	}
	// Binds
	case 1:
	{
		if (Section("Settings", 8))
		{
			auto vTable = WidgetTable(3, H::Draw.Scale(16));

			if (BeginWidgetTable(0, vTable))
			{
				FToggle(Vars::Menu::BindWindow);
			} EndChild();

			if (BeginWidgetTable(1, vTable))
			{
				FToggle(Vars::Menu::BindWindowTitle);
			} EndChild();

			if (BeginWidgetTable(2, vTable))
			{
				FToggle(Vars::Menu::MenuShowsBinds);
			} EndChild();
		} EndSection();
		if (Section("Binds"))
		{
			static int iBind = DEFAULT_BIND;
			static Bind_t tBind = {};

			static int bParent = false;
			if (bParent)
				SetMouseCursor(ImGuiMouseCursor_Hand);

			auto vTable = WidgetTable(2, H::Draw.Scale(96));

			if (BeginWidgetTable(0, vTable))
			{
				FSDropdown("Name", &tBind.m_sName, {}, FDropdownEnum::Left | FSDropdownEnum::AutoUpdate);
				{
					auto sParent = bParent ? "..." : tBind.m_iParent != DEFAULT_BIND && tBind.m_iParent < F::Binds.m_vBinds.size() ? F::Binds.m_vBinds[tBind.m_iParent].m_sName : "None";
					if (FButton(std::format("Parent: {}", sParent).c_str(), FButtonEnum::Right | FButtonEnum::SameLine | FButtonEnum::NoUpper, { 0, 40 }))
						bParent = 2;
				}
				FDropdown("Type", &tBind.m_iType, { "Key", "Class", "Weapon type", "Item slot", "Misc" }, {}, FDropdownEnum::Left);
				switch (tBind.m_iType)
				{
				case BindEnum::Key: FDropdown("Behavior", &tBind.m_iInfo, { "Hold", "Toggle", "Double click" }, {}, FDropdownEnum::Right); break;
				case BindEnum::Class: FDropdown("Class", &tBind.m_iInfo, { "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy" }, {}, FDropdownEnum::Right); break;
				case BindEnum::WeaponType: FDropdown("Weapon type", &tBind.m_iInfo, { "Hitscan", "Projectile", "Melee", "Throwable" }, {}, FDropdownEnum::Right); break;
				case BindEnum::ItemSlot: FDropdown("Item slot", &tBind.m_iInfo, { "1", "2", "3", "4", "5", "6", "7", "8", "9" }, {}, FDropdownEnum::Right); break;
				case BindEnum::Misc: FDropdown("Misc", &tBind.m_iInfo, { "Spectated", "Spectated 1st", "Spectated 3rd", "##Divider", "Zoomed", "Aiming" }, {}, FDropdownEnum::Right); break;
				}
			} EndChild();

			if (BeginWidgetTable(1, vTable))
			{
				int iNot = tBind.m_bNot;
				FDropdown("While", &iNot, { "Active", "Not active" }, {}, FDropdownEnum::Left);
				tBind.m_bNot = iNot;
				FDropdown("Visibility", &tBind.m_iVisibility, { "Always", "While active", "Hidden" }, {}, FDropdownEnum::Right);
				if (tBind.m_iType == 0)
					FKeybind("Key", tBind.m_iKey, FButtonEnum::None, { Vars::Menu::PrimaryKey[DEFAULT_BIND], Vars::Menu::SecondaryKey[DEFAULT_BIND] }, { 0, 40 }, -96);

				// create/modify button
				bool bCreate = false, bClear = false, bParent = true;
				if (tBind.m_iParent != DEFAULT_BIND)
					bParent = F::Binds.m_vBinds.size() > tBind.m_iParent;

				SetCursorPos({ GetWindowWidth() - H::Draw.Scale(96), H::Draw.Scale(48) });
				PushDisabled(!bParent || !(tBind.m_iType == BindEnum::Key ? tBind.m_iKey : true));
				{
					bool bMatch = iBind != DEFAULT_BIND && F::Binds.m_vBinds.size() > iBind;
					bCreate = FButton(bMatch ? ICON_MD_SETTINGS : ICON_MD_ADD, FButtonEnum::None, { 40, 40 }, 0, F::Render.IconFont);
				}
				PopDisabled();

				// clear button
				SetCursorPos({ GetWindowWidth() - H::Draw.Scale(48), H::Draw.Scale(48) });
				bClear = FButton(ICON_MD_CLEAR, FButtonEnum::None, { 40, 40 }, 0, F::Render.IconFont);

				if (bCreate)
					F::Binds.AddBind(iBind, tBind);
				if (bCreate || bClear)
				{
					iBind = DEFAULT_BIND;
					tBind = {};
				}
			} EndChild();

			PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
			SetCursorPos({ H::Draw.Scale(13), H::Draw.Scale(124) });
			Divider(); SetCursorPosY(H::Draw.Scale(136));
			FText("Binds");
			SetCursorPosY(GetCursorPosY() - H::Draw.Scale(5));
			PopStyleColor();

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
						//if (iParent == iLayer && iNumber >= i)
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
				int iIndex = floorf((vPos.y - GetCursorPosY() - H::Draw.Scale(4)) / H::Draw.Scale(36));
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

					ImVec2 vOriginalPos = { H::Draw.Scale(8) + H::Draw.Scale(28) * std::min(x, 3), GetCursorPosY() + H::Draw.Scale(8) };

					// background
					float flWidth = GetWindowWidth() - GetStyle().WindowPadding.x * 2 - H::Draw.Scale(28) * std::min(x, 3);
					float flHeight = H::Draw.Scale(28);
					ImVec2 vDrawPos = GetDrawPos() + vOriginalPos;
					if (iBind != _iBind)
						GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + ImVec2(flWidth, flHeight), F::Render.Background1p5, H::Draw.Scale(4));
					else
					{
						ImColor tColor = F::Render.Background1p5L;
						GetWindowDrawList()->AddRectFilled(vDrawPos, vDrawPos + ImVec2(flWidth, flHeight), tColor, H::Draw.Scale(4));

						tColor = ColorByteToFloat((ColorFloatToByte(F::Render.Background1p5)).Lerp({ 127, 127, 127 }, 1.f / 9, LerpEnum::NoAlpha));
						float flInset = H::Draw.Scale(0.5f) - 0.5f;
						GetWindowDrawList()->AddRect(vDrawPos + ImVec2(flInset, flInset), vDrawPos + ImVec2(flWidth - flInset, flHeight - flInset), tColor, H::Draw.Scale(4), ImDrawFlags_None, H::Draw.Scale());
					}

					// text
					if (x > 3)
					{	// don't indent too much
						auto sText = std::format("-> {}", x);
						SetCursorPos(vOriginalPos + ImVec2(-FCalcTextSize(sText.c_str()).x - H::Draw.Scale(10), H::Draw.Scale(7)));
						PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
						FText(sText.c_str());
						PopStyleColor();
					}

					float flTextWidth = flWidth - H::Draw.Scale(127);
					PushTransparent(!F::Binds.WillBeEnabled(_iBind), true);

					SetCursorPos(vOriginalPos + ImVec2(H::Draw.Scale(9), H::Draw.Scale(7)));
					PushStyleColor(ImGuiCol_Text, _tBind.m_bActive ? F::Render.Accent.Value : F::Render.Active.Value);
					FText(TruncateText(_tBind.m_sName, flTextWidth * (1.f / 3) - H::Draw.Scale(20)).c_str());
					PopStyleColor();

					SetCursorPos(vOriginalPos + ImVec2(flTextWidth * (1.f / 3), H::Draw.Scale(7)));
					FText(sType.c_str());

					SetCursorPos(vOriginalPos + ImVec2(flTextWidth * (2.f / 3), H::Draw.Scale(7)));
					FText(sInfo.c_str());

					// buttons
					int iOffset = 1;

					SetCursorPos(vOriginalPos + ImVec2(flWidth - H::Draw.Scale(iOffset += 25), H::Draw.Scale(2)));
					bool bDelete = IconButton(ICON_MD_DELETE);

					SetCursorPos(vOriginalPos + ImVec2(flWidth - H::Draw.Scale(iOffset += 25), H::Draw.Scale(2)));
					if (IconButton(ICON_MD_EDIT))
						CurrentBind = CurrentBind != _iBind ? _iBind : DEFAULT_BIND;

					SetCursorPos(vOriginalPos + ImVec2(flWidth - H::Draw.Scale(iOffset += 25), H::Draw.Scale(2)));
					if (IconButton(!_tBind.m_bNot ? ICON_MD_CODE : ICON_MD_CODE_OFF))
						_tBind.m_bNot = !_tBind.m_bNot;

					PushTransparent(Transparent || _tBind.m_iVisibility == BindVisibilityEnum::Hidden, true);
					SetCursorPos(vOriginalPos + ImVec2(flWidth - H::Draw.Scale(iOffset += 25), H::Draw.Scale(2)));
					if (IconButton(_tBind.m_iVisibility == BindVisibilityEnum::Always ? ICON_MD_VISIBILITY : ICON_MD_VISIBILITY_OFF))
						_tBind.m_iVisibility = (_tBind.m_iVisibility + 1) % 3;
					PopTransparent(1, 1);

					SetCursorPos(vOriginalPos + ImVec2(flWidth - H::Draw.Scale(iOffset += 25), H::Draw.Scale(2)));
					if (IconButton(_tBind.m_bEnabled ? ICON_MD_TOGGLE_ON : ICON_MD_TOGGLE_OFF))
						_tBind.m_bEnabled = !_tBind.m_bEnabled;

					SetCursorPos(vOriginalPos);
					bool bClicked = Button(std::format("##{}", _iBind).c_str(), { flWidth, flHeight });
					bool bPopup = IsItemClicked(ImGuiMouseButton_Right);

					PopTransparent(1, 1);

					if (bClicked)
					{
						if (!bParent)
						{
							iBind = _iBind;
							tBind = _tBind;
						}
						else
						{
							bParent = false;
							tBind.m_iParent = _iBind;

							// make sure bind can't be parented to itself or any of its children
							int _iBind2 = _iBind;
							Bind_t _tBind2;
							while (F::Binds.GetBind(_iBind2, &_tBind2))
							{
								if (_iBind2 == iBind)
									tBind.m_iParent = DEFAULT_BIND;
								_iBind2 = _tBind2.m_iParent;
							}
						}
					}
					else if (bPopup)
						OpenPopup(std::format("RightClicked{}", _iBind).c_str());
					else if (iDragging == -1 && IsItemHovered() && IsMouseDown(ImGuiMouseButton_Left))
						iDragging = _iBind, iLayer = iParent;
					else if (bDelete)
					{
						if (_tBind.m_vVars.size() <= 1 && !F::Binds.HasChildren(_iBind) || U::KeyHandler.Down(VK_SHIFT)) // allow user to quickly remove binds
							F::Binds.RemoveBind(_iBind);
						else
							OpenPopup(std::format("DeleteBind{}", _iBind).c_str());
					}

					if (FBeginPopup(std::format("RightClicked{}", _iBind).c_str()))
					{
						PushStyleVar(ImGuiStyleVar_ItemSpacing, { H::Draw.Scale(8), 0 });

						{
							static std::string sInput = "";

							bool bEnter = FInputText("Name...", sInput, H::Draw.Scale(284), ImGuiInputTextFlags_EnterReturnsTrue);
							if (!IsItemFocused())
								sInput = _tBind.m_sName;
							if (bEnter)
								_tBind.m_sName = sInput;
						}

						FDropdown("Type", &_tBind.m_iType, { "Key", "Class", "Weapon type", "Item slot", "Misc" }, {}, FDropdownEnum::Left);
						switch (_tBind.m_iType)
						{
						case BindEnum::Key: FDropdown("Behavior", &_tBind.m_iInfo, { "Hold", "Toggle", "Double click" }, {}, FDropdownEnum::Right); break;
						case BindEnum::Class: FDropdown("Class", &_tBind.m_iInfo, { "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy" }, {}, FDropdownEnum::Right); break;
						case BindEnum::WeaponType: FDropdown("Weapon type", &_tBind.m_iInfo, { "Hitscan", "Projectile", "Melee", "Throwable" }, {}, FDropdownEnum::Right); break;
						case BindEnum::ItemSlot: FDropdown("Item slot", &_tBind.m_iInfo, { "1", "2", "3", "4", "5", "6", "7", "8", "9" }, {}, FDropdownEnum::Right); break;
						case BindEnum::Misc: FDropdown("Misc", &_tBind.m_iInfo, { "Spectated", "Spectated 1st", "Spectated 3rd", "##Divider", "Zoomed", "Aiming" }, {}, FDropdownEnum::Right); break;
						}
						if (_tBind.m_iType == BindEnum::Key)
							FKeybind("Key", _tBind.m_iKey);

						PopStyleVar();
						EndPopup();
					}
					else if (FBeginPopupModal(std::format("DeleteBind{}", _iBind).c_str()))
					{
						FText(std::format("Do you really want to delete '{}'{}?", _tBind.m_sName, F::Binds.HasChildren(_iBind) ? " and all of its children" : "").c_str());

						if (FButton("Yes", FButtonEnum::Left))
						{
							F::Binds.RemoveBind(_iBind);
							CloseCurrentPopup();

							iBind = DEFAULT_BIND;
							tBind = {};
						}
						if (FButton("No", FButtonEnum::Right | FButtonEnum::SameLine))
							CloseCurrentPopup();

						EndPopup();
					}

					if (iParent != DEFAULT_BIND - 1)
						fGetBinds(_iBind, x + 1);
				}
			};
			fGetBinds(DEFAULT_BIND, 0);

			// this should ideally never happen, but failsafe
			if (F::Binds.m_vBinds.size() > mBinds.size())
			{
				PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
				SetCursorPos({ H::Draw.Scale(13), GetCursorPosY() + H::Draw.Scale(5) });
				FText("Dangling");
				SetCursorPosY(GetCursorPosY() - H::Draw.Scale(5));
				PopStyleColor();

				fGetBinds(DEFAULT_BIND - 1, 0);
			}

			if (bParent == 2) // dumb
				bParent = 1;
			else if (bParent && IsMouseReleased(ImGuiMouseButton_Left))
			{
				bParent = false;
				tBind.m_iParent = DEFAULT_BIND;
			}
		} EndSection();
		break;
	}
	// Materials
	case 2:
	{
		static TextEditor tTextEditor;
		static std::string sCurrentMaterial;
		static bool bLockedMaterial;

		bool bTable = false;
		if (!sCurrentMaterial.empty())
			bTable = BeginTable("MaterialsTable", 2);
		{
			if (bTable)
			{
				TableSetupColumn("MaterialsTable1", ImGuiTableColumnFlags_WidthFixed, H::Draw.Scale(288));
				TableSetupColumn("MaterialsTable2", ImGuiTableColumnFlags_WidthFixed, GetWindowWidth());

				/* Column 1 */
				TableNextColumn();
			}

			if (Section("Manager"))
			{
				static std::string sStaticName;

				FSDropdown("Name", &sStaticName, {}, FSDropdownEnum::AutoUpdate, -H::Draw.Unscale(FCalcTextSize("CREATE").x + H::Draw.Scale(40)) - 44);
				PushDisabled(sStaticName.empty());
				{
					if (FButton("Create", FButtonEnum::Fit | FButtonEnum::SameLine, { 0, 40 }))
					{
						F::Materials.AddMaterial(sStaticName.c_str());
						sStaticName.clear();
					}
				}
				PopDisabled();

				if (FButton(ICON_MD_FOLDER, FButtonEnum::Fit | FButtonEnum::SameLine, { 40, 40 }, 0, F::Render.IconFont))
					ShellExecuteA(NULL, NULL, F::Configs.m_sMaterialsPath.c_str(), NULL, NULL, SW_SHOWNORMAL);

				std::vector<Material_t> vMaterials;
				for (auto& tMaterial : F::Materials.m_mMaterials | std::views::values)
					vMaterials.push_back(tMaterial);

				std::sort(vMaterials.begin(), vMaterials.end(), [&](const auto& a, const auto& b) -> bool
				{
					// override for none material
					if (FNV1A::Hash32(a.m_sName.c_str()) == FNV1A::Hash32Const("None"))
						return true;
					if (FNV1A::Hash32(b.m_sName.c_str()) == FNV1A::Hash32Const("None"))
						return false;

					// keep locked materials higher
					if (a.m_bLocked && !b.m_bLocked)
						return true;
					if (!a.m_bLocked && b.m_bLocked)
						return false;

					return a.m_sName < b.m_sName;
				});

				for (auto& tMaterial : vMaterials)
				{
					ImVec2 vOriginalPos = GetCursorPos();

					SetCursorPos({ H::Draw.Scale(17), vOriginalPos.y + H::Draw.Scale(14) });
					TextColored(tMaterial.m_bLocked ? F::Render.Inactive.Value : F::Render.Active.Value, TruncateText(tMaterial.m_sName, GetWindowWidth() - GetStyle().WindowPadding.x * 2 - H::Draw.Scale(56)).c_str());

					int iOffset = 9;
					if (!tMaterial.m_bLocked)
					{
						SetCursorPos({ GetWindowWidth() - H::Draw.Scale(iOffset += 25), vOriginalPos.y + H::Draw.Scale(9) });
						if (IconButton(ICON_MD_DELETE))
							OpenPopup(std::format("DeleteMat{}", tMaterial.m_sName).c_str());
						if (FBeginPopupModal(std::format("DeleteMat{}", tMaterial.m_sName).c_str()))
						{
							FText(std::format("Do you really want to delete '{}'?", tMaterial.m_sName).c_str());

							if (FButton("Yes", FButtonEnum::Left))
							{
								F::Materials.RemoveMaterial(tMaterial.m_sName.c_str());
								CloseCurrentPopup();
							}
							if (FButton("No", FButtonEnum::Right | FButtonEnum::SameLine))
								CloseCurrentPopup();

							EndPopup();
						}
					}

					SetCursorPos({ GetWindowWidth() - H::Draw.Scale(iOffset += 25), vOriginalPos.y + H::Draw.Scale(9) });
					if (IconButton(ICON_MD_EDIT))
					{
						sCurrentMaterial = tMaterial.m_sName;
						bLockedMaterial = tMaterial.m_bLocked;

						tTextEditor.SetText(F::Materials.GetVMT(FNV1A::Hash32(sCurrentMaterial.c_str())));
						tTextEditor.SetReadOnlyEnabled(bLockedMaterial);
					}

					SetCursorPos(vOriginalPos); DebugDummy({ 0, H::Draw.Scale(28) });
				}
				DebugDummy({ 0, H::Draw.Scale(7) });
			}
			else
				SetScrollY(0);
			EndSection();

			if (bTable)
			{
				/* Column 2 */
				TableNextColumn();
				if (sCurrentMaterial.length())
				{
					SetCursorPosY(GetScrollY() + GetStyle().WindowPadding.y);
					if (Section("Editor", 0, GetWindowHeight() - GetStyle().WindowPadding.y * 2, true))
					{
						// Toolbar
						if (!bLockedMaterial)
						{
							if (FButton("Save", FButtonEnum::Fit))
							{
								auto sText = tTextEditor.GetText();
								F::Materials.EditMaterial(sCurrentMaterial.c_str(), sText.c_str());
							}
							SameLine();
						}
						if (FButton("Close", FButtonEnum::Fit))
							sCurrentMaterial = "";
						SetCursorPosY(H::Draw.Scale(52));
						PushStyleColor(ImGuiCol_Text, F::Render.Inactive.Value);
						FText(std::format("{}: {}", bLockedMaterial ? "Viewing" : "Editing", sCurrentMaterial).c_str(), {}, FTextEnum::Right);
						PopStyleColor();

						// Text editor
						DebugDummy({ 0, H::Draw.Scale(8) });

						tTextEditor.SetLanguage(TextEditor::Language::Cpp());
						TextEditor::Palette tPalette = {{
							F::Render.Active,				// text
							F::Render.Active,				// keyword
							F::Render.Active,				// declaration
							F::Render.Active,				// number
							F::Render.Accent,				// string
							F::Render.Inactive,				// punctuation
							F::Render.Inactive,				// preprocessor
							F::Render.Inactive,				// identifier
							F::Render.Inactive,				// known identifier
							F::Render.Inactive,				// comment
							F::Render.Background1,			// background
							F::Render.Active,				// cursor
							ImColor(F::Render.Inactive.Value.x, F::Render.Inactive.Value.y, F::Render.Inactive.Value.z, 0.5f),	// selection
							ImColor(F::Render.Inactive.Value.x, F::Render.Inactive.Value.y, F::Render.Inactive.Value.z, 0.1f),	// whitespace
							IM_COL32( 70,  70,  70, 255),	// matchingBracketBackground
							IM_COL32(140, 140, 140, 255),	// matchingBracketActive
							IM_COL32(246, 222,  36, 255),	// matchingBracketLevel1
							IM_COL32( 66, 120, 198, 255),	// matchingBracketLevel2
							IM_COL32(213,  96, 213, 255),	// matchingBracketLevel3
							IM_COL32(198,   8,  32, 255),	// matchingBracketError
							F::Render.Inactive,				// line number
							F::Render.Active,				// current line number
						}};
						tTextEditor.SetPalette(tPalette);
						//TextEditor.SetShowLineNumbersEnabled(false);

						PushFont(F::Render.FontMono);
						PushStyleVar(ImGuiStyleVar_ChildBorderSize, H::Draw.Scale(1));
						ImVec2 vDrawPos = GetDrawPos() + GetCursorPos();
						tTextEditor.Render("TextEditor");
						ImVec2 vSize = GetItemRectSize();
						float flInset = H::Draw.Scale(0.5f) - 0.5f;
						GetWindowDrawList()->AddRect(vDrawPos + ImVec2(flInset, flInset), vDrawPos + ImVec2(vSize.x - flInset, vSize.y - flInset), F::Render.Background2, H::Draw.Scale(4), ImDrawFlags_None, H::Draw.Scale());
						PopStyleVar();
						PopFont();
					} EndSection();
				}

				EndTable();
			}
		}
		break;
	}
	// Extra
	case 3:
	{
		if (Section("Functions"))
		{
			if (FButton("Fullupdate", FButtonEnum::Left))
				I::EngineClient->ClientCmd_Unrestricted("cl_fullupdate");
			if (FButton("Retry", FButtonEnum::Right | FButtonEnum::SameLine))
				I::EngineClient->ClientCmd_Unrestricted("retry");
			if (FButton("Console", FButtonEnum::Left))
				I::EngineClient->ClientCmd_Unrestricted("toggleconsole");
			if (FButton("Reload materials", FButtonEnum::Right | FButtonEnum::SameLine) && F::Materials.m_bLoaded)
				F::Materials.ReloadMaterials();

			if (!I::EngineClient->IsConnected())
			{
				bool bItem = false;
				if (FButton("Unlock achievements", FButtonEnum::Left))
					OpenPopup("UnlockAchievements");
				if (FButton("Unlock item achievements", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					OpenPopup("UnlockAchievements");
					bItem = true;
				}
				if (FButton("Lock achievements", FButtonEnum::Left))
					OpenPopup("LockAchievements");
				if (FButton("Lock item achievements", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					OpenPopup("LockAchievements");
					bItem = true;
				}
				if (FBeginPopupModal("UnlockAchievements"))
				{
					FText(std::format("Do you really want to unlock all {}achievements?", (bItem ? "item " : "")).c_str());

					if (FButton("Yes, unlock", FButtonEnum::Left))
					{
						if (bItem) F::Misc.UnlockItemAchievements();
						else F::Misc.UnlockAchievements();
						CloseCurrentPopup();
					}
					if (FButton("No", FButtonEnum::Right | FButtonEnum::SameLine))
						CloseCurrentPopup();

					EndPopup();
				}
				else if (FBeginPopupModal("LockAchievements"))
				{
					FText(std::format("Do you really want to lock all {}achievements?", (bItem ? "item " : "")).c_str());

					if (FButton("Yes, lock", FButtonEnum::Left))
					{
						if (bItem) F::Misc.LockItemAchievements();
						else F::Misc.LockAchievements();
						CloseCurrentPopup();
					}
					if (FButton("No", FButtonEnum::Right | FButtonEnum::SameLine))
						CloseCurrentPopup();

					EndPopup();
				}

			}
		} EndSection();
		if (Section("Debug", 8))
		{
			FToggle(Vars::Debug::Info, FToggleEnum::Left);
			FToggle(Vars::Debug::Logging, FToggleEnum::Right);
			FToggle(Vars::Debug::Options, FToggleEnum::Left);
			FToggle(Vars::Debug::CrashLogging, FToggleEnum::Right);

#ifdef DEBUG_TRACES
			FToggle(Vars::Debug::VisualizeTraces, FToggleEnum::Left);
			FToggle(Vars::Debug::VisualizeTraceHits, FToggleEnum::Right);
#endif
			FToggle(Vars::Config::LoadDebugSettings, FToggleEnum::Left); FTooltip("Load/Save debug settings in this config");
		} EndSection();
		if (Vars::Debug::Options.Value && I::EngineClient->IsConnected())
		{
			if (Section("##Debug"))
			{
				FToggle(Vars::Debug::DrawHitboxes);

				if (FButton("Restore visuals", FButtonEnum::Left))
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
				if (FButton("Clear visuals", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					G::LineStorage.clear();
					G::PathStorage.clear();
					G::BoxStorage.clear();
					G::SphereStorage.clear();
					G::SweptStorage.clear();
					G::TriangleStorage.clear();
				}

				if (FButton("Restore lines", FButtonEnum::Left))
				{
					for (auto& tLine : G::LineStorage)
						tLine.m_flTime = I::GlobalVars->curtime + 60.f;
				}
				if (FButton("Clear lines", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					G::LineStorage.clear();
				}

				if (FButton("Restore paths", FButtonEnum::Left))
				{
					for (auto& tPath : G::PathStorage)
						tPath.m_flTime = I::GlobalVars->curtime + 60.f;
				}
				if (FButton("Clear paths", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					G::PathStorage.clear();
				}

				if (FButton("Restore boxes", FButtonEnum::Left))
				{
					for (auto& tBox : G::BoxStorage)
						tBox.m_flTime = I::GlobalVars->curtime + 60.f;
				}
				if (FButton("Clear boxes", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					G::BoxStorage.clear();
				}

				if (FButton("Restore spheres", FButtonEnum::Left))
				{
					for (auto& tSphere : G::SphereStorage)
						tSphere.m_flTime = I::GlobalVars->curtime + 60.f;
				}
				if (FButton("Clear spheres", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					G::SphereStorage.clear();
				}

				if (FButton("Restore swept", FButtonEnum::Left))
				{
					for (auto& tSwept : G::SweptStorage)
						tSwept.m_flTime = I::GlobalVars->curtime + 60.f;
				}
				if (FButton("Clear swept", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					G::SweptStorage.clear();
				}

				if (FButton("Restore triangles", FButtonEnum::Left))
				{
					for (auto& tTriangle : G::TriangleStorage)
						tTriangle.m_flTime = I::GlobalVars->curtime + 60.f;
				}
				if (FButton("Clear triangles", FButtonEnum::Right | FButtonEnum::SameLine))
				{
					G::TriangleStorage.clear();
				}
			} EndSection();
		}
		/*
		if (Vars::Debug::Options.Value)
		{
			if (Section("Convar spoofer"))
			{
				static std::string sName = "", sValue = "";

				FSDropdown("Convar", &sName, {}, FDropdownEnum::Left);
				FSDropdown("Value", &sValue, {}, FDropdownEnum::Right);
				if (FButton("Send"))
				{
					if (auto pNetChan = static_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo()))
					{
						SDK::Output("Convar", std::format("Sent {} as {}", sName, sValue).c_str(), Vars::Menu::Theme::Accent.Value);
						NET_SetConVar cmd(sName.c_str(), sValue.c_str());
						pNetChan->SendNetMsg(cmd);

						sName = sValue = "";
					}
				}
			} EndSection();
		}
		*/
#ifdef WORLD_DEBUG
		if (Section("World"))
		{
			FDropdown(Vars::World::Faces, FDropdownEnum::Left);
			FDropdown(Vars::World::Draw, FDropdownEnum::Right);
			FSlider(Vars::World::Offset, FSliderEnum::Left);
			FSlider(Vars::World::Resize, FSliderEnum::Right);
			FColorPicker(Vars::World::BoxBrush, FColorPickerEnum::Left);
			FColorPicker(Vars::World::PlaneBrush, FColorPickerEnum::Right);
			FColorPicker(Vars::World::Displacement, FColorPickerEnum::Left);
			FColorPicker(Vars::World::Prop, FColorPickerEnum::Right);
			FColorPicker(Vars::World::Entity, FColorPickerEnum::Left);
		} EndSection();
#endif
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
			if (FButton("Disable all", FButtonEnum::Right | FButtonEnum::SameLine))
				SetAll(false);

			int i = 0; for (auto& pBase : G::Vars)
			{
				if (std::string(pBase->Name()).find("Vars::Hooks::") == std::string::npos)
					continue;

				FToggle(*pBase->As<bool>(), !(i % 2) ? FToggleEnum::Left : FToggleEnum::Right);
				i++;
			}
		} EndSection();
#endif
		break;
	}
	}
}

void CMenu::MenuSearch(std::string sSearch)
{
	using namespace ImGui;

	if (sSearch.empty())
		return;

	static std::vector<BaseVar*> vVars = {}; // don't string search every single frame

	static uint32_t uStaticHash = 0;
	if (const uint32_t uCurrHash = FNV1A::Hash32(sSearch.c_str());
		uCurrHash != uStaticHash)
	{
		std::string sSearch2 = sSearch;
		std::transform(sSearch2.begin(), sSearch2.end(), sSearch2.begin(), ::tolower);

		vVars.clear();
		for (auto& pBase : G::Vars)
		{
			if (!Vars::Debug::Options[DEFAULT_BIND] && pBase->m_iFlags & DEBUGVAR)
				continue;

			std::vector<const char*> vSearch = { pBase->Name(), pBase->Section() };
			vSearch.insert(vSearch.end(), pBase->m_vNames.begin(), pBase->m_vNames.end());
			vSearch.insert(vSearch.end(), pBase->m_vValues.begin(), pBase->m_vValues.end());
			for (auto pSearch : vSearch)
			{
				std::string sSearch3 = pSearch;
				if (auto iFind = sSearch3.find("Vars::"); iFind != std::string::npos)
					sSearch3 = sSearch3.replace(iFind, strlen("Vars::"), "");
				if (auto iFind = sSearch3.find("##"); iFind != std::string::npos)
					sSearch3 = sSearch3.replace(iFind, strlen("##"), "");
				std::transform(sSearch3.begin(), sSearch3.end(), sSearch3.begin(), ::tolower);
				if (sSearch3.find(sSearch2) != std::string::npos)
				{
					vVars.push_back(pBase);
					break;
				}
			}
		}

		uStaticHash = uCurrHash;
	}

	if (vVars.empty())
		return;

	uint32_t uLastSection = 0;
	int i = 0; for (auto pBase : vVars) // possibly implement tablelike visuals, do away with left right, just switch if current side is higher than other?
	{
		int iWidgetEnum = WidgetEnum::Invalid, iTypeEnum = WidgetEnum::Invalid;
		if (auto pVar = pBase->As<bool>())
			iWidgetEnum = iTypeEnum = WidgetEnum::FToggle;
		else if (auto pVar = pBase->As<int>())
		{
			if (!pVar->m_vValues.empty()
				|| FNV1A::Hash32(pVar->Name()) == FNV1A::Hash32Const("Vars::ESP::ActiveGroups"))
				iWidgetEnum = iTypeEnum = WidgetEnum::FDropdown;
			else if (pVar->m_sExtra)
				iWidgetEnum = WidgetEnum::FISlider, iTypeEnum = WidgetEnum::FSlider;
			else
				iWidgetEnum = iTypeEnum = WidgetEnum::FKeybind;
		}
		else if (auto pVar = pBase->As<float>())
			iWidgetEnum = WidgetEnum::FFSlider, iTypeEnum = WidgetEnum::FSlider;
		else if (auto pVar = pBase->As<IntRange_t>())
			iWidgetEnum = WidgetEnum::FIRSlider, iTypeEnum = WidgetEnum::FSlider;
		else if (auto pVar = pBase->As<FloatRange_t>())
			iWidgetEnum = WidgetEnum::FFRSlider, iTypeEnum = WidgetEnum::FSlider;
		else if (auto pVar = pBase->As<std::string>())
			iWidgetEnum = WidgetEnum::FSDropdown, iTypeEnum = WidgetEnum::FDropdown;
		else if (auto pVar = pBase->As<std::vector<std::pair<std::string, Color_t>>>())
			iWidgetEnum = WidgetEnum::FMDropdown, iTypeEnum = WidgetEnum::FDropdown;
		else if (auto pVar = pBase->As<Color_t>())
			iWidgetEnum = WidgetEnum::FColorPicker, iTypeEnum = WidgetEnum::FToggle;
		else if (auto pVar = pBase->As<Gradient_t>())
			iWidgetEnum = WidgetEnum::FGColorPicker, iTypeEnum = WidgetEnum::FToggle;
		else
			continue;

		uint32_t uSection = FNV1A::Hash32(pBase->Section());
		if (uSection != uLastSection)
		{
			if (uLastSection)
				EndSection();
			Section(std::format("{}## {}", pBase->Section(), pBase->Name()).c_str());
			i = 0;
		}
		uLastSection = uSection;

		static int iLastEnum = WidgetEnum::Invalid;
		if (!i || iTypeEnum != iLastEnum)
		{
			if (!i)
			{
				switch (iWidgetEnum)
				{
				case WidgetEnum::FToggle:
				case WidgetEnum::FISlider:
				case WidgetEnum::FFSlider:
				case WidgetEnum::FIRSlider:
				case WidgetEnum::FFRSlider:
				case WidgetEnum::FColorPicker:
				case WidgetEnum::FGColorPicker:
					DebugDummy({ 0, H::Draw.Scale(8) });
				}
				i = 2;
			}
			else if (iTypeEnum == WidgetEnum::FToggle && iLastEnum == WidgetEnum::FSlider && (i % 2))
			{
				SetCursorPos({ GetWindowWidth() / 2 + GetStyle().WindowPadding.x / 2, GetRowPos() + H::Draw.Scale(8) });
				i = 0;
			}
			else if (iTypeEnum == WidgetEnum::FSlider && iLastEnum == WidgetEnum::FDropdown && (i % 2) && !pRowSizes->empty())
			{
				auto& tRow = pRowSizes->front();
				tRow.m_vPos.y += H::Draw.Scale(13), tRow.m_vSize.y -= H::Draw.Scale(13);
				SetCursorPos({ GetWindowWidth() / 2 + GetStyle().WindowPadding.x / 2, GetRowPos() });
				i = 0;
			}
			else
				i = 2;
		}
		iLastEnum = iTypeEnum;

		int iOverride = -1;
		switch (iWidgetEnum)
		{
		case WidgetEnum::FToggle:
		{
			auto pVar = pBase->As<bool>();
			if (FToggle(*pVar, !(i % 2) ? FToggleEnum::Left : FToggleEnum::Right, nullptr, iOverride/*, iOverride*/))
			{
				if (FNV1A::Hash32(pVar->Name()) == FNV1A::Hash32Const("Vars::Debug::Options"))
					uStaticHash = 0;
			}
			break;
		}
		case WidgetEnum::FISlider:
		{
			auto pVar = pBase->As<int>();
			const char* sFormat = nullptr;
			switch (FNV1A::Hash32(pVar->Name()))
			{
			case FNV1A::Hash32Const("Vars::Aimbot::Projectile::SplashAirCount"):
				if (!pVar->Map[DEFAULT_BIND])
					sFormat = "random";
			}
			FSlider(*pVar, !(i % 2) ? FSliderEnum::Left : FSliderEnum::Right, sFormat, nullptr, iOverride/*, iOverride*/);
			break;
		}
		case WidgetEnum::FFSlider:
		{
			auto pVar = pBase->As<float>();
			const char* sFormat = nullptr;
			switch (FNV1A::Hash32(pVar->Name()))
			{
			case FNV1A::Hash32Const("Vars::Visuals::Prediction::PlayerDrawDuration"):
			case FNV1A::Hash32Const("Vars::Visuals::Prediction::ProjectileDrawDuration"):
				if (!pVar->Map[DEFAULT_BIND])
					sFormat = "timed";
				break;
			case FNV1A::Hash32Const("Vars::Aimbot::Projectile::SplashRotateX"):
			case FNV1A::Hash32Const("Vars::Aimbot::Projectile::SplashRotateY"):
				if (pVar->Map[DEFAULT_BIND] < 0.f)
					sFormat = "random";
			}
			FSlider(*pVar, !(i % 2) ? FSliderEnum::Left : FSliderEnum::Right, sFormat, nullptr, iOverride/*, iOverride*/);
			break;
		}
		case WidgetEnum::FIRSlider:
		{
			auto pVar = pBase->As<IntRange_t>();
			FSlider(*pVar, !(i % 2) ? FSliderEnum::Left : FSliderEnum::Right, nullptr, nullptr, iOverride/*, iOverride*/);
			break;
		}
		case WidgetEnum::FFRSlider:
		{
			auto pVar = pBase->As<FloatRange_t>();
			FSlider(*pVar, !(i % 2) ? FSliderEnum::Left : FSliderEnum::Right, nullptr, nullptr, iOverride/*, iOverride*/);
			break;
		}
		case WidgetEnum::FDropdown:
		{
			auto pVar = pBase->As<int>();
			FDropdown(*pVar, !(i % 2) ? FDropdownEnum::Left : FDropdownEnum::Right, 0, nullptr, iOverride/*, iOverride*/);
			break;
		}
		case WidgetEnum::FSDropdown:
		{
			auto pVar = pBase->As<std::string>();
			FSDropdown(*pVar, !(i % 2) ? FDropdownEnum::Left : FDropdownEnum::Right, 0, nullptr, iOverride/*, iOverride*/);
			break;
		}
		case WidgetEnum::FMDropdown:
		{
			auto pVar = pBase->As<std::vector<std::pair<std::string, ChamsMaterial_t>>>();
			FMDropdown(*pVar, !(i % 2) ? FDropdownEnum::Left : FDropdownEnum::Right, 0, nullptr, iOverride/*, iOverride*/);
			break;
		}
		case WidgetEnum::FColorPicker:
		{
			auto pVar = pBase->As<Color_t>();
			FColorPicker(*pVar, !(i % 2) ? FColorPickerEnum::Left : FColorPickerEnum::Right, {}, { H::Draw.Scale(12), H::Draw.Scale(12) }, {}, nullptr, iOverride, iOverride);
			break;
		}
		case WidgetEnum::FGColorPicker:
		{
			auto pVar = pBase->As<Gradient_t>();
			FColorPicker(*pVar, true, !(i % 2) ? FColorPickerEnum::Left : FColorPickerEnum::Right, {}, { H::Draw.Scale(12), H::Draw.Scale(12) }, {}, nullptr, iOverride/*, iOverride*/);
			FColorPicker(*pVar, false, !(++i % 2) ? FColorPickerEnum::Left : FColorPickerEnum::Right, {}, { H::Draw.Scale(12), H::Draw.Scale(12) }, {}, nullptr, iOverride/*, iOverride*/);
			break;
		}
		case WidgetEnum::FKeybind:
		{
			auto pVar = pBase->As<int>();
			std::vector<int> vIgnore;
			switch (FNV1A::Hash32(pVar->Name()))
			{
			case FNV1A::Hash32Const("Vars::Menu::PrimaryKey"):
				vIgnore = { Vars::Menu::SecondaryKey[DEFAULT_BIND], VK_LBUTTON, VK_RBUTTON };
				break;
			case FNV1A::Hash32Const("Vars::Menu::SecondaryKey"):
				vIgnore = { Vars::Menu::PrimaryKey[DEFAULT_BIND], VK_LBUTTON, VK_RBUTTON };
				break;
			default:
				vIgnore = { Vars::Menu::PrimaryKey[DEFAULT_BIND], Vars::Menu::SecondaryKey[DEFAULT_BIND] };
			}
			FKeybind(iOverride != -1 ? pVar->m_vNames[iOverride] : pVar->m_vNames.front(), pVar->Map[DEFAULT_BIND], !(i % 2) ? FButtonEnum::Left : FButtonEnum::Right | FButtonEnum::SameLine, vIgnore);
			break;
		}
		}

		if (iOverride != -2)
			i += i > 1 ? 1 : 2;
	}
	if (uLastSection)
		EndSection();
}
#pragma endregion

#pragma region Draggables
static inline void SquareConstraints(ImGuiSizeCallbackData* data)
{
	//data->DesiredSize.x = data->DesiredSize.y = std::max(data->DesiredSize.x, data->DesiredSize.y);
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

	PushStyleColor(ImGuiCol_WindowBg, {});
	PushStyleColor(ImGuiCol_Border, F::Render.Active.Value);
	PushStyleVar(ImGuiStyleVar_WindowRounding, H::Draw.Scale(3));
	PushStyleVar(ImGuiStyleVar_WindowBorderSize, H::Draw.Scale(1));
	PushStyleVar(ImGuiStyleVar_WindowMinSize, vSize);
	if (Begin(sLabel, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
	{
		ImVec2 vWindowPos = GetWindowPos();

		tDragBox.x = vWindowPos.x + vSize.x / 2, tDragBox.y = vWindowPos.y;
		tStorage = { tDragBox, H::Draw.Scale() };
		FSet(tVar, tDragBox);

		PushFont(F::Render.FontBold);
		ImVec2 vTextSize = FCalcTextSize(sLabel);
		SetCursorPos({ (vSize.x - vTextSize.x) * 0.5f, (vSize.y - vTextSize.y) * 0.5f });
		FText(sLabel);
		PopFont();

		End();
	}
	PopStyleVar(3);
	PopStyleColor(2);
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

	PushStyleColor(ImGuiCol_WindowBg, {});
	PushStyleColor(ImGuiCol_Border, F::Render.Active.Value);
	PushStyleVar(ImGuiStyleVar_WindowRounding, H::Draw.Scale(3));
	PushStyleVar(ImGuiStyleVar_WindowBorderSize, H::Draw.Scale(1));
	if (Begin(sLabel, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
	{
		ImVec2 vWindowPos = GetWindowPos();
		ImVec2 vWinSize = GetWindowSize();

		tWindowBox.w = vWinSize.x, tWindowBox.h = vWinSize.y;
		tWindowBox.x = vWindowPos.x + tWindowBox.w / 2, tWindowBox.y = vWindowPos.y;
		tStorage = { tWindowBox, H::Draw.Scale() };
		FSet(tVar, tWindowBox);

		PushFont(F::Render.FontBold);
		ImVec2 vTextSize = FCalcTextSize(sLabel);
		SetCursorPos({ (vWinSize.x - vTextSize.x) * 0.5f, (vWinSize.y - vTextSize.y) * 0.5f });
		FText(sLabel);
		PopFont();

		End();
	}
	PopStyleVar(2);
	PopStyleColor(2);
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

	float flNameWidth = 0, flInfoWidth = 0, flStateWidth = 0;
	PushFont(F::Render.FontSmall);
	for (auto& [sName, sInfo, sState, iBind, tBind] : vInfo)
	{
		flNameWidth = std::max(flNameWidth, FCalcTextSize(sName).x);
		flInfoWidth = std::max(flInfoWidth, FCalcTextSize(sInfo.c_str()).x);
		flStateWidth = std::max(flStateWidth, FCalcTextSize(sState.c_str()).x);
	}
	PopFont();
	flNameWidth += H::Draw.Scale(9), flInfoWidth += H::Draw.Scale(9), flStateWidth += H::Draw.Scale(9);

	float flWidth = flNameWidth + flInfoWidth + flStateWidth + (m_bIsOpen ? H::Draw.Scale(113) : H::Draw.Scale(14));
	float flHeight = H::Draw.Scale(18 * vInfo.size() + (Vars::Menu::BindWindowTitle.Value ? 42 : 12));
	SetNextWindowSize({ flWidth, flHeight }, ImGuiCond_Always);
	PushStyleVar(ImGuiStyleVar_WindowMinSize, { H::Draw.Scale(40), H::Draw.Scale(40) });
	if (Begin("Binds", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
	{
		ImVec2 vWindowPos = GetWindowPos();

		if (Vars::Menu::BindWindowTitle.Value)
			RenderTwoToneBackground(H::Draw.Scale(28), F::Render.Background0, F::Render.Background0p5, F::Render.Background2);
		else
			RenderBackground(F::Render.Background0p5, F::Render.Background2);

		tDragBox.x = vWindowPos.x; tDragBox.y = vWindowPos.y; tOld = tDragBox;
		if (m_bIsOpen)
			FSet(Vars::Menu::BindsDisplay, tDragBox);

		int iListStart = 8;
		if (Vars::Menu::BindWindowTitle.Value)
		{
			SetCursorPos({ H::Draw.Scale(8), H::Draw.Scale(6) });
			PushFont(F::Render.FontLarge);
			SetCursorPos({ H::Draw.Scale(8), H::Draw.Scale(7) });
			PushStyleColor(ImGuiCol_Text, F::Render.Active.Value);
			FText("Key");
			int keyWidth = 0, keyHeight = 0;
			ImVec2 textSize = ImGui::CalcTextSize("Key");
			keyWidth = textSize.x;
			keyHeight = textSize.y;
			SetCursorPos({ H::Draw.Scale(8) + keyWidth, H::Draw.Scale(7) });
			PushStyleColor(ImGuiCol_Text, F::Render.Accent.Value);
			FText("binds");
			PopStyleColor(2);
			PopFont();

			iListStart = 36;
		}

		PushFont(F::Render.FontSmall);
		int i = 0; for (auto& [sName, sInfo, sState, iBind, tBind] : vInfo)
		{
			float flPosX = 0;

			if (m_bIsOpen)
				PushTransparent(!F::Binds.WillBeEnabled(iBind), true);

			SetCursorPos({ flPosX += H::Draw.Scale(12), H::Draw.Scale(iListStart + 18 * i) });
			PushStyleColor(ImGuiCol_Text, tBind.m_bActive ? F::Render.Accent.Value : F::Render.Inactive.Value);
			FText(sName);
			PopStyleColor();

			SetCursorPos({ flPosX += flNameWidth, H::Draw.Scale(iListStart + 18 * i) });
			PushStyleColor(ImGuiCol_Text, tBind.m_bActive ? F::Render.Active.Value : F::Render.Inactive.Value);
			FText(sInfo.c_str());

			SetCursorPos({ flPosX += flInfoWidth, H::Draw.Scale(iListStart + 18 * i) });
			FText(sState.c_str());
			PopStyleColor();

			if (m_bIsOpen)
			{	// buttons
				SetCursorPos({ flWidth - H::Draw.Scale(26), H::Draw.Scale(iListStart - 2 + 18 * i) });
				bool bDelete = IconButton(ICON_MD_DELETE, H::Draw.Scale(18));

				SetCursorPos({ flWidth - H::Draw.Scale(51), H::Draw.Scale(iListStart - 2 + 18 * i) });
				bool bNot = IconButton(!tBind.m_bNot ? ICON_MD_CODE : ICON_MD_CODE_OFF, H::Draw.Scale(18));

				PushTransparent(Transparent || tBind.m_iVisibility == BindVisibilityEnum::Hidden, true);
				SetCursorPos({ flWidth - H::Draw.Scale(76), H::Draw.Scale(iListStart - 2 + 18 * i) });
				bool bVisibility = IconButton(tBind.m_iVisibility == BindVisibilityEnum::Always ? ICON_MD_VISIBILITY : ICON_MD_VISIBILITY_OFF, H::Draw.Scale(18));
				PopTransparent(1, 1);

				SetCursorPos({ flWidth - H::Draw.Scale(101), H::Draw.Scale(iListStart - 2 + 18 * i) });
				bool bEnable = IconButton(tBind.m_bEnabled ? ICON_MD_TOGGLE_ON : ICON_MD_TOGGLE_OFF, H::Draw.Scale(18));

				PopTransparent(1, 1);

				PushFont(F::Render.FontRegular);
				PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(8), H::Draw.Scale(8) });

				if (bEnable)
					tBind.m_bEnabled = !tBind.m_bEnabled;
				else if (bVisibility)
					tBind.m_iVisibility = (tBind.m_iVisibility + 1) % 3;
				else if (bNot)
					tBind.m_bNot = !tBind.m_bNot;
				else if (bDelete)
				{
					if (tBind.m_vVars.size() <= 1 && !F::Binds.HasChildren(iBind) || U::KeyHandler.Down(VK_SHIFT)) // allow user to quickly remove binds
						F::Binds.RemoveBind(iBind);
					else
						OpenPopup(std::format("DeleteBind{}", iBind).c_str());
				}

				if (FBeginPopupModal(std::format("DeleteBind{}", iBind).c_str()))
				{
					FText(std::format("Do you really want to delete '{}'{}?", tBind.m_sName, F::Binds.HasChildren(iBind) ? " and all of its children" : "").c_str());

					SetCursorPosY(GetCursorPosY() - 8); // stupid and i don't know why this is needed here
					if (FButton("Yes", FButtonEnum::Left))
					{
						F::Binds.RemoveBind(iBind);
						CloseCurrentPopup();
					}
					if (FButton("No", FButtonEnum::Right | FButtonEnum::SameLine))
						CloseCurrentPopup();

					EndPopup();
				}

				PopStyleVar();
				PopFont();
			}

			i++;
		}
		PopFont();

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

	PushFont(F::Render.FontRegular);

	ProcessDeferredNotifications();

	if (m_bIsOpen)
	{
		DrawNotifications();
		ManageVars();
		DrawMenu();

		AddDraggable("Ticks", Vars::Menu::TicksDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::Ticks);
		AddDraggable("Crit hack", Vars::Menu::CritsDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::CritHack);
		AddDraggable("Spectators", Vars::Menu::SpectatorsDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::Spectators);
		AddDraggable("Ping", Vars::Menu::PingDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::Ping);
		AddDraggable("Conditions", Vars::Menu::ConditionsDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::Conditions);
		AddDraggable("Seed prediction", Vars::Menu::SeedPredictionDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::SeedPrediction);
		AddDraggable("Nav bot", Vars::Menu::NavBotDisplay, FGet(Vars::Menu::Indicators) & Vars::Menu::IndicatorsEnum::NavBot);
		AddResizableDraggable("Camera", Vars::Visuals::Simulation::ProjectileWindow, FGet(Vars::Visuals::Simulation::ProjectileCamera), OptionalConstraints);

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

	DrawBinds();
	F::Notifications.Draw();
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
	
	ImVec2 vDisplaySize = GetIO().DisplaySize;
	float flNotificationWidth = H::Draw.Scale(400);
	float flNotificationHeight = H::Draw.Scale(80);
	float flPosX = (vDisplaySize.x - flNotificationWidth) * 0.5f;
	float flPosY = H::Draw.Scale(20);
	
	for (size_t i = 0; i < m_vNotifications.size(); ++i)
	{
		auto& notification = m_vNotifications[i];
		if (!notification.m_bVisible)
			continue;
		
		float flCurrentPosY = flPosY + (i * (flNotificationHeight + H::Draw.Scale(10)));
		
		PushStyleVar(ImGuiStyleVar_WindowRounding, H::Draw.Scale(8));
		PushStyleVar(ImGuiStyleVar_WindowPadding, { H::Draw.Scale(16), H::Draw.Scale(12) });
		
		PushStyleColor(ImGuiCol_WindowBg, F::Render.Background1.Value);
		PushStyleColor(ImGuiCol_Border, F::Render.Accent.Value);
		
		SetNextWindowPos({ flPosX, flCurrentPosY }, ImGuiCond_Always);
		SetNextWindowSize({ flNotificationWidth, 0 }, ImGuiCond_Always);
		
		char sWindowName[64];
		snprintf(sWindowName, sizeof(sWindowName), "##Notification%zu", i);
		
		if (Begin(sWindowName, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
		{
			PushFont(F::Render.FontBold);
			PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
			
			PushFont(F::Render.IconFont);
			Text(ICON_MD_WARNING);
			PopFont();
			
			SameLine();
			Text(" %s", notification.m_sTitle.c_str());
			PopStyleColor();
			PopFont();
			
			Separator();
			
			PushFont(F::Render.FontRegular);
			PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			TextWrapped("%s", notification.m_sMessage.c_str());
			PopStyleColor();
			PopFont();
		}
		End();
		
		PopStyleColor(2);
		PopStyleVar(2);
	}
}
#endif
