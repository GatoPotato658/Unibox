#pragma once
#include "../../../SDK/SDK.h"

#include <mutex>
#include <array>
#include <map>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

class CEconItem;

struct ItemInfo_t
{
	uint16_t iDef = 0;
	std::string sName;
	std::string sClass;
	std::string sImage;
	std::string sImageOverlay;
	std::string sToolTargetImage;
	float flToolIconX = 0.285f, flToolIconY = 0.43f, flToolIconScale = 0.35f;
	std::string sExtraModel, sExtraModelVM;
	std::vector<std::pair<int, std::string>> vStyles;
	int iPaintColor = -1;
	int iPaintColor2 = -1;
	bool bPaintable = false;
	bool bHat = false;
};

struct BackpackItem_t
{
	uint32_t uSerial = 0;
	uint16_t iDef = 0;
	int iUnusual = 0;
	int iPaintColor = -1;
	int iPaintColor2 = -1;
	int iStyle = 0;
};

struct Skin_t
{
	int iPaintKit = -1;
	int iWear = 0;
	int iSeed = 0;
	int iQuality = -1;
	int iStatTrak = -1;
	int iKillEffect = -1;
	int iPaintColor = -1;
	int iBotkiller = 0;
	bool bAustralium = false;
	bool bFestive = false;
	int iKillstreak = 0;
	int iSheen = 0;
	int iUnusual = 0;

	bool Empty() const
	{
		return iPaintKit < 0 && iQuality < 0 && !bAustralium && !bFestive && iKillstreak <= 0 && iSheen <= 0 && iUnusual <= 0 && iBotkiller <= 0 && iStatTrak < 0 && iKillEffect <= 0 && iPaintColor < 0;
	}

	bool operator==(const Skin_t&) const = default;
};

enum EWeaponUnusual
{
	WeaponUnusualOff = 0,
	WeaponUnusualHot,
	WeaponUnusualIsotope,
	WeaponUnusualCool,
	WeaponUnusualEnergyOrb
};

struct HeldWeapon_t
{
	bool bValid = false;
	int iKey = -1;
};

class CSkinChanger
{
	struct Tracked_t
	{
		CBaseEntity* pEntity = nullptr;
		int iOriginal = -1;
		int iApplied = -1;
	};

	std::mutex m_tMutex;
	std::unordered_map<int, Skin_t> m_mSkins;
	std::vector<BackpackItem_t> m_vBackpack = {};
	uint32_t m_uNextSerial = 1;
	bool m_bInventoryLoaded = false;
	std::unordered_map<uint64_t, void*> m_mClientItemsByID = {};
	std::unordered_set<uint64_t> m_tFailedItems = {};
	std::unordered_map<uint64_t, int> m_mAppliedUnusual = {};
	std::vector<void*> m_vGraveyard = {};
	std::map<int, uint64_t> m_mLoadout = {};
	std::map<int, uint64_t> m_mOriginalLoadout = {};
	std::unordered_map<uint16_t, int> m_mInventoryCounts = {};
	uint64_t m_uLastSync = 0;
	struct PendingCrate_t
	{
		uint64_t uKey = 0;
		uint64_t uCrate = 0;
		uint64_t uTick = 0;
	};
	std::vector<PendingCrate_t> m_vPendingCrates = {};
	std::vector<PendingCrate_t> m_vPendingPaints = {};
	std::unordered_map<uint64_t, uint64_t> m_mAwaitingPosition = {};
	HeldWeapon_t m_tHeld;

	struct Wearable_t
	{
		CHandle<CEconWearable> hEntity = {};
		int iDef = -1;
		int iClass = -1;
		int iTeam = -1;
		int iUnusual = -1;
		int iPaintColor = -1;
		int iPaintColor2 = -1;
		int iStyle = -1;
	};
	struct LoadoutView_t
	{
		alignas(16) uint8_t aData[SIZE_OF_ITEMVIEW] = {};
	};
	std::array<Wearable_t, 19> m_aWearables = {};
	std::array<CHandle<CEconWearable>, 8> m_aHiddenWearables = {};
	std::array<LoadoutView_t, 19> m_aWearableViews = {};
	uint64_t m_aWearableViewKey[19] = {};
	bool m_aWearableViewBuilt[19] = {};
	CHandle<CTFPlayer> m_hWearableOwner = {};

	std::unordered_map<int, Tracked_t> m_mTracked;
	bool m_bWasEnabled = false;
	unsigned m_uLastHash = 0;
	bool m_bRefreshPending = false;
	float m_flDirtyTime = 0.f;
	float m_flLastRefresh = -100.f;

	void ScheduleRefresh();
	int OriginalDef(CBaseEntity* pWeapon, int iHandle, int iCurrent) const;
	void RunFrame();
	CTFPlayerInventory* GetLocalInventory() const;
	void SyncInventory();
	void ClearClientItems();
	void RemoveClientItem(CTFPlayerInventory* pInventory, uint64_t uItemID);
	void ReapplyLoadout(CTFPlayerInventory* pInventory);
	uint32_t NextSerial();
	int GetItemUnusual(uint64_t uItemID);
	void ProcessCrates(CTFPlayerInventory* pInventory);
	bool OpenCrate(CTFPlayerInventory* pInventory, const PendingCrate_t& tCrate);
	bool ApplyLocalPaint(CTFPlayerInventory* pInventory, const PendingCrate_t& tPaint);
	void UpdateWearables(CTFPlayer* pLocal);
	void UpdateWeaponVisuals(CTFPlayer* pLocal);
	void RestoreWeaponVisuals();
	void ResetWearables(CTFPlayer* pLocal);
	void RestoreHiddenWearables();
	void HideReplacedWearables(CTFPlayer* pLocal, int iClass);
	void RemoveWearable(CTFPlayer* pLocal, Wearable_t& tWearable);
	CEconItemView* GetWearableView(int iClass, int iSlot, int iTeam, int iDef, int iUnusual, int iPaintColor, int iPaintColor2, int iStyle);
	CEconWearable* CreateWearable(CTFPlayer* pLocal, CEconItemView* pView, int iClass, int iTeam);
	bool AppendWearable(CTFPlayer* pLocal, CEconWearable* pWearable);

public:
	void Apply();
	void Service();
	void CacheMenuInfo(CTFWeaponBase* pWeapon);
	void OnLevelShutdown();
	void Unload();
	void ClearWearables();

	bool ClientInventoryActive();
	bool IsClientItem(uint64_t uItemID);
	void* GetClientSOCData(uint64_t uItemID);
	bool OnSendMessage(void* pMessage);
	uint64_t FindOwnedWeapon(uint64_t uClientItemID);
	bool IsReplacedWearableItem(uint64_t uItemID);
	void ApplyLocalLoadout(uint64_t uItemID, int iClass, int iSlot);
	std::map<int, uint64_t> GetLoadout();
	void SetLoadout(std::map<int, uint64_t> mLoadout);
	void ApplyLocalPosition(uint64_t uItemID, uint32_t uPosition);
	void ApplyLocalDelete(uint64_t uItemID);

	HeldWeapon_t GetHeldWeapon();
	Skin_t Get(int iKey);
	void Set(int iKey, const Skin_t& tSkin);
	void Clear();
	std::unordered_map<int, Skin_t> GetSkins();
	void SetSkins(std::unordered_map<int, Skin_t> mSkins);

	void RequestFullUpdate();

	bool InBackpack(uint16_t iDef);
	void AddToBackpack(uint16_t iDef, int iUnusual = 0, int iStyle = 0);
	void SetBackpackStyle(uint32_t uSerial, int iStyle);
	bool QueueLocalCrate(uint16_t iDef);
	void RemoveFromBackpack(uint16_t iDef);
	void ClearBackpack();
	std::vector<BackpackItem_t> GetBackpack();
	void SetBackpack(std::vector<BackpackItem_t> vBackpack);
	std::vector<const ItemInfo_t*> GetInventoryItems();
	bool IsInventoryLoading();
	int GetInventoryCount(uint16_t iDef);

	static const std::vector<ItemInfo_t>& GetItems();
	static bool IsCatalogueLoading();

	static int Key(int iDefIndex);
	static const char* WeaponName(int iKey);
	static bool CanAustralium(int iKey);
	static bool IsGoldenPan(int iKey);
	static bool CanFestive(int iKey);
	static bool CanPaint(int iKey, bool bAllowUnsupported);
	static bool CanBotkiller(int iKey);
	static void GetKits(int iKey, bool bAllowUnsupported, int iCurrent, std::vector<std::string>& vNames, std::vector<int>& vIds);
};

ADD_FEATURE(CSkinChanger, SkinChanger);

void EquipOwnedInventoryWeapon(CTFPlayerInventory* pInventory, uint64_t uItemID, int iClass, int iSlot);
void RefreshInventoryHudModel();
void ResetInventoryHudModel();
