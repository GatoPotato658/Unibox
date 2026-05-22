#pragma once
#include "../../../SDK/SDK.h"

struct Text_t
{
	int m_iMode = ALIGN_TOP;
	std::string m_sText = "";
	Color_t m_tColor = {};
	Color_t m_tOutline = {};
	byte m_ucBackgroundAlpha = -1;
};

struct Bar_t
{
	int m_iMode = ALIGN_TOP;
	float m_flPercent = 1.f;
	Color_t m_tColor = {};
	Color_t m_tOverfill = {};
	Color_t m_tBackground = Color_t(0, 0, 0, 0);
	bool m_bSmooth = false;
};

struct EntityCache_t
{
	float m_flAlpha = 1.f;
	std::vector<Text_t> m_vText = {};
	Color_t m_tColor = {};
	bool m_bBox = false;
};

struct BuildingCache_t : EntityCache_t
{
	std::vector<Bar_t> m_vBars = {};
	float m_flHealth = 1.f;
};

struct PlayerCache_t : BuildingCache_t
{
	bool m_bBones = false;
	int m_iClassIcon = 0;
	CHudTexture* m_pWeaponIcon = nullptr;
};

struct BarKey
{
	CBaseEntity* m_pEntity = nullptr;
	uint32_t m_uIndex = 0;

	bool operator==(const BarKey& other) const noexcept
	{
		return m_pEntity == other.m_pEntity && m_uIndex == other.m_uIndex;
	}
};

struct BarKeyHasher
{
	size_t operator()(const BarKey& key) const noexcept
	{
		size_t h1 = std::hash<CBaseEntity*>{}(key.m_pEntity);
		size_t h2 = std::hash<uint32_t>{}(key.m_uIndex);
		return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
	}
};

class CESP
{
private:
	void DrawPlayers();
	void DrawBuildings();
	void DrawWorld();
	
	bool GetDrawBounds(CBaseEntity* pEntity, float& x, float& y, float& w, float& h);

	void DrawBones(CTFPlayer* pPlayer, matrix3x4* aBones, std::vector<int> vBones, Color_t tColor);
	float SmoothBarValue(const BarKey& tKey, float flTarget);
	void CleanupSmoothedBars();

	std::unordered_map<CBaseEntity*, PlayerCache_t> m_mPlayerCache = {};
	std::unordered_map<CBaseEntity*, BuildingCache_t> m_mBuildingCache = {};
	std::unordered_map<CBaseEntity*, EntityCache_t> m_mEntityCache = {};
	std::unordered_map<BarKey, float, BarKeyHasher> m_mBarSmoothing = {};
	std::unordered_set<BarKey, BarKeyHasher> m_sBarsSeenThisFrame = {};

public:
	void Store(CTFPlayer* pLocal);
	void Draw();
};

ADD_FEATURE(CESP, ESP);