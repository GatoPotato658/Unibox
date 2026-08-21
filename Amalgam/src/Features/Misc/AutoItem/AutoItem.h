#pragma once
#include "../../../SDK/SDK.h"
#include <chrono>

struct AchivementItem_t
{
	int m_iAchievementId;
	std::string m_sName;
};

class CAutoItem
{
private:
	bool Craft(CTFPlayerInventory* pLocalInventory, std::vector<item_definition_index_t> vItemDefs);
	bool UnlockAchievementItem(int iAchievementId);

	AchivementItem_t* IsAchievementItem(item_definition_index_t iItemDef);

	void GetItem(item_definition_index_t iItemDef, bool bRent = true);

	void GetAndEquipWeapon(CTFInventoryManager* pInventoryManager, CTFPlayerInventory* pLocalInventory, std::string sItemDefs, int iClass, int iSlot);
	void EquipItem(CTFInventoryManager* pInventoryManager, CTFPlayerInventory* pLocalInventory, int iClass, int iSlot, item_definition_index_t iItemDef, bool bGet = true, bool bRent = false);
	item_definition_index_t SelectNoisemaker(CTFPlayerInventory* pLocalInventory);

	std::array<std::vector<std::string>, 3> aCraftGroups{ {{"-1"}, {"-1"}, {"-1"}} };
	std::unordered_map<item_definition_index_t, AchivementItem_t> m_mAchievementItems{
		{45,  {1036, "TF_SCOUT_ACHIEVE_PROGRESS1"}},	// Force-A-Nature - Scout Milestone 1
		{44,  {1037, "TF_SCOUT_ACHIEVE_PROGRESS2"}},	// Sandman - Scout Milestone 2
		{46,  {1038, "TF_SCOUT_ACHIEVE_PROGRESS3"}},	// Bonk! Atomic Punch - Scout Milestone 3

		{128, {1236, "TF_SOLDIER_ACHIEVE_PROGRESS1"}},	// Equalizer - Soldier Milestone 1
		{127, {1237, "TF_SOLDIER_ACHIEVE_PROGRESS2"}},	// Direct Hit - Soldier Milestone 2
		{129, {1238, "TF_SOLDIER_ACHIEVE_PROGRESS3"}},	// Buff Banner - Soldier Milestone 3

		{39,  {1637, "TF_PYRO_ACHIEVE_PROGRESS1"}},		// Flare Gun - Pyro Milestone 1
		{40,  {1638, "TF_PYRO_ACHIEVE_PROGRESS2"}},		// Backburner - Pyro Milestone 2
		{38,  {1639, "TF_PYRO_ACHIEVE_PROGRESS3"}},		// Axtinguisher - Pyro Milestone 3

		{131, {1336, "TF_DEMOMAN_ACHIEVE_PROGRESS1"}},	// Chargin' Targe - Demoman Milestone 1
		{132, {1337, "TF_DEMOMAN_ACHIEVE_PROGRESS2"}},	// Eyelander - Demoman Milestone 2
		{130, {1338, "TF_DEMOMAN_ACHIEVE_PROGRESS3"}},	// Scottish Resistance - Demoman Milestone 3

		{42,  {1537, "TF_HEAVY_ACHIEVE_PROGRESS1"}},	// Sandvich - Heavy Milestone 1
		{41,  {1538, "TF_HEAVY_ACHIEVE_PROGRESS2"}},	// Natascha - Heavy Milestone 2
		{43,  {1539, "TF_HEAVY_ACHIEVE_PROGRESS3"}},	// Killing Gloves of Boxing - Heavy Milestone 3

		{141, {1801, "TF_ENGINEER_ACHIEVE_PROGRESS1"}},	// Frontier Justice - Engineer Milestone 1
		{142, {1802, "TF_ENGINEER_ACHIEVE_PROGRESS2"}},	// Gunslinger - Engineer Milestone 2
		{140, {1803, "TF_ENGINEER_ACHIEVE_PROGRESS3"}},	// Wrangler - Engineer Milestone 3

		{36,  {1437, "TF_MEDIC_ACHIEVE_PROGRESS1"}},	// Blutsauger - Medic Milestone 1
		{35,  {1438, "TF_MEDIC_ACHIEVE_PROGRESS2"}},	// Kritzkrieg - Medic Milestone 2
		{37,  {1439, "TF_MEDIC_ACHIEVE_PROGRESS3"}},	// Ubersaw - Medic Milestone 3

		{56,  {1136, "TF_SNIPER_ACHIEVE_PROGRESS1"}},	// Huntsman - Sniper Milestone 1
		{58,  {1137, "TF_SNIPER_ACHIEVE_PROGRESS2"}},	// Jarate - Sniper Milestone 2
		{57,  {1138, "TF_SNIPER_ACHIEVE_PROGRESS3"}},	// Razorback - Sniper Milestone 3

		{61,  {1735, "TF_SPY_ACHIEVE_PROGRESS1"}},		// Ambassador - Spy Milestone 1
		{60,  {1736, "TF_SPY_ACHIEVE_PROGRESS2"}},		// Cloak and Dagger - Spy Milestone 2
		{59,  {1737, "TF_SPY_ACHIEVE_PROGRESS3"}},		// Dead Ringer - Spy Milestone 3

		{1123, {1928, "TF_HALLOWEEN_DOOMSDAY_MILESTONE"}},		// Necro Smasher - Carnival of Carnage: Step Right Up
		{940,  {1902, "TF_HALLOWEEN_DOMINATE_FOR_HAT"}},		// Ghostly Gibus - Ghastly Gibus Grab
		{115,  {1901, "TF_HALLOWEEN_COLLECT_PUMPKINS"}},		// Mildly Disturbing Halloween Mask - Candy Coroner
		{278,  {1906, "TF_HALLOWEEN_BOSS_KILL"}},				// Horseless Headless Horsemann's Head - Sleepy Holl0WND
		{302,  {2006, "TF_REPLAY_YOUTUBE_VIEWS_TIER2"}},		// Frontline Field Recorder - Local Cinema Star
		{581,  {1910, "TF_HALLOWEEN_EYEBOSS_KILL"}},			// MONOCULUS! - Optical Defusion
		{668,  {2212, "TF_MAPS_FOUNDRY_ACHIEVE_PROGRESS1"}},	// Full Head Of Steam - Foundry Milestone
		{756,  {2412, "TF_MAPS_DOOMSDAY_ACHIEVE_PROGRESS1"}},	// Gentle Munitionne of Leisure - Doomsday Milestone
		{941,  {1912, "TF_HALLOWEEN_MERASMUS_COLLECT_LOOT"}},	// Skull Island Topper - A Lovely Vacation Spot
		{581,  {1911, "TF_HALLOWEEN_LOOT_ISLAND"}},				// Bombinomicon - Dive Into a Good Book
		{744,  {156, "TF_DOMINATE_FOR_GOGGLES"}},				// Pyrovision Goggles - A Fresh Pair of Eyes
		{1164, {167, "TF_PASS_TIME_GRIND"}},					// Civilian Grade JACK Hat - Jackpot!
		{1169, {166, "TF_PASS_TIME_HAT"}},						// Military Grade JACK Hat - Tune Merasmus's Multi-Dimensional Television
		{1170, {166, "TF_PASS_TIME_HAT"}},						// PASS Time Miniature Half JACK - Tune Merasmus's Multi-Dimensional Television
		{267,  {1909, "TF_HALLOWEEN_BOSS_KILL_MELEE"}}			// Haunted Metal Scrap - Gored!
	};

	item_definition_index_t m_iNoisemakerDefIndex = 536;
	item_definition_index_t m_iFallbackNoisemakerDefIndex = 536;
public:
	CAutoItem()
	{
		std::time_t theTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm aTime;
		localtime_s(&aTime, &theTime);

		int day = aTime.tm_mday;
		int month = aTime.tm_mon;

		// We only want to use the Christmas noisemaker around Christmas time, let's use the 1st of December till 12th of January
		if ((month == 11 && day >= 1) || (!month && day <= 12))
			m_iNoisemakerDefIndex = 673;

		m_iFallbackNoisemakerDefIndex = m_iNoisemakerDefIndex;
	}

	void Rent(item_definition_index_t iItemDef);
	void MvmRent();
	void Run(CTFPlayer* pLocal);
};

ADD_FEATURE(CAutoItem, AutoItem);