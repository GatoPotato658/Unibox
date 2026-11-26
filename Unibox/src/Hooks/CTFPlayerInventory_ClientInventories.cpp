#include "../SDK/SDK.h"

#ifndef TEXTMODE
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#endif


#ifndef TEXTMODE

MAKE_HOOK(CTFPlayerInventory_GetSOCDataForItem, S::CTFPlayerInventory_GetSOCDataForItem(), void*,
	void* rcx, uint64_t uItemID, uint64_t a3, uint64_t a4)
{
	DEBUG_RETURN(CTFPlayerInventory_GetSOCDataForItem, rcx, uItemID, a3, a4);

	if (F::SkinChanger.IsClientItem(uItemID))
		return F::SkinChanger.GetClientSOCData(uItemID);

	return CALL_ORIGINAL(rcx, uItemID, a3, a4);
}

MAKE_HOOK(CInventoryManager_UpdateInventoryEquippedState, S::CInventoryManager_UpdateInventoryEquippedState(), void,
	void* rcx, void* pInventory, uint64_t uItemID, uint16_t iClass, uint16_t iSlot)
{
	DEBUG_RETURN(CInventoryManager_UpdateInventoryEquippedState, rcx, pInventory, uItemID, iClass, iSlot);

	if (F::SkinChanger.ClientInventoryActive())
	{
		if (!F::SkinChanger.IsClientItem(uItemID) && iSlot <= 2)
			CALL_ORIGINAL(rcx, pInventory, uItemID, iClass, iSlot);
		F::SkinChanger.ApplyLocalLoadout(uItemID, iClass, iSlot);
		return;
	}

	CALL_ORIGINAL(rcx, pInventory, uItemID, iClass, iSlot);
}

void EquipOwnedInventoryWeapon(CTFPlayerInventory* pInventory, uint64_t uItemID, int iClass, int iSlot)
{
	if (!pInventory || !uItemID || F::SkinChanger.IsClientItem(uItemID))
		return;
	using namespace Hooks::CInventoryManager_UpdateInventoryEquippedState;
	if (Hook.m_pOriginal)
		Hook.As<FN>()(I::TFInventoryManager(), pInventory, uItemID, uint16_t(iClass), uint16_t(iSlot));
}

MAKE_HOOK(CInventoryManager_UpdateInventoryPosition, S::CInventoryManager_UpdateInventoryPosition(), void,
	void* rcx, void* pInventory, uint64_t uItemID, uint32_t uPosition)
{
	DEBUG_RETURN(CInventoryManager_UpdateInventoryPosition, rcx, pInventory, uItemID, uPosition);

	if (F::SkinChanger.ClientInventoryActive())
	{
		F::SkinChanger.ApplyLocalPosition(uItemID, uPosition);
		return;
	}

	CALL_ORIGINAL(rcx, pInventory, uItemID, uPosition);
}

MAKE_HOOK(CGCClient_SendMessage, S::CGCClient_SendMessage(), bool,
	void* rcx, void* pMessage)
{
	DEBUG_RETURN(CGCClient_SendMessage, rcx, pMessage);

	if (F::SkinChanger.OnSendMessage(pMessage))
		return true;

	return CALL_ORIGINAL(rcx, pMessage);
}

MAKE_HOOK(CInventoryManager_DropItem, S::CInventoryManager_DropItem(), void,
	void* rcx, uint64_t uItemID)
{
	DEBUG_RETURN(CInventoryManager_DropItem, rcx, uItemID);

	if (F::SkinChanger.ClientInventoryActive())
	{
		F::SkinChanger.ApplyLocalDelete(uItemID);
		return;
	}

	CALL_ORIGINAL(rcx, uItemID);
}

#endif
