#pragma once
#include "../../../SDK/SDK.h"
#include "../../ImGui/IndicatorCache.h"

struct ImDrawList;

enum class ERadarEntityType
{
	Player,
	Building,
	Health,
	Ammo,
	Objective,
	Projectile,
	NPC,
	Other
};

struct RadarEntity_t
{
	ERadarEntityType m_eType = ERadarEntityType::Other;
	Vec3 m_vOrigin = {};
	float m_flYaw = 0.f;
	Color_t m_tColor = {};
	int m_iClass = 0;
	int m_iHealth = 0;
	int m_iMaxHealth = 0;
	bool m_bDormant = false;
	bool m_bCloaked = false;
};

struct RadarCache_t
{
	bool m_bValid = false;
	Vec3 m_vCenterOrigin = {};
	float m_flLocalFOV = 90.f;
	std::vector<RadarEntity_t> m_vEntities = {};
};

class CRadar
{
private:
	bool GetDrawPosition(const Vec3& vCenterOrigin, const Vec3& vEntityOrigin, float flViewYaw, float flPadding, float& x, float& y, float& z);
	void DrawBackground(ImDrawList* pDrawList, float flLocalFOV);
	void DrawPoints(ImDrawList* pDrawList, const RadarCache_t& tCache, float flViewYaw);

	CIndicatorCache<RadarCache_t> m_tCache = {};

public:
	void Store(CTFPlayer* pLocal);
	void Draw(ImDrawList* pDrawList, CTFPlayer* pLocal = nullptr);
};

ADD_FEATURE(CRadar, Radar);
