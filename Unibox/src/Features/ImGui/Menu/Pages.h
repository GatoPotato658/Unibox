#pragma once
#include "Shell.h"

enum EMenuPage
{
	MenuPageAimbot,
	MenuPageVisuals,
	MenuPageHvH,
	MenuPageMisc,
	MenuPageAnticheat,
	MenuPagePlayers,
	MenuPageSkins,
	MenuPageSettings
};

inline const std::vector<NavPage_t> vMenuPages = {
	{ "Aimbot", ICON_MD_GPS_FIXED, { "General", "Weapons", "Support", "Draw" } },
	{ "Visuals", ICON_MD_VISIBILITY, { "ESP", "View", "World", "Menu" } },
	{ "HvH", ICON_MD_BOLT, { "Ticks", "Angles" } },
	{ "Misc", ICON_MD_TUNE, { "Main", "Automation", "Bots", "Queueing" } },
	{ "Anticheat", ICON_MD_GPP_MAYBE, { "Cheaters", "Detection" } },
	{ "Players", ICON_MD_GROUP, { "Lobby", "Tags", "Marked", "Logging", "Output" } },
	{ "Skins", ICON_MD_PALETTE, { "Weapons", "Backpack" } },
	{ "Settings", ICON_MD_SETTINGS, { "Config", "Binds", "Materials", "Misc" } }
};

namespace Pages
{
	bool Draw(int iPage, int iSub);
	void Search(const std::string& sSearch);
}
