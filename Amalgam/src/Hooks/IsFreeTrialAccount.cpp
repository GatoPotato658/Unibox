#include "../SDK/SDK.h"

MAKE_SIGNATURE(IsFreeTrialAccount, "client.dll", "48 83 EC ? E8 ? ? ? ? 48 85 C0 74 ? E8 ? ? ? ? 48 8B C8 E8 ? ? ? ?", 0x0);
MAKE_SIGNATURE(CCraftingPanel_UpdateCraftButton_IsFreeTrialAccount_Call, "client.dll", "4C 8B E8 48 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 41 38 7D ? 74 ? 32 DB 49 8B 8F", 0x11);

MAKE_HOOK(IsFreeTrialAccount, S::IsFreeTrialAccount(), bool)
{
	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CCraftingPanel_UpdateCraftButton_IsFreeTrialAccount_Call();

	if (dwRetAddr == dwDesired && Vars::Misc::Exploits::PremiumCraftingBypass.Value)
		return false;

	return CALL_ORIGINAL();
}
