#pragma once
#include "../../../SDK/SDK.h"

#include <unordered_map>
#include <vector>

struct Skin_t
{
	int iPaintKit = 0;
	bool bAustralium = false;
	bool bFestive = false;
	int iKillstreak = 0;
	int iSheen = 0;
	int iUnusual = 0;

	bool Empty() const
	{
		return !iPaintKit && !bAustralium && !bFestive && !iKillstreak && !iSheen && !iUnusual;
	}
};

class CSkinChanger
{
	bool m_bWasEnabled = false;
	int m_iLastHash = 0;

	int ConfigHash() const;

public:
	std::unordered_map<int, Skin_t> m_mSkins;

	void Apply();
	int Key(int iDefIndex);
	Skin_t Get(int iKey) const;
	void Set(int iKey, const Skin_t& tSkin);
	void GetKits(int iKey, std::vector<const char*>& vNames, std::vector<int>& vIds);
	const char* WeaponLabel(int iKey);
};

ADD_FEATURE(CSkinChanger, SkinChanger);
