#include "SkinChanger.h"

#ifndef TEXTMODE

MAKE_SIGNATURE(CEconItemSchema_GetAttributeDefinition, "client.dll", "89 54 24 ? 53 48 83 EC ? 48 8B D9 48 8D 54 24 ? 48 81 C1 ? ? ? ? E8 ? ? ? ? 8B D0 3B 83 ? ? ? ? 73 ? 8B 83 ? ? ? ? 83 F8 ? 74 ? 3B D0 7F ? 48 81 C3 ? ? ? ? 44 8B C2 83 FA ? 74 ? 48 8B 03 8B CA", 0x0);
MAKE_SIGNATURE(CAttributeList_SetRuntimeAttributeValue, "client.dll", "48 89 5C 24 10 55 56 57 48 8B EC 48 83 EC 50 44", 0x0);

#endif

namespace
{
	enum EPaintFamily
	{
		FamilyNone,
		FamilyScattergun, FamilyPistol, FamilyRocket, FamilyShotgun, FamilyFlame,
		FamilyGrenade, FamilySticky, FamilyMinigun, FamilyWrench, FamilyMedigun,
		FamilySniper, FamilySMG, FamilyKnife, FamilyRevolver, FamilyUnique
	};

	struct Kit_t
	{
		int iId;
		const char* sName;
	};

	constexpr Kit_t kKits[] =
	{
		{ 0, "None" },
		{ 102, "Wrapped Reviver Mk.II" },
		{ 104, "Carpet Bomber Mk.II" },
		{ 105, "Masked Mender Mk.II" },
		{ 106, "Woodland Warrior Mk.II" },
		{ 109, "Forest Fire Mk.II" },
		{ 112, "Backwoods Boomstick Mk.II" },
		{ 113, "Woodsy Widowmaker Mk.II" },
		{ 114, "Night Owl Mk.II" },
		{ 120, "Iron Wood Mk.II" },
		{ 122, "Plaid Potshotter Mk.II" },
		{ 130, "Bovine Blazemaker Mk.II" },
		{ 139, "Civil Servant Mk.II" },
		{ 143, "Smalltown Bringdown Mk.II" },
		{ 144, "Civic Duty Mk.II" },
		{ 151, "Dead Reckoner Mk.II" },
		{ 160, "Autumn Mk.II" },
		{ 161, "Nutcracker Mk.II" },
		{ 163, "Macabre Web Mk.II" },
		{ 200, "Bloom Buffed" },
		{ 201, "Quack Canvassed" },
		{ 202, "Bank Rolled" },
		{ 203, "Merc Stained" },
		{ 204, "Kill Covered" },
		{ 205, "Fire Glazed" },
		{ 206, "Pizza Polished" },
		{ 207, "Bonk Varnished" },
		{ 208, "Star Crossed" },
		{ 209, "Clover Camo'd" },
		{ 210, "Freedom Wrapped" },
		{ 211, "Cardboard Boxed" },
		{ 212, "Dream Piped" },
		{ 213, "Miami Element" },
		{ 214, "Neo Tokyo" },
		{ 215, "Geometrical Teams" },
		{ 217, "Bomber Soul" },
		{ 218, "Uranium" },
		{ 220, "Cabin Fevered" },
		{ 221, "Polar Surprise" },
		{ 223, "Hana" },
		{ 224, "Dovetailed" },
		{ 225, "Cosmic Calamity" },
		{ 226, "Hazard Warning" },
		{ 228, "Mosaic" },
		{ 230, "Jazzy" },
		{ 232, "Alien Tech" },
		{ 234, "Damascus and Mahogany" },
		{ 235, "Skull Study" },
		{ 236, "Haunted Ghosts" },
		{ 237, "Spectral Shimmered" },
		{ 238, "Spirit of Halloween" },
		{ 239, "Horror Holiday" },
		{ 240, "Totally Boned" },
		{ 241, "Electroshocked" },
		{ 242, "Ghost Town" },
		{ 243, "Tumor Toasted" },
		{ 244, "Calavera Canvas" },
		{ 245, "Snow Covered" },
		{ 246, "Frost Ornamented" },
		{ 247, "Smissmas Village" },
		{ 248, "Igloo" },
		{ 249, "Seriously Snowed" },
		{ 250, "Smissmas Camo" },
		{ 251, "Sleighin' Style" },
		{ 252, "Alpine" },
		{ 253, "Gift Wrapped" },
		{ 254, "Winterland Wrapped" },
		{ 255, "Helldriver" },
		{ 256, "Organ-ically Hellraised" },
		{ 257, "Spectrum Splattered" },
		{ 258, "Candy Coated" },
		{ 259, "Pumpkin Pied" },
		{ 260, "Sweet Toothed" },
		{ 261, "Crawlspace Critters" },
		{ 262, "Portal Plastered" },
		{ 263, "Death Deluxe" },
		{ 264, "Raving Dead" },
		{ 265, "Eyestalker" },
		{ 266, "Spider's Cluster" },
		{ 267, "Gourdy Green" },
		{ 268, "Mummified Mimic" },
		{ 269, "Spider Season" },
		{ 270, "Gingerbread Winner" },
		{ 271, "Saccharine Striped" },
		{ 272, "Elfin Enamel" },
		{ 273, "Peppermint Swirl" },
		{ 275, "Snow Globalization" },
		{ 276, "Gifting Mann's Wrapping Paper" },
		{ 277, "Snowflake Swirled" },
		{ 278, "Smissmas Spycrabs" },
		{ 279, "Frozen Aurora" },
		{ 280, "Starlight Serenity" },
		{ 281, "Frosty Delivery" },
		{ 282, "Glacial Glazed" },
		{ 283, "Cookie Fortress" },
		{ 284, "Sarsaparilla Sprayed" },
		{ 285, "Swashbuckled" },
		{ 286, "Skull Cracked" },
		{ 287, "Misfortunate" },
		{ 289, "Neon-ween" },
		{ 290, "Simple Spirits" },
		{ 291, "Broken Bones" },
		{ 292, "Potent Poison" },
		{ 293, "Searing Souls" },
		{ 294, "Party Phantoms" },
		{ 295, "Polter-Guised" },
		{ 296, "Kiln and Conquer" },
		{ 297, "Necromanced" },
		{ 300, "Yeti Coated" },
		{ 301, "Park Pigmented" },
		{ 302, "Mannana Peeled" },
		{ 303, "Macaw Masked" },
		{ 304, "Sax Waxed" },
		{ 305, "Anodized Aloha" },
		{ 306, "Bamboo Brushed" },
		{ 307, "Tiger Buffed" },
		{ 308, "Croc Dusted" },
		{ 309, "Pina Polished" },
		{ 310, "Leopard Printed" },
		{ 390, "Dragon Slayer" },
		{ 391, "Smissmas Sweater" },
		{ 400, "Ghoul Blaster" },
		{ 401, "Cream Corned" },
		{ 402, "Sunriser" },
		{ 403, "Sacred Slayer" },
		{ 404, "Metalized Soul" },
		{ 405, "Bonzo Gnawed" },
		{ 406, "Health and Hell" },
		{ 407, "Health and Hell (Green)" },
		{ 408, "Hypergon" },
		{ 409, "Pumpkin Plastered" },
		{ 410, "Chilly Autumn" },
		{ 411, "Steel Brushed" },
		{ 412, "Secretly Serviced" },
		{ 413, "Sky Stallion" },
		{ 414, "Bomb Carrier" },
		{ 415, "Business Class" },
		{ 416, "Deadly Dragon" },
		{ 417, "Team Serviced" },
		{ 418, "Warborn" },
		{ 419, "Pacific Peacemaker" },
		{ 420, "Mechanized Monster" },
		{ 421, "Stardust" },
		{ 422, "Team Detail" },
		{ 423, "Gobi Glazed" },
		{ 424, "Sleek Greek" },
		{ 425, "Graphite Gripped" },
		{ 426, "Stealth Specialist" },
		{ 427, "Piranha Mania" },
		{ 428, "Team Charged" },
		{ 429, "Brawler's Iron" },
		{ 430, "Necropolish" },
		{ 431, "Blackout" },
		{ 432, "Broken Record" },
		{ 433, "Sandwich Diner" },
		{ 434, "Beachy Boy" },
		{ 435, "Army Guns" },
		{ 436, "Taxi Cabbed" },
		{ 437, "Ocean Mapped" },
		{ 438, "Krak-coated" },
		{ 439, "Team Union" },
		{ 440, "Sideshow" },
		{ 441, "Storage War" },
		{ 442, "Die'n Dasher" },
	};

	void RedirectIndex(int& nWeaponIndex)
	{
		switch (nWeaponIndex)
		{
		case Soldier_m_RocketLauncher: nWeaponIndex = Soldier_m_RocketLauncherR; break;
		case Scout_m_Scattergun: nWeaponIndex = Scout_m_ScattergunR; break;
		case Pyro_m_FlameThrower: nWeaponIndex = Pyro_m_FlameThrowerR; break;
		case Demoman_m_GrenadeLauncher: nWeaponIndex = Demoman_m_GrenadeLauncherR; break;
		case Demoman_s_StickybombLauncher: nWeaponIndex = Demoman_s_StickybombLauncherR; break;
		case Heavy_m_Minigun: nWeaponIndex = Heavy_m_MinigunR; break;
		case Engi_t_Wrench: nWeaponIndex = Engi_t_WrenchR; break;
		case Medic_s_MediGun: nWeaponIndex = Medic_s_MediGunR; break;
		case Sniper_m_SniperRifle: nWeaponIndex = Sniper_m_SniperRifleR; break;
		case Sniper_s_SMG: nWeaponIndex = Sniper_s_SMGR; break;
		case Spy_t_Knife: nWeaponIndex = Spy_t_KnifeR; break;
		case Spy_m_Revolver: nWeaponIndex = Spy_m_RevolverR; break;
		case Scout_s_ScoutsPistol:
		case Engi_s_EngineersPistol: nWeaponIndex = Engi_s_PistolR; break;
		case Soldier_s_SoldiersShotgun:
		case Pyro_s_PyrosShotgun:
		case Heavy_s_HeavysShotgun:
		case Engi_m_EngineersShotgun: nWeaponIndex = Soldier_s_ShotgunR; break;
		case Scout_t_Bat: nWeaponIndex = Scout_t_BatR; break;
		case Soldier_t_Shovel: nWeaponIndex = Soldier_t_ShovelR; break;
		case Pyro_t_FireAxe: nWeaponIndex = Pyro_t_FireAxeR; break;
		case Demoman_t_Bottle: nWeaponIndex = Demoman_t_BottleR; break;
		case Medic_t_Bonesaw: nWeaponIndex = Medic_t_BonesawR; break;
		case Sniper_t_Kukri: nWeaponIndex = Sniper_t_KukriR; break;
		default: break;
		}
	}

	int Mk2Family(int iKit)
	{
		switch (iKit)
		{
		case 102: case 105: case 139: return FamilyMedigun;
		case 104: return FamilySticky;
		case 106: case 143: return FamilyRocket;
		case 109: case 130: return FamilyFlame;
		case 112: case 144: return FamilyShotgun;
		case 113: case 122: return FamilySMG;
		case 114: return FamilySniper;
		case 120: return FamilyMinigun;
		case 151: return FamilyRevolver;
		default: return FamilyNone;
		}
	}

	int PaintFamily(int iDefIndex)
	{
		int iRedirected = iDefIndex;
		RedirectIndex(iRedirected);
		switch (iRedirected)
		{
		case Scout_m_ScattergunR: return FamilyScattergun;
		case Scout_s_PistolR: return FamilyPistol;
		case Soldier_m_RocketLauncherR: return FamilyRocket;
		case Soldier_s_ShotgunR: return FamilyShotgun;
		case Pyro_m_FlameThrowerR: return FamilyFlame;
		case Demoman_m_GrenadeLauncherR: return FamilyGrenade;
		case Demoman_s_StickybombLauncherR: return FamilySticky;
		case Heavy_m_MinigunR: return FamilyMinigun;
		case Engi_t_WrenchR: return FamilyWrench;
		case Medic_s_MediGunR: return FamilyMedigun;
		case Sniper_m_SniperRifleR: return FamilySniper;
		case Sniper_s_SMGR: return FamilySMG;
		case Spy_t_KnifeR: return FamilyKnife;
		case Spy_m_RevolverR: return FamilyRevolver;
		default: break;
		}

		switch (iDefIndex)
		{
		case Scout_m_FestiveScattergun:
		case Scout_m_SilverBotkillerScattergunMkI:
		case Scout_m_GoldBotkillerScattergunMkI:
		case Scout_m_RustBotkillerScattergunMkI:
		case Scout_m_BloodBotkillerScattergunMkI:
		case Scout_m_CarbonadoBotkillerScattergunMkI:
		case Scout_m_DiamondBotkillerScattergunMkI:
		case Scout_m_SilverBotkillerScattergunMkII:
		case Scout_m_GoldBotkillerScattergunMkII:
		case Scout_m_NightTerror:
		case Scout_m_TartanTorpedo:
		case Scout_m_CountryCrusher:
		case Scout_m_BackcountryBlaster:
		case Scout_m_SpruceDeuce:
		case Scout_m_CurrentEvent:
		case Scout_m_MacabreWeb:
		case Scout_m_Nutcracker:
		case Scout_m_BlueMew:
		case Scout_m_FlowerPower:
		case Scout_m_ShottoHell:
		case Scout_m_CoffinNail:
		case Scout_m_KillerBee:
		case Scout_m_Corsair:
			return FamilyScattergun;

		case Scout_s_RedRockRoscoe:
		case Scout_s_HomemadeHeater:
		case Scout_s_HickoryHolepuncher:
		case Scout_s_LocalHero:
		case Scout_s_BlackDahlia:
		case Scout_s_SandstoneSpecial:
		case Scout_s_MacabreWeb:
		case Scout_s_Nutcracker:
		case Scout_s_BlueMew:
		case Scout_s_BrainCandy:
		case Scout_s_ShottoHell:
		case Scout_s_DressedToKill:
		case Scout_s_Blitzkrieg:
			return FamilyPistol;

		case Soldier_m_FestiveRocketLauncher:
		case Soldier_m_SilverBotkillerRocketLauncherMkI:
		case Soldier_m_GoldBotkillerRocketLauncherMkI:
		case Soldier_m_RustBotkillerRocketLauncherMkI:
		case Soldier_m_BloodBotkillerRocketLauncherMkI:
		case Soldier_m_CarbonadoBotkillerRocketLauncherMkI:
		case Soldier_m_DiamondBotkillerRocketLauncherMkI:
		case Soldier_m_SilverBotkillerRocketLauncherMkII:
		case Soldier_m_GoldBotkillerRocketLauncherMkII:
		case Soldier_m_WoodlandWarrior:
		case Soldier_m_SandCannon:
		case Soldier_m_AmericanPastoral:
		case Soldier_m_SmalltownBringdown:
		case Soldier_m_ShellShocker:
		case Soldier_m_AquaMarine:
		case Soldier_m_Autumn:
		case Soldier_m_BlueMew:
		case Soldier_m_BrainCandy:
		case Soldier_m_CoffinNail:
		case Soldier_m_HighRollers:
		case Soldier_m_Warhawk:
			return FamilyRocket;

		case Soldier_s_FestiveShotgun:
		case Soldier_s_BackwoodsBoomstick:
		case Soldier_s_RusticRuiner:
		case Soldier_s_CivicDuty:
		case Soldier_s_LightningRod:
		case Soldier_s_Autumn:
		case Soldier_s_FlowerPower:
		case Soldier_s_CoffinNail:
		case Soldier_s_DressedtoKill:
		case Soldier_s_RedBear:
			return FamilyShotgun;

		case Pyro_m_FestiveFlameThrower:
		case Pyro_m_SilverBotkillerFlameThrowerMkI:
		case Pyro_m_GoldBotkillerFlameThrowerMkI:
		case Pyro_m_RustBotkillerFlameThrowerMkI:
		case Pyro_m_BloodBotkillerFlameThrowerMkI:
		case Pyro_m_CarbonadoBotkillerFlameThrowerMkI:
		case Pyro_m_DiamondBotkillerFlameThrowerMkI:
		case Pyro_m_SilverBotkillerFlameThrowerMkII:
		case Pyro_m_GoldBotkillerFlameThrowerMkII:
		case Pyro_m_ForestFire:
		case Pyro_m_BarnBurner:
		case Pyro_m_BovineBlazemaker:
		case Pyro_m_EarthSkyandFire:
		case Pyro_m_FlashFryer:
		case Pyro_m_TurbineTorcher:
		case Pyro_m_Autumn:
		case Pyro_m_PumpkinPatch:
		case Pyro_m_Nutcracker:
		case Pyro_m_Balloonicorn:
		case Pyro_m_Rainbow:
		case Pyro_m_CoffinNail:
		case Pyro_m_Warhawk:
			return FamilyFlame;

		case Demoman_m_FestiveGrenadeLauncher:
		case Demoman_m_Autumn:
		case Demoman_m_MacabreWeb:
		case Demoman_m_Rainbow:
		case Demoman_m_SweetDreams:
		case Demoman_m_CoffinNail:
		case Demoman_m_TopShelf:
		case Demoman_m_Warhawk:
		case Demoman_m_ButcherBird:
			return FamilyGrenade;

		case Demoman_s_FestiveStickybombLauncher:
		case Demoman_s_SilverBotkillerStickybombLauncherMkI:
		case Demoman_s_GoldBotkillerStickybombLauncherMkI:
		case Demoman_s_RustBotkillerStickybombLauncherMkI:
		case Demoman_s_BloodBotkillerStickybombLauncherMkI:
		case Demoman_s_CarbonadoBotkillerStickybombLauncherMkI:
		case Demoman_s_DiamondBotkillerStickybombLauncherMkI:
		case Demoman_s_SilverBotkillerStickybombLauncherMkII:
		case Demoman_s_GoldBotkillerStickybombLauncherMkII:
		case Demoman_s_SuddenFlurry:
		case Demoman_s_CarpetBomber:
		case Demoman_s_BlastedBombardier:
		case Demoman_s_RooftopWrangler:
		case Demoman_s_LiquidAsset:
		case Demoman_s_PinkElephant:
		case Demoman_s_Autumn:
		case Demoman_s_PumpkinPatch:
		case Demoman_s_MacabreWeb:
		case Demoman_s_SweetDreams:
		case Demoman_s_CoffinNail:
		case Demoman_s_DressedtoKill:
		case Demoman_s_Blitzkrieg:
			return FamilySticky;

		case Heavy_m_FestiveMinigun:
		case Heavy_m_IronCurtain:
		case Heavy_m_SilverBotkillerMinigunMkI:
		case Heavy_m_GoldBotkillerMinigunMkI:
		case Heavy_m_RustBotkillerMinigunMkI:
		case Heavy_m_BloodBotkillerMinigunMkI:
		case Heavy_m_CarbonadoBotkillerMinigunMkI:
		case Heavy_m_DiamondBotkillerMinigunMkI:
		case Heavy_m_SilverBotkillerMinigunMkII:
		case Heavy_m_GoldBotkillerMinigunMkII:
		case Heavy_m_KingoftheJungle:
		case Heavy_m_IronWood:
		case Heavy_m_AntiqueAnnihilator:
		case Heavy_m_WarRoom:
		case Heavy_m_CitizenPain:
		case Heavy_m_BrickHouse:
		case Heavy_m_MacabreWeb:
		case Heavy_m_PumpkinPatch:
		case Heavy_m_Nutcracker:
		case Heavy_m_BrainCandy:
		case Heavy_m_MisterCuddles:
		case Heavy_m_CoffinNail:
		case Heavy_m_DressedtoKill:
		case Heavy_m_TopShelf:
		case Heavy_m_ButcherBird:
			return FamilyMinigun;

		case Engi_t_FestiveWrench:
		case Engi_t_SilverBotkillerWrenchMkI:
		case Engi_t_GoldBotkillerWrenchMkI:
		case Engi_t_RustBotkillerWrenchMkI:
		case Engi_t_BloodBotkillerWrenchMkI:
		case Engi_t_CarbonadoBotkillerWrenchMkI:
		case Engi_t_DiamondBotkillerWrenchMkI:
		case Engi_t_SilverBotkillerWrenchMkII:
		case Engi_t_GoldBotkillerWrenchMkII:
		case Engi_t_Nutcracker:
		case Engi_t_Autumn:
		case Engi_t_Boneyard:
		case Engi_t_DressedtoKill:
		case Engi_t_TopShelf:
		case Engi_t_TorquedtoHell:
		case Engi_t_Airwolf:
			return FamilyWrench;

		case Medic_s_FestiveMediGun:
		case Medic_s_SilverBotkillerMediGunMkI:
		case Medic_s_GoldBotkillerMediGunMkI:
		case Medic_s_RustBotkillerMediGunMkI:
		case Medic_s_BloodBotkillerMediGunMkI:
		case Medic_s_CarbonadoBotkillerMediGunMkI:
		case Medic_s_DiamondBotkillerMediGunMkI:
		case Medic_s_SilverBotkillerMediGunMkII:
		case Medic_s_GoldBotkillerMediGunMkII:
		case Medic_s_MaskedMender:
		case Medic_s_WrappedReviver:
		case Medic_s_ReclaimedReanimator:
		case Medic_s_CivilServant:
		case Medic_s_SparkofLife:
		case Medic_s_Wildwood:
		case Medic_s_FlowerPower:
		case Medic_s_DressedToKill:
		case Medic_s_HighRollers:
		case Medic_s_Blitzkrieg:
		case Medic_s_Corsair:
			return FamilyMedigun;

		case Sniper_m_FestiveSniperRifle:
		case Sniper_m_SilverBotkillerSniperRifleMkI:
		case Sniper_m_GoldBotkillerSniperRifleMkI:
		case Sniper_m_RustBotkillerSniperRifleMkI:
		case Sniper_m_BloodBotkillerSniperRifleMkI:
		case Sniper_m_CarbonadoBotkillerSniperRifleMkI:
		case Sniper_m_DiamondBotkillerSniperRifleMkI:
		case Sniper_m_SilverBotkillerSniperRifleMkII:
		case Sniper_m_GoldBotkillerSniperRifleMkII:
		case Sniper_m_NightOwl:
		case Sniper_m_PurpleRange:
		case Sniper_m_LumberFromDownUnder:
		case Sniper_m_ShotintheDark:
		case Sniper_m_Bogtrotter:
		case Sniper_m_Thunderbolt:
		case Sniper_m_PumpkinPatch:
		case Sniper_m_Boneyard:
		case Sniper_m_Wildwood:
		case Sniper_m_Balloonicorn:
		case Sniper_m_Rainbow:
		case Sniper_m_CoffinNail:
		case Sniper_m_DressedtoKill:
		case Sniper_m_Airwolf:
			return FamilySniper;

		case Sniper_s_FestiveSMG:
		case Sniper_s_WoodsyWidowmaker:
		case Sniper_s_PlaidPotshotter:
		case Sniper_s_TreadplateTormenter:
		case Sniper_s_TeamSprayer:
		case Sniper_s_LowProfile:
		case Sniper_s_Wildwood:
		case Sniper_s_BlueMew:
		case Sniper_s_HighRollers:
		case Sniper_s_Blitzkrieg:
			return FamilySMG;

		case Spy_t_FestiveKnife:
		case Spy_t_SilverBotkillerKnifeMkI:
		case Spy_t_GoldBotkillerKnifeMkI:
		case Spy_t_RustBotkillerKnifeMkI:
		case Spy_t_BloodBotkillerKnifeMkI:
		case Spy_t_CarbonadoBotkillerKnifeMkI:
		case Spy_t_DiamondBotkillerKnifeMkI:
		case Spy_t_SilverBotkillerKnifeMkII:
		case Spy_t_GoldBotkillerKnifeMkII:
		case Spy_t_Boneyard:
		case Spy_t_BlueMew:
		case Spy_t_BrainCandy:
		case Spy_t_StabbedtoHell:
		case Spy_t_DressedtoKill:
		case Spy_t_TopShelf:
		case Spy_t_Blitzkrieg:
		case Spy_t_Airwolf:
			return FamilyKnife;

		case Spy_m_FestiveRevolver:
		case Spy_m_PsychedelicSlugger:
		case Spy_m_OldCountry:
		case Spy_m_Mayor:
		case Spy_m_DeadReckoner:
		case Spy_m_Wildwood:
		case Spy_m_MacabreWeb:
		case Spy_m_FlowerPower:
		case Spy_m_TopShelf:
		case Spy_m_Blitzkrieg:
			return FamilyRevolver;

		case Scout_m_ForceANature:
		case Scout_m_FestiveForceANature:
		case Scout_m_TheShortstop:
		case Scout_m_TheSodaPopper:
		case Scout_m_TheBackScatter:
		case Scout_m_BabyFacesBlaster:
		case Scout_s_TheWinger:
		case Scout_t_TheHolyMackerel:
		case Scout_t_FestiveHolyMackerel:
		case Soldier_m_TheBlackBox:
		case Soldier_m_FestiveBlackBox:
		case Soldier_m_TheAirStrike:
		case Soldier_s_TheReserveShooter:
		case Soldier_s_PanicAttack:
		case Soldier_t_TheDisciplinaryAction:
		case Pyro_m_TheDegreaser:
		case Pyro_m_TheBackburner:
		case Pyro_m_FestiveBackburner:
		case Pyro_s_TheDetonator:
		case Pyro_s_TheScorchShot:
		case Pyro_t_ThePowerjack:
		case Pyro_t_TheBackScratcher:
		case Demoman_m_TheLochnLoad:
		case Demoman_m_TheLooseCannon:
		case Demoman_t_TheScotsmansSkullcutter:
		case Demoman_t_HorselessHeadlessHorsemannsHeadtaker:
		case Demoman_t_TheClaidheamhMor:
		case Demoman_t_ThePersianPersuader:
		case Heavy_m_TheBrassBeast:
		case Heavy_m_Tomislav:
		case Heavy_s_TheFamilyBusiness:
		case Engi_m_TheRescueRanger:
		case Engi_t_TheJag:
		case Medic_m_CrusadersCrossbow:
		case Medic_m_FestiveCrusadersCrossbow:
		case Medic_t_TheUbersaw:
		case Medic_t_FestiveUbersaw:
		case Medic_t_Amputator:
		case Sniper_m_TheBazaarBargain:
		case Sniper_t_TheShahanshah:
			return FamilyUnique;

		default:
			return FamilyNone;
		}
	}

#ifndef TEXTMODE
	constexpr uint16_t kPaintkit = 834;
	constexpr uint16_t kWear = 725;
	constexpr uint16_t kSeedLo = 866;
	constexpr uint16_t kSeedHi = 867;
	constexpr uint16_t kInspect = 731;
	constexpr uint16_t kUnusualWeapon = 370;
	constexpr uint16_t kFestive = 2053;
	constexpr uint16_t kAustralium = 2027;
	constexpr uint16_t kLootRarity = 2022;
	constexpr uint16_t kStyleOverride = 542;
	constexpr uint16_t kKillstreakTier = 2025;
	constexpr uint16_t kKillstreakSheen = 2014;

	constexpr int kUnusualHot = 701;
	constexpr int kUnusualIsotope = 702;
	constexpr int kUnusualCool = 703;
	constexpr int kUnusualEnergyOrb = 704;

	int UnusualParticle(int iUnusual)
	{
		switch (iUnusual)
		{
		case Vars::Visuals::SkinChanger::UnusualEnum::Hot: return kUnusualHot;
		case Vars::Visuals::SkinChanger::UnusualEnum::Isotope: return kUnusualIsotope;
		case Vars::Visuals::SkinChanger::UnusualEnum::Cool: return kUnusualCool;
		case Vars::Visuals::SkinChanger::UnusualEnum::EnergyOrb: return kUnusualEnergyOrb;
		default: return 0;
		}
	}

	float IntToStupidFloat(int v)
	{
		return *reinterpret_cast<float*>(&v);
	}

	class CAttributeList
	{
	public:
		void SetAttribute(int iIndex, float flValue)
		{
			auto pSchema = CEconItemSchema::GetInstance();
			if (!pSchema)
				return;

			auto pDef = S::CEconItemSchema_GetAttributeDefinition.Call<void*>(pSchema, iIndex);
			if (!pDef)
				return;

			S::CAttributeList_SetRuntimeAttributeValue.Call<void>(this, pDef, flValue);
		}

		void SetInt(int iIndex, int iValue)
		{
			SetAttribute(iIndex, IntToStupidFloat(iValue));
		}
	};

	CAttributeList* AttributeList(CTFWeaponBase* pWeapon)
	{
		static int nOffset = U::NetVars.GetNetVar("CEconEntity", "m_AttributeList");
		if (nOffset <= 0)
			return nullptr;
		return reinterpret_cast<CAttributeList*>(uintptr_t(pWeapon) + nOffset);
	}

	void RequestFullUpdate()
	{
		if (!I::ClientState)
			return;
		if (I::ClientState->m_nDeltaTick == -1)
			I::ClientState->m_nDeltaTick = 0;
		I::ClientState->ForceFullUpdate();
	}

	void ApplySkin(CTFWeaponBase* pWeapon, const Skin_t& tSkin)
	{
		if (!pWeapon)
			return;

		int& nWeaponIndex = pWeapon->m_iItemDefinitionIndex();
		RedirectIndex(nWeaponIndex);

		auto pList = AttributeList(pWeapon);
		if (!pList)
			return;

		if (tSkin.iPaintKit)
		{
			pList->SetInt(kPaintkit, tSkin.iPaintKit);
			pList->SetAttribute(kWear, 0.f);
			pList->SetAttribute(kInspect, 1.f);
			pList->SetInt(kSeedLo, 0);
			pList->SetInt(kSeedHi, 0);
		}

		if (tSkin.bAustralium)
		{
			pList->SetInt(kAustralium, 1);
			pList->SetInt(kLootRarity, 1);
			pList->SetAttribute(kStyleOverride, 1.f);
		}

		if (tSkin.bFestive)
			pList->SetAttribute(kFestive, 1.f);

		if (tSkin.iKillstreak)
			pList->SetAttribute(kKillstreakTier, float(tSkin.iKillstreak));

		int iSheen = tSkin.iSheen;
		if (!iSheen && tSkin.iKillstreak)
			iSheen = 1;
		if (iSheen)
			pList->SetAttribute(kKillstreakSheen, float(iSheen));

		if (const int iUnusual = UnusualParticle(tSkin.iUnusual))
			pList->SetAttribute(kUnusualWeapon, float(iUnusual));
	}
#endif
}

int CSkinChanger::Key(int iDefIndex)
{
	RedirectIndex(iDefIndex);
	return iDefIndex;
}

Skin_t CSkinChanger::Get(int iKey) const
{
	auto it = m_mSkins.find(iKey);
	return it != m_mSkins.end() ? it->second : Skin_t{};
}

void CSkinChanger::Set(int iKey, const Skin_t& tSkin)
{
	if (iKey < 0)
		return;
	if (tSkin.Empty())
		m_mSkins.erase(iKey);
	else
		m_mSkins[iKey] = tSkin;
}

void CSkinChanger::GetKits(int iKey, std::vector<const char*>& vNames, std::vector<int>& vIds)
{
	vNames = { "None" };
	vIds = { 0 };
	if (iKey < 0)
		return;

	const int iFamily = PaintFamily(iKey);
	if (iFamily == FamilyNone)
		return;

	for (const auto& tKit : kKits)
	{
		if (!tKit.iId)
			continue;
		const int iMk2 = Mk2Family(tKit.iId);
		if (iMk2 && (iFamily == FamilyUnique || iMk2 != iFamily))
			continue;
		vNames.push_back(tKit.sName);
		vIds.push_back(tKit.iId);
	}
}

const char* CSkinChanger::WeaponLabel(int iKey)
{
	switch (PaintFamily(iKey))
	{
	case FamilyScattergun: return "Scattergun";
	case FamilyPistol: return "Pistol";
	case FamilyRocket: return "Rocket launcher";
	case FamilyShotgun: return "Shotgun";
	case FamilyFlame: return "Flame thrower";
	case FamilyGrenade: return "Grenade launcher";
	case FamilySticky: return "Stickybomb launcher";
	case FamilyMinigun: return "Minigun";
	case FamilyWrench: return "Wrench";
	case FamilyMedigun: return "Medigun";
	case FamilySniper: return "Sniper rifle";
	case FamilySMG: return "SMG";
	case FamilyKnife: return "Knife";
	case FamilyRevolver: return "Revolver";
	case FamilyUnique: return "This weapon";
	default: return "This weapon (no warpaints)";
	}
}

int CSkinChanger::ConfigHash() const
{
	unsigned uHash = Vars::Visuals::SkinChanger::Enabled.Value ? 1u : 0u;
	for (const auto& [iKey, tSkin] : m_mSkins)
	{
		uHash ^= unsigned(iKey) * 0x9E3779B9u;
		uHash ^= unsigned(tSkin.iPaintKit);
		uHash ^= unsigned(tSkin.bAustralium) << 16;
		uHash ^= unsigned(tSkin.bFestive) << 17;
		uHash ^= unsigned(tSkin.iKillstreak) << 18;
		uHash ^= unsigned(tSkin.iSheen) << 20;
		uHash ^= unsigned(tSkin.iUnusual) << 24;
		uHash = (uHash << 7) | (uHash >> 25);
	}
	return int(uHash);
}

#ifndef TEXTMODE

void CSkinChanger::Apply()
{
	if (!Vars::Visuals::SkinChanger::Enabled.Value)
	{
		if (m_bWasEnabled)
		{
			m_bWasEnabled = false;
			m_iLastHash = 0;
			RequestFullUpdate();
		}
		return;
	}

	m_bWasEnabled = true;

	const int iHash = ConfigHash();
	if (iHash != m_iLastHash)
	{
		m_iLastHash = iHash;
		RequestFullUpdate();
	}

	if (!I::EngineClient || !I::ClientEntityList)
		return;

	const int iLocal = I::EngineClient->GetLocalPlayer();
	if (iLocal <= 0)
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iLocal);
	if (!pEntity)
		return;

	auto pLocal = pEntity->As<CTFPlayer>();
	if (!pLocal || !pLocal->IsPlayer())
		return;

	auto& vWeapons = pLocal->m_hMyWeapons();
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		if (!vWeapons[i].IsValid())
			continue;
		auto pWeapon = vWeapons[i].Get();
		if (!pWeapon)
			continue;

		const int iKey = Key(pWeapon->m_iItemDefinitionIndex());
		auto it = m_mSkins.find(iKey);
		if (it == m_mSkins.end() || it->second.Empty())
			continue;
		ApplySkin(pWeapon, it->second);
	}
}

#else

void CSkinChanger::Apply() {}

#endif
