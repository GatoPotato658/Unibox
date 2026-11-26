#include "../SDK/SDK.h"

#ifndef TEXTMODE
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#endif


#ifndef TEXTMODE

namespace
{
	std::atomic<bool> s_bInventoryHudRefreshPending = false;
	bool s_bBuildingHudClassModel = false;
}
#endif

MAKE_HOOK(CTFHudPlayerClass_UpdateModelPanel, S::CTFHudPlayerClass_UpdateModelPanel(), void,
	void* rcx)
{
#ifndef TEXTMODE
	DEBUG_RETURN(CTFHudPlayerClass_UpdateModelPanel, rcx);
	const bool bRefreshPending = s_bInventoryHudRefreshPending.exchange(false);
	const bool bPrevious = s_bBuildingHudClassModel;
	s_bBuildingHudClassModel = true;
	CALL_ORIGINAL(rcx);
	if (bRefreshPending)
		CALL_ORIGINAL(rcx);
	s_bBuildingHudClassModel = bPrevious;
#endif
}

#ifndef TEXTMODE
MAKE_HOOK(CTFPlayerModelPanel_AddCarriedItem, S::CTFPlayerModelPanel_AddCarriedItem(), int,
	void* rcx, CEconItemView* pItem)
{
	DEBUG_RETURN(CTFPlayerModelPanel_AddCarriedItem, rcx, pItem);
	if (s_bBuildingHudClassModel && pItem && F::SkinChanger.IsReplacedWearableItem(pItem->UUID()))
		return -1;
	return CALL_ORIGINAL(rcx, pItem);
}

void RefreshInventoryHudModel()
{
	s_bInventoryHudRefreshPending.store(true);
}

void ResetInventoryHudModel()
{
	s_bInventoryHudRefreshPending.store(false);
	s_bBuildingHudClassModel = false;
}
#endif
