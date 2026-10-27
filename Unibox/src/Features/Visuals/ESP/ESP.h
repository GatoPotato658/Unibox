#pragma once
#include "../../../SDK/SDK.h"
#include "../../ImGui/IndicatorCache.h"

struct ImDrawList;
void InvalidateESPIconTextures();

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

enum class EESPBadge
{
	None,
	Class,
	Weapon
};

struct Badge_t
{
	EESPBadge m_eType = EESPBadge::None;
	std::string m_sTexture = "";
	Color_t m_tColor = {};
	float m_flW = 0.f, m_flH = 0.f;
	float m_flU0 = 0.f, m_flV0 = 0.f, m_flU1 = 1.f, m_flV1 = 1.f;
};

struct EntityCache_t
{
	float m_flAlpha = 1.f;
	std::vector<Text_t> m_vText = {};
	Color_t m_tColor = {};
	bool m_bBox = false;
	int m_iBoxStyle = 0;
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

struct ESPDrawEntity_t
{
	const void* m_pKey = nullptr;

	float m_flX = 0.f, m_flY = 0.f, m_flW = 0.f, m_flH = 0.f;
	float m_flAlpha = 1.f;
	Color_t m_tColor = {};
	bool m_bBox = false;
	int m_iBoxStyle = 0;
	float m_flHealth = 1.f;

	std::vector<Text_t> m_vText = {};
	std::vector<Bar_t> m_vBars = {};
	std::vector<Badge_t> m_vBadges = {};
	std::vector<std::pair<Vec2, Vec2>> m_vBones = {};
};

struct ESPDrawCache_t
{
	std::vector<ESPDrawEntity_t> m_vPlayers = {};
	std::vector<ESPDrawEntity_t> m_vBuildings = {};
	std::vector<ESPDrawEntity_t> m_vWorld = {};
};

struct ESPAnim_t
{
	float m_flAppear = 0.f;
	float m_flHealthGhost = -1.f;
	std::vector<float> m_vBars = {};
	float m_flLastSeen = 0.f;
};

struct ESPMotion_t
{
	Vec3 m_vPos = {};
	Vec3 m_vVelocity = {};
	Vec3 m_vMins = {};
	Vec3 m_vMaxs = {};
	double m_dTime = 0.0;
};

class CESP
{
private:
	void DrawEntities(ImDrawList* pDrawList, const std::vector<ESPDrawEntity_t>& vEntities, bool bPlayers);

	bool GetDrawBounds(CBaseEntity* pEntity, bool bSmooth, double dNow, Vec3& vOffset, float& x, float& y, float& w, float& h);
	void CacheBones(CTFPlayer* pPlayer, const Vec3& vOffset, ESPDrawEntity_t& tOut);
	void CacheBoneChain(CTFPlayer* pPlayer, matrix3x4* aBones, const std::vector<int>& vBones, const Vec3& vOffset, ESPDrawEntity_t& tOut);

	ESPAnim_t& GetAnim(const void* pKey);
	void CleanupAnims();

	std::unordered_map<CBaseEntity*, PlayerCache_t> m_mPlayerCache = {};
	std::unordered_map<CBaseEntity*, BuildingCache_t> m_mBuildingCache = {};
	std::unordered_map<CBaseEntity*, EntityCache_t> m_mEntityCache = {};

	CIndicatorCache<ESPDrawCache_t> m_tDrawCache = {};
	std::unordered_map<const void*, ESPAnim_t> m_mAnims = {};
	float m_flAnimTime = 0.f;
	std::unordered_map<CBaseEntity*, ESPMotion_t> m_mMotion = {};

public:
	void Store(CTFPlayer* pLocal);
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw(ImDrawList* pDrawList);
};

ADD_FEATURE(CESP, ESP);
