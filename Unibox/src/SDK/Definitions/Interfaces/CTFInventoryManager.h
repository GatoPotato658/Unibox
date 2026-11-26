#pragma once
#include "Interface.h"
#include "../../../Utils/Memory/Memory.h"



class CTFItemDefinition;

class CEconItemSchema
{
public:
	SIGNATURE_ARGS(GetItemDefinition, CTFItemDefinition*, CEconItemSchema, (int iItemIndex), this, iItemIndex);

	OFFSET_SIG(m_pMvMMissions, void*, CEconItemSchema_MvMOffsets, int, 32);
	OFFSET_SIG(m_iMvMMissionCount, int, CEconItemSchema_MvMOffsets, int, 2);
	OFFSET_SIG_ADD(m_pMvMTours, void*, CEconItemSchema_MvMOffsets, int, 2, 16);
	OFFSET_SIG_ADD(m_iMvMTourCount, int, CEconItemSchema_MvMOffsets, int, 2, 32);

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
		static const int iOffset = S::CEconItemSchema_CosmeticUnusualOffset.ReadAt<int>(22);
		const auto* pVector = reinterpret_cast<const uint8_t*>(this) + iOffset;
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
	OFFSET_SIG(m_pTauntData, void*, CTFItemDefinition_TauntDataOffset, int, 10);
	OFFSET_SIG(m_pszImageInventory, const char*, CTFItemDefinition_ImageInventoryOffset, int, 29);
	OFFSET_SIG(m_pszItemName, const char*, CTFItemDefinition_ItemNameOffset, uint8_t, 33);
	OFFSET_SIG(m_pszItemClass, const char*, CTFItemDefinition_ItemClassOffset, int, 3);
	OFFSET_SIG(m_pszName, const char*, CTFItemDefinition_NameOffset, int, 3);
	OFFSET_SIG(m_uEquipRegionMask, uint32_t, CTFItemDefinition_EquipRegionMaskOffset, int, 12);

	inline uint16 GetDefinitionIndex()
	{
		static const int iOffset = S::CTFItemDefinition_DefIndexOffset.ReadAt<uint8_t>(8);
		return *reinterpret_cast<uint16*>(reinterpret_cast<uintptr_t>(this) + iOffset);
	}

};

class CEconItemView
{
public:
	SIGNATURE(GetStaticData, CTFItemDefinition*, CEconItemView, this);
	inline uint16 GetDefinitionIndex()
	{
		static const int iOffset = S::CEconItemView_AssignOffsets.ReadAt<uint8_t>(3);
		return *reinterpret_cast<uint16*>(reinterpret_cast<uintptr_t>(this) + iOffset);
	}

	inline uint64_t UUID()
	{
		static const int iOffset = S::CEconItemView_AssignOffsets.ReadAt<uint8_t>(21);
		uint64_t value = *reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(this) + iOffset);
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

	static inline int GetItemViewSize()
	{
		static const int iSize = S::CTFPlayerInventory_GetFirstItemOfItemDef_Offsets.ReadAt<int>(12);
		return iSize;
	}

	inline int GetItemCount() 
	{
		static const int iOffset = S::CTFPlayerInventory_GetFirstItemOfItemDef_Offsets.ReadAt<uint8_t>(4);
		return *reinterpret_cast<int*>(uintptr_t(this) + iOffset);
	}

	inline CEconItemView* GetItem(int idx)
	{
		static const int iOffset = S::CTFPlayerInventory_GetFirstItemOfItemDef_Offsets.ReadAt<uint8_t>(19);
		uintptr_t uStart = *reinterpret_cast<uintptr_t*>(uintptr_t(this) + iOffset);
		uintptr_t nOffset = static_cast<uintptr_t>(idx) * GetItemViewSize();
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
