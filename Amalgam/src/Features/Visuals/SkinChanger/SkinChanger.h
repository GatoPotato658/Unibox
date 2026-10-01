#pragma once
#include "../../../SDK/SDK.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Skin_t
{
	int iPaintKit = 0;
	int iWear = 0;
	int iQuality = -1;
	bool bAustralium = false;
	bool bFestive = false;
	int iKillstreak = 0;
	int iSheen = 0;
	int iUnusual = 0;

	bool Empty() const
	{
		return !iPaintKit && iQuality < 0 && !bAustralium && !bFestive && !iKillstreak && !iSheen && !iUnusual;
	}

	bool operator==(const Skin_t&) const = default;
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
	HeldWeapon_t m_tHeld;

	std::unordered_map<int, Tracked_t> m_mTracked;
	bool m_bWasEnabled = false;
	unsigned m_uLastHash = 0;
	bool m_bRefreshPending = false;
	float m_flDirtyTime = 0.f;
	float m_flLastRefresh = -100.f;

	void ScheduleRefresh();
	int OriginalDef(CBaseEntity* pWeapon, int iHandle, int iCurrent) const;

public:
	void Apply();
	void Service();
	void CacheMenuInfo(CTFWeaponBase* pWeapon);

	HeldWeapon_t GetHeldWeapon();
	Skin_t Get(int iKey);
	void Set(int iKey, const Skin_t& tSkin);
	std::unordered_map<int, Skin_t> GetSkins();
	void SetSkins(std::unordered_map<int, Skin_t> mSkins);

	static int Key(int iDefIndex);
	static const char* WeaponName(int iKey);
	static bool CanAustralium(int iKey);
	static bool IsGoldenPan(int iKey);
	static bool CanFestive(int iKey);
	static bool CanPaint(int iKey, bool bAllowUnsupported);
	static void GetKits(int iKey, bool bAllowUnsupported, int iCurrent, std::vector<std::string>& vNames, std::vector<int>& vIds);
};

ADD_FEATURE(CSkinChanger, SkinChanger);
