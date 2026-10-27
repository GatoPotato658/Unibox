#ifndef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CHudItemEffectMeter_Weapon_C_TFWeaponBase_IsEnabled, "client.dll", "48 83 EC ? E8 ? ? ? ? 48 85 C0 74 ? 45 33 C9 C6 44 24 ? ? 4C 8B C0 48 8D 15 ? ? ? ? 33 C9 E8 ? ? ? ? 85 C0 0F 95 C0 48 83 C4 ? C3 48 83 C4 ? C3 CC CC CC CC CC CC CC CC CC CC 8B 91", 0x0);

// Enables killstreak count in hud
// Probably the least bloated thing in this cheat after my updates
MAKE_HOOK(CHudItemEffectMeter_Weapon_C_TFWeaponBase_IsEnabled, S::CHudItemEffectMeter_Weapon_C_TFWeaponBase_IsEnabled(), bool)
{
	DEBUG_RETURN(CHudItemEffectMeter_Weapon_C_TFWeaponBase_IsEnabled);

	return Vars::Visuals::Other::KillstreakWeapons.Value && !SDK::CleanScreenshot() ? true : CALL_ORIGINAL();
}
#endif