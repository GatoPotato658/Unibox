#include "AutoItem.h"
#include "../../../BytePatches/BytePatches.h"
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

MAKE_SIGNATURE(CStorePage_DoPreviewItem, "client.dll", "40 53 48 81 EC ? ? ? ? 0F B7 DA", 0x0);
MAKE_SIGNATURE(CCraftingPanel_Craft, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC ? FF 81", 0x0);

constexpr std::array<item_definition_index_t, 12> kPreferredNoisemakerDefs{
	280, 281, 282, 283, 284, 286, 288, 362, 364, 365, 493, 542
};

bool CAutoItem::Craft(CTFPlayerInventory* pLocalInventory, std::vector<item_definition_index_t> vItemDefs)
{
	// Return early to avoid a crash
	static BytePatch patch("client.dll", "C6 44 24 ? ? 41 B0 ? 48 8D 15 ? ? ? ? 49 8B CD", 0x0, "EB 6E");
	if (!patch.Initialize())
		return false;

	std::vector<uint64_t> vUUIDs;

	// Iterate all desired items
	for (auto iDefIndex : vItemDefs)
	{
		auto vItems = pLocalInventory->GetItemsOfItemDef(iDefIndex);
		// Iterate all the items matching the ID
		for (auto uItemUUID : vItems)
		{
			// Only add entries which are not added yet
			bool bFailed = false;
			for (auto uUUID : vUUIDs)
			{
				if (uUUID == uItemUUID)
					bFailed = true;
			}

			// Continue to next entry on failure
			if (!bFailed)
			{
				vUUIDs.push_back(uItemUUID);
				break;
			}
			else
				continue;
		}
	}

	// No items found
	if (vUUIDs.empty())
		return false;

	CCraftingPanel panel{};

	// Add items
	for (int i = 0; i < vUUIDs.size(); i++)
		panel.m_InputItems[i] = vUUIDs[i];

	// Select "Custom" Recipe
	panel.m_iCurrentlySelectedRecipe = -2;

	// Call the Craft function
	S::CCraftingPanel_Craft.Call<void>(&panel);

	// Undo our patches to the game
	patch.Unload();
	return true;
}

void CAutoItem::Rent(item_definition_index_t iItemDef)
{
	S::CStorePage_DoPreviewItem.Call<void>(nullptr, iItemDef);
}

void CAutoItem::MvmRent()
{
	constexpr item_definition_index_t iVaccinator = 998;
	constexpr item_definition_index_t iHeatmaker = 752;

	auto pInventoryManager = I::TFInventoryManager();
	if (!pInventoryManager)
	{
		SDK::Output("cat_mvm_rent", "Inventory manager unavailable", { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return;
	}

	auto pLocalInventory = pInventoryManager->GetLocalInventory();
	if (!pLocalInventory)
	{
		SDK::Output("cat_mvm_rent", "Local inventory unavailable", { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return;
	}

	EquipItem(pInventoryManager, pLocalInventory, TF_CLASS_MEDIC, SLOT_SECONDARY, iVaccinator, true, true);
	EquipItem(pInventoryManager, pLocalInventory, TF_CLASS_SNIPER, SLOT_PRIMARY, iHeatmaker, true, true);
}

item_definition_index_t CAutoItem::SelectNoisemaker(CTFPlayerInventory* pLocalInventory)
{
	for (auto iItemDef : kPreferredNoisemakerDefs)
	{
		if (pLocalInventory->GetFirstItemOfItemDef(iItemDef))
			return iItemDef;
	}

	if (pLocalInventory->GetFirstItemOfItemDef(m_iNoisemakerDefIndex))
		return m_iNoisemakerDefIndex;

	if (m_iFallbackNoisemakerDefIndex != m_iNoisemakerDefIndex && pLocalInventory->GetFirstItemOfItemDef(m_iFallbackNoisemakerDefIndex))
		return m_iFallbackNoisemakerDefIndex;

	return -1;
}

AchivementItem_t* CAutoItem::IsAchievementItem(item_definition_index_t iItemDef)
{
	return m_mAchievementItems.contains(iItemDef) ? &m_mAchievementItems[iItemDef] : nullptr;
}

void CAutoItem::GetItem(item_definition_index_t iItemDef, bool bRent)
{
	SDK::Output("CAutoItem", std::format("Trying to get item, {}", iItemDef).c_str(), { 255, 223, 0 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	auto pAchievementItem = IsAchievementItem(iItemDef);
	if (pAchievementItem)
	{
		UnlockAchievementItem(pAchievementItem->m_iAchievementId);
		I::EngineClient->ClientCmd_Unrestricted("cl_trigger_first_notification");
	}
	else if (bRent)
		Rent(iItemDef);
	else
		SDK::Output("CAutoItem", std::format("Failed to get item {}", iItemDef).c_str(), { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
}

void CAutoItem::GetAndEquipWeapon(CTFInventoryManager* pInventoryManager, CTFPlayerInventory* pLocalInventory, std::string sItemDefs, int iClass, int iSlot)
{
	static bool bFallback{};
	static int iFirstItemAttempts{};

	if (sItemDefs == "-1")
	{
		EquipItem(pInventoryManager, pLocalInventory, iClass, iSlot, -1, false, false);
		return;
	}

	const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();

	// Single item, rent or simply get via achievement, use fallback if needed and specified.
	if (sItemDefs.find(',') == std::string::npos && sItemDefs.find(';') == std::string::npos)
	{
		std::vector<std::string> vSplitStrDefIdx;
		std::vector<item_definition_index_t> vSplitDefIdx;
		try
		{
			if (sItemDefs.find('/') != std::string::npos)
			{
				boost::split(vSplitStrDefIdx, sItemDefs, boost::is_any_of("/"));
				for (auto sDefIdx : vSplitStrDefIdx)
					vSplitDefIdx.emplace_back(std::stoi(sDefIdx));

				bFallback = true;
			}
			else
				vSplitDefIdx.emplace_back(std::stoi(sItemDefs));
		}
		catch (const std::invalid_argument&)
		{
			SDK::Output("CAutoItem", "invalid_argument error making vSplitDefIdx vector.", { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return;
		}

		auto pItem = pLocalInventory->GetFirstItemOfItemDef(vSplitDefIdx.at(0));

		// Try the first item 3 times before moving to fallback
		if (bFallback)
		{
			if (!pItem && iFirstItemAttempts >= 3)
				EquipItem(pInventoryManager, pLocalInventory, iClass, iSlot, vSplitDefIdx.at(1), true, true);
			else
			{
				EquipItem(pInventoryManager, pLocalInventory, iClass, iSlot, vSplitDefIdx.at(0), true, true);
				iFirstItemAttempts++;
			}
		}
		else
			EquipItem(pInventoryManager, pLocalInventory, iClass, iSlot, vSplitDefIdx.at(0), true, true);
	}
	else
	{
		// Using auto-craft
		int iResultDefIndex;
		size_t loc = sItemDefs.find('-');
		if (loc != std::string::npos)
		{
			try
			{
				iResultDefIndex = std::stoi(sItemDefs.substr(loc + 1, sItemDefs.length()));
			}
			catch (const std::invalid_argument&)
			{
				SDK::Output("CAutoItem", "invalid_argument error making result integer.", { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
				return;
			}

			auto pItem = pLocalInventory->GetFirstItemOfItemDef(iResultDefIndex);
			if (pItem)
			{
				EquipItem(pInventoryManager, pLocalInventory, iClass, iSlot, iResultDefIndex, false, false);
				return;
			}

			std::vector<std::string> vStrDefIndexes;
			std::vector<item_definition_index_t> vDefIndexes;

			for (auto sGroup : aCraftGroups[iSlot])
			{
				SDK::Output("CAutoItem", std::format("Crafting group: {}", sGroup).c_str(), { 70,130,180 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
				vDefIndexes.clear();
				vStrDefIndexes.clear();

				// Last group should be the just the result, skip it
				if (sGroup == std::to_string(iResultDefIndex))
					continue;

				// Split this crafting group into IDs
				boost::split(vStrDefIndexes, sGroup, boost::is_any_of(","));

				try
				{
					// Convert to ints
					for (auto sDefIdx : vStrDefIndexes)
						vDefIndexes.emplace_back(std::stoi(sDefIdx));
				}
				catch (const std::invalid_argument&)
				{
					SDK::Output("CAutoItem", "invalid_argument error making vDefIndexes vector.", { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
					return;
				}

				// See if we have the requirements to perform this craft, if not try to get them.
				size_t uAmountAvailableRequirements = 0;
				for (auto iItemDef : vDefIndexes)
				{
					// For now we should just assume the user has
					// provided a way for getting needed scrap
					if (iItemDef == 5000 || iItemDef == 5001 || iItemDef == 5002)
					{
						uAmountAvailableRequirements++;
						continue;
					}

					// In this loop id is a item id of 1 part of the crafting group
					auto pItem = pLocalInventory->GetFirstItemOfItemDef(iItemDef);
					if (pItem)
						uAmountAvailableRequirements++;
					else
					{
						auto pAchievementItem = IsAchievementItem(iItemDef);
						if (pAchievementItem)
						{
							if (!pAchievementMgr)
								return;

							if (pAchievementMgr->HasAchieved(pAchievementItem->m_sName.c_str()))
							{
								SDK::Output("CAutoItem", std::format("Cant get specified ach item {} because it has already been unlocked! moving to next item.", iItemDef).c_str(), { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
								// User's fault if they put a ach item they unlocked and have used for crafting already/deleted.
								uAmountAvailableRequirements++;
								continue;
							}
							SDK::Output("CAutoItem", std::format("Getting ach item {}, required for this craft.", iItemDef).c_str(), { 70,130,180 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
							GetItem(iItemDef, false);
							return;
						}
						else
						{
							SDK::Output("CAutoItem", std::format("Missing required crafting materials! {} is NOT a ach item!", iItemDef).c_str(), { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
							return;
						}
					}
				}
				// Only attempt crafting if we have the correct amount of items.
				if (uAmountAvailableRequirements == vDefIndexes.size())
				{
					Craft(pLocalInventory, vDefIndexes);
					I::EngineClient->ClientCmd_Unrestricted("cl_trigger_first_notification");
				}
			}
		}
		else
			SDK::Output("CAutoItem", "No result item, can't auto-craft.", { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	}
}

void CAutoItem::EquipItem(CTFInventoryManager* pInventoryManager, CTFPlayerInventory* pLocalInventory, int iClass, int iSlot, item_definition_index_t iItemDef, bool bGet, bool bRent)
{
	if (iClass == TF_CLASS_SPY)
	{
		// Primary -> Secondary (Because guns are not in primary, nothing is, thanks gaben)
		if (iSlot == 0)
			iSlot = 1;
		// Secondary -> Cloak
		else if (iSlot == 1)
			iSlot = 6;
	}

	if (iItemDef == -1)
	{
		pInventoryManager->EquipItemInLoadout(iClass, iSlot, -1);
		return;
	}

	auto pItem = pLocalInventory->GetFirstItemOfItemDef(iItemDef);
	if (bGet && !pItem)
	{
		GetItem(iItemDef, bRent);
		return;
	}

	pItem = pLocalInventory->GetFirstItemOfItemDef(iItemDef);
	if (pItem)
	{
		if (!pItem->UUID())
			SDK::Output("CAutoItem", std::format("Invalid UUID for item {}", iItemDef).c_str(), { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		else
			pInventoryManager->EquipItemInLoadout(iClass, iSlot, pItem->UUID());
	}
}

bool CAutoItem::UnlockAchievementItem(int iAchievementId)
{
	const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
	if (pAchievementMgr && pAchievementMgr->GetAchievementByID(iAchievementId))
	{
		pAchievementMgr->AwardAchievement(iAchievementId);
		return true;
	}
	return false;
}

void CAutoItem::Run(CTFPlayer* pLocal)
{
	static Timer tTimer{};
	if (!Vars::Misc::Automation::AutoItem::Enable.Value || !pLocal || !tTimer.Run(Vars::Misc::Automation::AutoItem::Interval.Value))
		return;

	auto pInventoryManager = I::TFInventoryManager();
	if (!pInventoryManager)
		return;

	auto pLocalInventory = pInventoryManager->GetLocalInventory();
	if (!pLocalInventory)
		return;

	int iClass = pLocal->m_iClass();

	// Only run if we are playing a valid class
	if (iClass != 0)
	{
		if (Vars::Misc::Automation::AutoItem::Enable.Value & Vars::Misc::Automation::AutoItem::EnableEnum::Weapons)
		{
			GetAndEquipWeapon(pInventoryManager, pLocalInventory, Vars::Misc::Automation::AutoItem::Primary.Value, iClass, SLOT_PRIMARY);
			GetAndEquipWeapon(pInventoryManager, pLocalInventory, Vars::Misc::Automation::AutoItem::Secondary.Value, iClass, SLOT_SECONDARY);
			GetAndEquipWeapon(pInventoryManager, pLocalInventory, Vars::Misc::Automation::AutoItem::Melee.Value, iClass, SLOT_MELEE);
		}
		if (Vars::Misc::Automation::AutoItem::Enable.Value & Vars::Misc::Automation::AutoItem::EnableEnum::Hats)
		{
			static int offset = 0;
			const static int slots[3] = { 7, 8, 10 };

			EquipItem(pInventoryManager, pLocalInventory, iClass, slots[offset], Vars::Misc::Automation::AutoItem::FirstHat.Value);
			EquipItem(pInventoryManager, pLocalInventory, iClass, slots[(offset + 1) % 3], Vars::Misc::Automation::AutoItem::SecondHat.Value);
			EquipItem(pInventoryManager, pLocalInventory, iClass, slots[(offset + 2) % 3], Vars::Misc::Automation::AutoItem::ThirdHat.Value);
			offset = (offset + 1) % 3;
		}
		if (Vars::Misc::Automation::AutoItem::Enable.Value & Vars::Misc::Automation::AutoItem::EnableEnum::Noisemaker)
		{
			auto iNoisemakerToEquip = SelectNoisemaker(pLocalInventory);
			if (iNoisemakerToEquip != -1)
			{
				m_iNoisemakerDefIndex = iNoisemakerToEquip;
				EquipItem(pInventoryManager, pLocalInventory, iClass, 9, iNoisemakerToEquip, false, false);
			}
		}
	}
}