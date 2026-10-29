#include "../SDK/SDK.h"

#ifndef TEXTMODE
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#endif

MAKE_SIGNATURE(CTFPlayerInventory_GetSOCDataForItem, "client.dll", "48 89 5C 24 08 57 48 81 EC A0 00 00 00 48 83 B9 98 00 00 00 00 48 8B FA 48 8B D9 75 13 33 C0 48 8B 9C 24 B0 00 00 00", 0x0);
MAKE_SIGNATURE(CInventoryManager_UpdateInventoryEquippedState, "client.dll", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 50 41 0F B7 F1 49 8B D8 4C 8B DA 49 83 F8 FF 74 75 44 8B 52 70 33 C0 45 85 D2", 0x0);
MAKE_SIGNATURE(CInventoryManager_UpdateInventoryPosition, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 44 8B 5A 70 33 DB 45 8B F9 49 8B E8 48 8B F2 48", 0x0);
MAKE_SIGNATURE(CInventoryManager_DropItem, "client.dll", "40 55 41 55 41 56 41 57 48 8D 6C 24 C1 48 81 EC B8 00 00 00 8B 05 ? ? ? ? 45 33 FF 4C 8B F2 4C 8B E9", 0x0);
MAKE_SIGNATURE(CGCClient_SendMessage, "client.dll", "48 83 EC 28 44 8B 4A 18 4C 8B 42 08 41 83 E9 0C 48 8B 49 08 41 8B 10 48 85 C9 74 13 48 8B 01 49 83 C0 0C FF 10 85 C0 0F 94 C0 48 83 C4 28 C3", 0x0);

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
