#include "SpectatorList.h"

#include "../../ImGui/IndicatorPanel.h"
#include "../../Players/PlayerUtils.h"
#include "../../Spectate/Spectate.h"

static float s_flCurrentHeight = 0.0f;

bool CSpectatorList::GetSpectators(CTFPlayer* pTarget)
{
	m_vSpectators.clear();

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return false;

	int iTarget = pTarget->entindex();
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		auto pPlayer = I::ClientEntityList->GetClientEntity(n)->As<CTFPlayer>();
		bool bLocal = n == I::EngineClient->GetLocalPlayer();

		if (pResource->m_bValid(n) && !pResource->IsFakePlayer(n)
			&& pResource->m_iTeam(I::EngineClient->GetLocalPlayer()) != TEAM_SPECTATOR && pResource->m_iTeam(n) == TEAM_SPECTATOR)
		{
			m_vSpectators.emplace_back(F::PlayerUtils.GetPlayerName(n, pResource->GetName(n)), "possible", -1.f, false, n);
			continue;
		}

		if (pTarget->entindex() == n || pResource->IsFakePlayer(n)
			|| !pPlayer || !pPlayer->IsPlayer() || pPlayer->IsAlive()
			|| pTarget->IsDormant() != pPlayer->IsDormant()
			|| pResource->m_iTeam(iTarget) != pResource->m_iTeam(n))
		{
			if (m_mRespawnCache.contains(n))
				m_mRespawnCache.erase(n);
			continue;
		}

		int iObserverTarget = !pPlayer->IsDormant() ? pPlayer->m_hObserverTarget().GetEntryIndex() : iTarget;
		int iObserverMode = pPlayer->m_iObserverMode();
		if (bLocal && F::Spectate.HasTarget())
		{
			iObserverTarget = F::Spectate.m_hOriginalTarget.GetEntryIndex();
			iObserverMode = F::Spectate.m_iOriginalMode;
		}
		if (iObserverTarget != iTarget || bLocal && !I::EngineClient->IsPlayingDemo() && !F::Spectate.HasTarget())
		{
			if (m_mRespawnCache.contains(n))
				m_mRespawnCache.erase(n);
			continue;
		}

		const char* sMode = "possible";
		if (!pPlayer->IsDormant())
		{
			switch (iObserverMode)
			{
			case OBS_MODE_FIRSTPERSON: sMode = "1st"; break;
			case OBS_MODE_THIRDPERSON: sMode = "3rd"; break;
			default: continue;
			}
		}

		float flRespawnTime = 0.f, flRespawnIn = -1.f;
		bool bRespawnTimeIncreased = false;
		if (pPlayer->IsInValidTeam())
		{
			flRespawnTime = pResource->m_flNextRespawnTime(n);
			flRespawnIn = std::max(floorf(flRespawnTime - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick)), 0.f);
			if (!m_mRespawnCache.contains(n))
				m_mRespawnCache[n] = flRespawnTime;
			else if (m_mRespawnCache[n] + 0.5f < flRespawnTime)
				bRespawnTimeIncreased = true;
		}

		m_vSpectators.emplace_back(F::PlayerUtils.GetPlayerName(n, pResource->GetName(n)), sMode, flRespawnIn, bRespawnTimeIncreased, n);
	}

	return !m_vSpectators.empty();
}

void CSpectatorList::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Spectators))
	{
		m_mRespawnCache.clear();
		s_flCurrentHeight = 0.0f;
		return;
	}

	if (pLocal)
	{
		auto pTarget = pLocal;
		switch (pLocal->m_iObserverMode())
		{
		case OBS_MODE_FIRSTPERSON:
		case OBS_MODE_THIRDPERSON:
			pTarget = pLocal->m_hObserverTarget()->As<CTFPlayer>();
		}
		if (!pTarget || !pTarget->IsPlayer()
			|| !GetSpectators(pTarget))
			return;
	}

	if (m_vSpectators.empty())
		return;

	int x = Vars::Menu::SpectatorsDisplay.Value.x;
	int y = Vars::Menu::SpectatorsDisplay.Value.y;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(3);
	ImDrawList* pDrawList = ImGui::GetForegroundDrawList();

	float flMaxTextWidth = 0.f;
	for (auto& Spectator : m_vSpectators)
	{
		const std::string sText = std::format("{} ({} - respawn {}s)", Spectator.m_sName, Spectator.m_sMode, static_cast<int>(Spectator.m_flRespawnIn));
		flMaxTextWidth = std::max(flMaxTextWidth, ImGui::CalcTextSize(sText.c_str()).x);
	}

	int totalHeight = H::Draw.Scale(48);
	totalHeight += static_cast<int>(m_vSpectators.size()) * nTall;
	totalHeight += H::Draw.Scale(4); 

	s_flCurrentHeight = std::lerp(s_flCurrentHeight, static_cast<float>(totalHeight), I::GlobalVars->frametime * 10.0f);
	totalHeight = static_cast<int>(std::round(s_flCurrentHeight));

	const int boxWidth = std::max(H::Draw.Scale(220), static_cast<int>(flMaxTextWidth) + H::Draw.Scale(40)); 
	const int cornerRadius = H::Draw.Scale(2); 
	
	Color_t tBackgroundColor = Vars::Menu::Theme::Background.Value;
	tBackgroundColor = tBackgroundColor.Lerp({ 127, 127, 127, tBackgroundColor.a }, 1.f / 9);
	tBackgroundColor.a = 255;
	
	Color_t tAccentColor = Vars::Menu::Theme::Accent.Value;
	Color_t tActiveColor = Vars::Menu::Theme::Active.Value;

	const float flX = static_cast<float>(x);
	float flY = static_cast<float>(y);
	pDrawList->AddRectFilled({ flX, flY }, { flX + boxWidth, flY + totalHeight }, ColorToU32(tBackgroundColor), static_cast<float>(cornerRadius));

	const float headerHeight = H::Draw.Scale(24);
	Color_t tHeaderBgColor = tBackgroundColor;
	tHeaderBgColor = { 
		static_cast<byte>(tBackgroundColor.r * 0.9f), 
		static_cast<byte>(tBackgroundColor.g * 0.9f), 
		static_cast<byte>(tBackgroundColor.b * 0.9f), 
		tBackgroundColor.a 
	};
	
	pDrawList->AddRectFilled({ flX, flY }, { flX + boxWidth, flY + headerHeight }, ColorToU32(tHeaderBgColor), static_cast<float>(cornerRadius));
	DrawIndicatorText(pDrawList, flX + H::Draw.Scale(16), flY + H::Draw.Scale(5), tActiveColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT, "Spec");
	const float flSpecWidth = ImGui::CalcTextSize("Spec").x;
	DrawIndicatorText(pDrawList, flX + H::Draw.Scale(16) + flSpecWidth, flY + H::Draw.Scale(5), tAccentColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT, "tators");

	flY += H::Draw.Scale(32);
	for (auto& Spectator : m_vSpectators)
	{
		Color_t tColor = tActiveColor;
		if (Spectator.m_bIsFriend)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)].m_tColor;
		else if (Spectator.m_bInParty)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)].m_tColor;
		else if (Spectator.m_bRespawnTimeIncreased)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(CHEATER_TAG)].m_tColor;
		else if (FNV1A::Hash32(Spectator.m_sMode) == FNV1A::Hash32Const("1st"))
			tColor = tColor.Lerp({ 255, 150, 0, 255 }, 0.5f);

		if (Spectator.m_bRespawnTimeIncreased || FNV1A::Hash32(Spectator.m_sMode) == FNV1A::Hash32Const("1st"))
		{
			Color_t tHighlightColor = tBackgroundColor;
			tHighlightColor = tHighlightColor.Lerp({ 255, 255, 255, tBackgroundColor.a }, 0.05f);
			pDrawList->AddRectFilled(
				{ flX + H::Draw.Scale(12), flY - H::Draw.Scale(2) },
				{ flX + boxWidth - H::Draw.Scale(12), flY - H::Draw.Scale(2) + nTall },
				ColorToU32(tHighlightColor),
				H::Draw.Scale(2.f));
		}

		DrawIndicatorText(pDrawList, flX + H::Draw.Scale(16), flY, tColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT, std::format("{} ({} - respawn {}s)", Spectator.m_sName.c_str(), Spectator.m_sMode, static_cast<int>(Spectator.m_flRespawnIn)));
		flY += nTall;
	}
}
