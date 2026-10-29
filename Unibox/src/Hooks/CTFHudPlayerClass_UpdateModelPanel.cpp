#include "../SDK/SDK.h"

#ifndef TEXTMODE
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#endif

MAKE_SIGNATURE(CTFHudPlayerClass_UpdateModelPanel, "client.dll", "40 57 48 83 EC ? 80 B9 ? ? ? ? ? 48 8B F9 0F 84 ? ? ? ? 4C 89 74 24", 0x0);

#ifndef TEXTMODE
MAKE_SIGNATURE(CTFPlayerModelPanel_AddCarriedItem, "client.dll", "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F1 48 8B FA B9 58 01 00 00", 0x0);

namespace
{
	void* s_pHudClassPanel = nullptr;
	bool s_bBuildingHudClassModel = false;
}
#endif

MAKE_HOOK(CTFHudPlayerClass_UpdateModelPanel, S::CTFHudPlayerClass_UpdateModelPanel(), void,
	void* rcx)
{
#ifndef TEXTMODE
	DEBUG_RETURN(CTFHudPlayerClass_UpdateModelPanel, rcx);
	s_pHudClassPanel = rcx;
	const bool bPrevious = s_bBuildingHudClassModel;
	s_bBuildingHudClassModel = true;
	CALL_ORIGINAL(rcx);
	s_bBuildingHudClassModel = bPrevious;
#endif
}

#ifndef TEXTMODE
MAKE_HOOK(CTFPlayerModelPanel_AddCarriedItem, S::CTFPlayerModelPanel_AddCarriedItem(), int,
	void* rcx, CEconItemView* pItem)
{
	DEBUG_RETURN(CTFPlayerModelPanel_AddCarriedItem, rcx, pItem);
	// The HUD enumerates every wearable, including EF_NODRAW entities. Filter
	// only its original, replaced items; other model panels retain vanilla behavior.
	if (s_bBuildingHudClassModel && pItem && F::SkinChanger.IsReplacedWearableItem(pItem->UUID()))
		return -1;
	return CALL_ORIGINAL(rcx, pItem);
}

void RefreshInventoryHudModel()
{
	if (s_pHudClassPanel)
		S::CTFHudPlayerClass_UpdateModelPanel.Call<void>(s_pHudClassPanel);
}

void ResetInventoryHudModel()
{
	s_pHudClassPanel = nullptr;
	s_bBuildingHudClassModel = false;
}
#endif
