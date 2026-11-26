#ifndef TEXTMODE
#include "../SDK/SDK.h"


// Enables killstreak count in hud
// Probably the least bloated thing in this cheat after my updates
MAKE_HOOK(CHudItemEffectMeter_Weapon_C_TFWeaponBase_IsEnabled, S::CHudItemEffectMeter_Weapon_C_TFWeaponBase_IsEnabled(), bool)
{
	DEBUG_RETURN(CHudItemEffectMeter_Weapon_C_TFWeaponBase_IsEnabled);

	return Vars::Visuals::Other::KillstreakWeapons.Value && !SDK::CleanScreenshot() ? true : CALL_ORIGINAL();
}
#endif