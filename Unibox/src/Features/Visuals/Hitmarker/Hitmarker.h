#pragma once
#include "../../../SDK/SDK.h"
#include "../../ImGui/IndicatorCache.h"

#ifndef TEXTMODE
struct ImDrawList;
#endif

Enum(HitmarkerEvent, Normal, Critical, Kill, Count)

class CHitmarker
{
private:
	struct Hit_t
	{
		Vec3 m_vPosition = {};
		double m_dTime = -1.0;
		int m_iVictim = 0;
		int m_iDamage = 0;
		int m_iEvent = HitmarkerEventEnum::Normal;
	};

	struct DrawHit_t
	{
		float m_flX = 0.f;
		float m_flY = 0.f;
		float m_flProgress = 0.f;
		int m_iDamage = 0;
		int m_iEvent = HitmarkerEventEnum::Normal;
	};

	struct DrawCache_t
	{
		std::vector<DrawHit_t> m_vHits = {};
	};

	static constexpr size_t Capacity = 16;

	void Add(int iVictim, const Vec3& vPosition, int iDamage, int iEvent);
	void PlayerHurt(IGameEvent* pEvent);
	void PlayerDeath(IGameEvent* pEvent);

	Hit_t m_aHits[Capacity] = {};
	size_t m_uNext = 0;
	CIndicatorCache<DrawCache_t> m_tDrawCache = {};

public:
	void Event(IGameEvent* pEvent, uint32_t uHash);
	void CacheDrawInfo(CTFPlayer* pLocal);
#ifndef TEXTMODE
	void Draw(ImDrawList* pDrawList);
#endif
	void Clear();
};

ADD_FEATURE(CHitmarker, Hitmarker);
