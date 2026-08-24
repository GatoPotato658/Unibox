#pragma once
#include "Interface.h"
#include "../../../Utils/Memory/Memory.h"

MAKE_SIGNATURE(TFInventoryManager, "client.dll", "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 83 EC ? 4C 8D 0D", 0x0);
MAKE_SIGNATURE(CTFPlayerInventory_GetFirstItemOfItemDef, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B FA 0F B7 E9", 0x0);
MAKE_SIGNATURE(CEconItemView_GetStaticData, "client.dll", "40 53 48 83 EC ? 0F B7 59 ? E8 ? ? ? ? 48 8B C8 8B D3 E8 ? ? ? ? 4C 8D 0D ? ? ? ? C7 44 24 ? ? ? ? ? 4C 8D 05 ? ? ? ? 33 D2 48 8B C8 E8 ? ? ? ? 48 83 C4 ? 5B C3 CC 48 83 C1", 0x0);
MAKE_SIGNATURE(TFItemSystem_GetItemSchema, "client.dll", "48 83 EC 28 E8 ?? ?? ?? ?? 48 83 C0 08 48 83 C4 28 C3", 0x0);
MAKE_SIGNATURE(GetMvmTourBadgeMask, "client.dll", "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 45 33 FF 8B D9 44 89 3A 4D 8B E0 45 89 38", 0x0);

class CEconItemSchema
{
public:
	OFFSET(m_pMvMMissions, void*, 2200);
	OFFSET(m_iMvMMissionCount, int, 2216);
	OFFSET(m_pMvMTours, void*, 2232);
	OFFSET(m_iMvMTourCount, int, 2248);

	static CEconItemSchema* GetInstance()
	{
		return S::TFItemSystem_GetItemSchema.Call<CEconItemSchema*>();
	}

	static uint32_t GetTourCompletedMask(const int iTourIndex)
	{
		if (!S::GetMvmTourBadgeMask())
			return 0;

		int iUnused = 0;
		uint32_t uMask = 0;
		if (!S::GetMvmTourBadgeMask.Call<bool>(iTourIndex, &iUnused, &uMask))
			return 0;

		return uMask;
	}
};

class CTFItemDefinition
{
public:
	OFFSET(m_pTauntData, void*, 0x2A8);
};

class CEconItemView
{
public:
	SIGNATURE(GetStaticData, CTFItemDefinition*, CEconItemView, this);
	inline uint16 GetDefinitionIndex()
	{
		return *reinterpret_cast<uint16*>(this + 48);
	}

	inline uint64_t UUID()
	{
		uint64_t value = *reinterpret_cast<uint64_t*>(this + 96);
		auto a = value >> 32;
		auto b = value << 32;
		return b | a;
	}
};

#define SIZE_OF_ITEMVIEW 344
class CTFPlayerInventory
{
public:
	SIGNATURE_ARGS(GetFirstItemOfItemDef, CEconItemView*, CTFPlayerInventory, (uint16 iItemDef), iItemDef, this);
	VIRTUAL_ARGS(GetItemInLoadout, CEconItemView*, 10, (int iClass, int iSlot), this, iClass, iSlot);

	inline int GetItemCount() 
	{
		return *reinterpret_cast<int*>(this + 112);
	}

	inline CEconItemView* GetItem(int idx)
	{
		uintptr_t uStart = *(uintptr_t*)(this + 96);
		uintptr_t nOffset = idx * SIZE_OF_ITEMVIEW;
		return reinterpret_cast<CEconItemView*>(uStart + nOffset);
	}

	inline std::vector<uint64_t> GetItemsOfItemDef(uint16 iItemDef)
	{
		std::vector<uint64_t> vUUIDs;
		for (int i = 0; i < GetItemCount(); i++)
		{
			auto pItem = GetItem(i);
			if (pItem->GetDefinitionIndex() == iItemDef)
				vUUIDs.push_back(pItem->UUID());
		}
		return vUUIDs;
	}
};

class CTFInventoryManager
{
public:
	VIRTUAL_ARGS(EquipItemInLoadout, bool, 19, (int iClass, int iSlot, uint64_t uniqueid), this, iClass, iSlot, uniqueid);
	VIRTUAL(GetLocalInventory, CTFPlayerInventory*, 23, this);
};

namespace I
{
	inline CTFInventoryManager* TFInventoryManager()
	{
		return S::TFInventoryManager.Call<CTFInventoryManager*>();
	}
};