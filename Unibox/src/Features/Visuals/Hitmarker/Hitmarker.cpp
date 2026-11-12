#include "Hitmarker.h"

#ifndef TEXTMODE
#include <ImGui/imgui.h>
#endif

static constexpr float HitmarkerDuration = 0.65f;
static constexpr float HitmarkerKillDuration = 0.85f;
static constexpr double HitmarkerKillMerge = 0.35;

static float HitmarkerLifetime(int iEvent)
{
	return iEvent == HitmarkerEventEnum::Kill ? HitmarkerKillDuration : HitmarkerDuration;
}

static Color_t HitmarkerColor(int iEvent)
{
	switch (iEvent)
	{
	case HitmarkerEventEnum::Critical: return Vars::Colors::HitmarkerCritical.Value;
	case HitmarkerEventEnum::Kill: return Vars::Colors::HitmarkerKill.Value;
	default: return Vars::Colors::Hitmarker.Value;
	}
}

void CHitmarker::Add(int iVictim, const Vec3& vPosition, int iDamage, int iEvent)
{
	Hit_t& tHit = m_aHits[m_uNext];
	tHit.m_vPosition = vPosition;
	tHit.m_dTime = SDK::PlatFloatTime();
	tHit.m_iVictim = iVictim;
	tHit.m_iDamage = iDamage;
	tHit.m_iEvent = iEvent;
	m_uNext = (m_uNext + 1) % Capacity;
}

void CHitmarker::Event(IGameEvent* pEvent, uint32_t uHash)
{
	if (!Vars::Visuals::Hitmarker::Enabled.Value)
		return;

	switch (uHash)
	{
	case FNV1A::Hash32Const("player_hurt"): PlayerHurt(pEvent); break;
	case FNV1A::Hash32Const("player_death"): PlayerDeath(pEvent); break;
	}
}

void CHitmarker::PlayerHurt(IGameEvent* pEvent)
{
	const int iLocal = I::EngineClient->GetLocalPlayer();
	const int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
	const int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
	const int iDamage = pEvent->GetInt("damageamount");
	if (iAttacker != iLocal || iVictim <= 0 || iVictim == iAttacker || iDamage <= 0)
		return;

	auto pVictim = I::ClientEntityList->GetClientEntity(iVictim)->As<CBaseEntity>();
	if (!pVictim)
		return;

	const bool bCritical = pEvent->GetBool("crit") || pEvent->GetBool("minicrit");
	Add(iVictim, pVictim->GetCenter(), iDamage, bCritical ? HitmarkerEventEnum::Critical : HitmarkerEventEnum::Normal);
}

void CHitmarker::PlayerDeath(IGameEvent* pEvent)
{
	if (pEvent->GetInt("death_flags") & TF_DEATH_FEIGN_DEATH)
		return;

	const int iLocal = I::EngineClient->GetLocalPlayer();
	const int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
	const int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
	if (iAttacker != iLocal || iVictim <= 0 || iVictim == iAttacker)
		return;

	const double dNow = SDK::PlatFloatTime();
	Hit_t* pRecent = nullptr;
	for (Hit_t& tHit : m_aHits)
	{
		const double dAge = dNow - tHit.m_dTime;
		if (tHit.m_iVictim == iVictim && dAge >= 0.0 && dAge <= HitmarkerKillMerge && (!pRecent || tHit.m_dTime > pRecent->m_dTime))
			pRecent = &tHit;
	}

	if (pRecent)
	{
		pRecent->m_iEvent = HitmarkerEventEnum::Kill;
		pRecent->m_dTime = dNow;
		return;
	}

	if (auto pVictim = I::ClientEntityList->GetClientEntity(iVictim)->As<CBaseEntity>())
		Add(iVictim, pVictim->GetCenter(), 0, HitmarkerEventEnum::Kill);
}

void CHitmarker::CacheDrawInfo(CTFPlayer* pLocal)
{
	DrawCache_t tCache = {};
	if (!pLocal || !Vars::Visuals::Hitmarker::Enabled.Value)
	{
		m_tDrawCache.Set(std::move(tCache));
		return;
	}

	const double dNow = SDK::PlatFloatTime();
	const bool bScreen = Vars::Visuals::Hitmarker::Mode.Value == Vars::Visuals::Hitmarker::ModeEnum::Crosshair;

	const Hit_t* pLatest = nullptr;
	if (bScreen)
	{
		for (const Hit_t& tHit : m_aHits)
		{
			const double dAge = dNow - tHit.m_dTime;
			if (dAge >= 0.0 && dAge < HitmarkerLifetime(tHit.m_iEvent) && (!pLatest || tHit.m_dTime > pLatest->m_dTime))
				pLatest = &tHit;
		}
	}

	for (const Hit_t& tHit : m_aHits)
	{
		if (bScreen && &tHit != pLatest)
			continue;

		const double dAge = dNow - tHit.m_dTime;
		const float flLifetime = HitmarkerLifetime(tHit.m_iEvent);
		if (dAge < 0.0 || dAge >= flLifetime)
			continue;

		DrawHit_t tDraw = {};
		if (bScreen)
		{
			tDraw.m_flX = H::Draw.m_nScreenW / 2.f;
			tDraw.m_flY = H::Draw.m_nScreenH / 2.f;
		}
		else
		{
			Vec3 vScreen;
			if (!SDK::W2S(tHit.m_vPosition, vScreen))
				continue;
			tDraw.m_flX = vScreen.x;
			tDraw.m_flY = vScreen.y;
		}

		tDraw.m_flProgress = float(dAge) / flLifetime;
		tDraw.m_iDamage = tHit.m_iDamage;
		tDraw.m_iEvent = tHit.m_iEvent;
		tCache.m_vHits.push_back(tDraw);
	}

	m_tDrawCache.Set(std::move(tCache));
}

#ifndef TEXTMODE
void CHitmarker::Draw(ImDrawList* pDrawList)
{
	if (!pDrawList || !Vars::Visuals::Hitmarker::Enabled.Value)
		return;

	const auto pCache = m_tDrawCache.Get();
	const float flThickness = std::max(H::Draw.IndicatorScale(2.f), 1.f);
	for (const DrawHit_t& tHit : pCache->m_vHits)
	{
		const bool bKill = tHit.m_iEvent == HitmarkerEventEnum::Kill;
		const float flInner = H::Draw.IndicatorScale(3.f + tHit.m_flProgress * 4.f);
		const float flOuter = H::Draw.IndicatorScale(9.f + tHit.m_flProgress * (bKill ? 10.f : 7.f));

		Color_t tColor = HitmarkerColor(tHit.m_iEvent);
		tColor.a = byte(float(tColor.a) * (1.f - tHit.m_flProgress));
		const ImU32 uColor = IM_COL32(tColor.r, tColor.g, tColor.b, tColor.a);

		for (const float flX : { -1.f, 1.f })
		{
			for (const float flY : { -1.f, 1.f })
			{
				const float flDX = flX * 0.7071f, flDY = flY * 0.7071f;
				pDrawList->AddLine({ tHit.m_flX + flDX * flInner, tHit.m_flY + flDY * flInner }, { tHit.m_flX + flDX * flOuter, tHit.m_flY + flDY * flOuter }, uColor, flThickness);
			}
		}

		if (Vars::Visuals::Hitmarker::ShowDamage.Value && tHit.m_iDamage > 0)
		{
			const std::string sDamage = std::format("-{}", tHit.m_iDamage);
			const ImVec2 vAt = { tHit.m_flX + H::Draw.IndicatorScale(13.f), tHit.m_flY - H::Draw.IndicatorScale(19.f + tHit.m_flProgress * 23.f) };
			pDrawList->AddText({ vAt.x + 1.f, vAt.y + 1.f }, IM_COL32(0, 0, 0, tColor.a), sDamage.c_str());
			pDrawList->AddText(vAt, uColor, sDamage.c_str());
		}
	}
}
#endif

void CHitmarker::Clear()
{
	for (Hit_t& tHit : m_aHits)
		tHit = {};
	m_uNext = 0;
	m_tDrawCache.Set(DrawCache_t{});
}
