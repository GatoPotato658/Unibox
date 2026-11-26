#pragma once
#include "Interface.h"
#include "../../../Utils/Memory/Memory.h"

MAKE_SIGNATURE(TFInventoryManager, "client.dll", "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 83 EC ? 4C 8D 0D", 0x0);
MAKE_SIGNATURE(CTFPlayerInventory_GetFirstItemOfItemDef, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B FA 0F B7 E9", 0x0);
MAKE_SIGNATURE(CEconItemView_GetStaticData, "client.dll", "40 53 48 83 EC ? 0F B7 59 48 E8 ? ? ? ? 48 8B C8 8B D3 E8 ? ? ? ? 4C 8D 0D ? ? ? ? C7 44 24 ? ? ? ? ? 4C 8D 05 ? ? ? ? 33 D2 48 8B C8 E8 ? ? ? ? 48 83 C4 ? 5B C3 CC 48 83 C1", 0x0);
MAKE_SIGNATURE(TFItemSystem_GetItemSchema, "client.dll", "48 83 EC 28 E8 ?? ?? ?? ?? 48 83 C0 08 48 83 C4 28 C3", 0x0);
MAKE_SIGNATURE(CEconItemSchema_GetItemDefinition, "client.dll", "89 54 24 ? 53 48 83 EC 20 48 8B D9 48 8D 54 24 ? 48 81 C1 ? ? ? ? E8 ? ? ? ? 85 C0 78 ? 3B 83 ? ? ? ? 7D ? 8B D0 48 8B 83 ? ? ? ? 48 8D 0C 52 83 7C C8 ? FF 7C ? 48 8D 0C 52 48 8B 44 C8 ?", 0x0);
MAKE_SIGNATURE(CTFPlayerInventory_GetFirstItemOfItemDef_Offsets, "client.dll", "33 DB 39 5F ? 7E ? 8B C3 48 69 F0 ? ? ? ? 48 03 77 ? 48 8B 46 ? 48 8D 4E ? FF 90 ? ? ? ? 66 3B C5", 0x0);
MAKE_SIGNATURE(GetMvmTourBadgeMask, "client.dll", "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 45 33 FF 8B D9 44 89 3A 4D 8B E0 45 89 38", 0x0);

class CTFItemDefinition;

class CEconItemSchema
{
public:
	SIGNATURE_ARGS(GetItemDefinition, CTFItemDefinition*, CEconItemSchema, (int iItemIndex), this, iItemIndex);

	OFFSET(m_pMvMMissions, void*, 2320);
	OFFSET(m_iMvMMissionCount, int, 2336);
	OFFSET(m_pMvMTours, void*, 2352);
	OFFSET(m_iMvMTourCount, int, 2368);

	inline int GetItemDefCount()
	{
		static const int iOffset = S::CEconItemSchema_GetItemDefinition.ReadAt<int>(35);
		return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + iOffset);
	}

	inline void* GetItemDefEntry(int iIndex)
	{
		static const int iOffset = S::CEconItemSchema_GetItemDefinition.ReadAt<int>(46);
		uintptr_t uData = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(this) + iOffset);
		return reinterpret_cast<void*>(uData + iIndex * 24);
	}

	inline bool IsCosmeticUnusualEffect(int iEffect) const
	{
		const auto* pVector = reinterpret_cast<const uint8_t*>(this) + 0x540;
		const auto* pEffects = *reinterpret_cast<const int* const*>(pVector);
		const int iCount = *reinterpret_cast<const int*>(pVector + 0x10);
		if (!pEffects || iCount <= 0)
			return false;
		for (int i = 0; i < iCount; i++)
		{
			if (pEffects[i] == iEffect)
				return true;
		}
		return false;
	}

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
	OFFSET(m_pszImageInventory, const char*, 0x88);
	OFFSET(m_pszItemName, const char*, 0x58);
	OFFSET(m_pszItemClass, const char*, 0x168);
	OFFSET(m_pszName, const char*, 0x180);
	OFFSET(m_uEquipRegionMask, uint32_t, 0x20C);

	inline uint16 GetDefinitionIndex()
	{
		return *reinterpret_cast<uint16*>(reinterpret_cast<uintptr_t>(this) + 0x10);
	}

};

class CEconItemView
{
public:
	SIGNATURE(GetStaticData, CTFItemDefinition*, CEconItemView, this);
	inline uint16 GetDefinitionIndex()
	{
		return *reinterpret_cast<uint16*>(this + 0x48);
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
		static const int iOffset = S::CTFPlayerInventory_GetFirstItemOfItemDef_Offsets.ReadAt<uint8_t>(4);
		return *reinterpret_cast<int*>(uintptr_t(this) + iOffset);
	}

	inline CEconItemView* GetItem(int idx)
	{
		static const int iOffset = S::CTFPlayerInventory_GetFirstItemOfItemDef_Offsets.ReadAt<uint8_t>(19);
		uintptr_t uStart = *reinterpret_cast<uintptr_t*>(uintptr_t(this) + iOffset);
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
