#include "../SDK/SDK.h"

MAKE_SIGNATURE(IsFreeTrialAccount, "client.dll", "48 83 EC ? E8 ? ? ? ? 48 85 C0 74 ? E8 ? ? ? ? 48 8B C8 E8 ? ? ? ?", 0x0);
MAKE_SIGNATURE(CCraftingPanel_UpdateCraftButton_IsFreeTrialAccount_Call, "client.dll", "84 C0 74 ? 41 38 7D", 0x0);

MAKE_HOOK(IsFreeTrialAccount, S::IsFreeTrialAccount(), bool)
{
	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CCraftingPanel_UpdateCraftButton_IsFreeTrialAccount_Call();

	if (dwRetAddr == dwDesired)
		return false;

	return CALL_ORIGINAL();
}
