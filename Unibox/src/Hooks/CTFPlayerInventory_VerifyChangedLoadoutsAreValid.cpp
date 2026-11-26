#include "../SDK/SDK.h"


MAKE_HOOK(CTFPlayerInventory_VerifyChangedLoadoutsAreValid, S::CTFPlayerInventory_VerifyChangedLoadoutsAreValid(), void,
	void* rcx)
{
	DEBUG_RETURN(CTFPlayerInventory_VerifyChangedLoadoutsAreValid, rcx);

	if (!Vars::Misc::Exploits::EquipRegionUnlock.Value)
		CALL_ORIGINAL(rcx);
}

MAKE_HOOK(GenerateEquipRegionConflictMask, S::GenerateEquipRegionConflictMask(), uint32_t,
	int iClass, int iUpToSlot, int iIgnoreSlot)
{
	DEBUG_RETURN(CTFPlayerInventory_VerifyChangedLoadoutsAreValid, iClass, iUpToSlot, iIgnoreSlot);

	if (Vars::Misc::Exploits::EquipRegionUnlock.Value)
		return 0;

	return CALL_ORIGINAL(iClass, iUpToSlot, iIgnoreSlot);
}

MAKE_HOOK(CTFInventoryManager_GetItemInLoadoutForClass, S::CTFInventoryManager_GetItemInLoadoutForClass(), void*,
	void* rcx, int iClass, int iSlot, CSteamID* pID)
{
	DEBUG_RETURN(CTFPlayerInventory_VerifyChangedLoadoutsAreValid, rcx, iClass, iSlot, pID);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CEquipSlotItemSelectionPanel_UpdateModelPanelsForSelection_GetItemInLoadoutForClass_Call();

	if (dwRetAddr == dwDesired && Vars::Misc::Exploits::EquipRegionUnlock.Value)
		return nullptr;

	return CALL_ORIGINAL(rcx, iClass, iSlot, pID);
}
