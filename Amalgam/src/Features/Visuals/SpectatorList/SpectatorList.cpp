#include "SpectatorList.h"

#include "../../ImGui/Menu/Components.h"
#include "../../Players/PlayerUtils.h"
#include "../../Spectate/Spectate.h"

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

void CSpectatorList::CacheDrawInfo(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Spectators) || !pLocal)
	{
		m_mRespawnCache.clear();
		m_tDrawCache.Set({});
		return;
	}

	auto pTarget = pLocal;
	switch (pLocal->m_iObserverMode())
	{
	case OBS_MODE_FIRSTPERSON:
	case OBS_MODE_THIRDPERSON:
		pTarget = pLocal->m_hObserverTarget()->As<CTFPlayer>();
	}
	if (!pTarget || !pTarget->IsPlayer() || !GetSpectators(pTarget))
	{
		m_tDrawCache.Set({});
		return;
	}

	m_tDrawCache.Set(m_vSpectators);
}

void CSpectatorList::Draw()
{
	using namespace ImGui;

	const std::vector<Spectator_t> vSpectators = m_tDrawCache.Get();
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Spectators) || (vSpectators.empty() && !F::Menu.m_bIsOpen))
		return;

	std::vector<std::string> vRespawns = {};
	float flNameWidth = 0.f, flModeWidth = 0.f, flRespawnWidth = 0.f;
	PushFont(F::Render.FontSmall);
	for (auto& Spectator : vSpectators)
	{
		vRespawns.push_back(Spectator.m_flRespawnIn >= 0.f ? std::format("respawn {}s", static_cast<int>(Spectator.m_flRespawnIn)) : "");
		flNameWidth = std::max(flNameWidth, FCalcTextSize(Spectator.m_sName.c_str()).x);
		flModeWidth = std::max(flModeWidth, FCalcTextSize(Spectator.m_sMode).x);
		flRespawnWidth = std::max(flRespawnWidth, FCalcTextSize(vRespawns.back().c_str()).x);
	}
	PopFont();
	flNameWidth += H::Draw.Scale(9), flModeWidth += H::Draw.Scale(9), flRespawnWidth += H::Draw.Scale(9);

	PushFont(F::Render.FontLarge);
	const float flTitleWidth = CalcTextSize("Spec").x + CalcTextSize("tators").x;
	PopFont();

	const float flWidth = std::max(flNameWidth + flModeWidth + flRespawnWidth + H::Draw.Scale(14), flTitleWidth + H::Draw.Scale(16));
	const float flHeight = H::Draw.Scale(18 * vSpectators.size() + 42);
	SetNextWindowSize({ flWidth, flHeight }, ImGuiCond_Always);

	static DragBox_t tOld = { -2147483648, -2147483648 };
	DragBox_t tDragBox = F::Menu.m_bIsOpen ? FGet(Vars::Menu::SpectatorsDisplay, true) : Vars::Menu::SpectatorsDisplay.Value;
	if (tDragBox != tOld)
		SetNextWindowPos({ float(tDragBox.x), float(tDragBox.y) }, ImGuiCond_Always);

	PushStyleVar(ImGuiStyleVar_WindowMinSize, { H::Draw.Scale(40), H::Draw.Scale(40) });
	if (Begin("Spectators", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
	{
		ImVec2 vWindowPos = GetWindowPos();

		RenderTwoToneBackground(H::Draw.Scale(28), F::Render.Background0, F::Render.Background0p5, F::Render.Background2);

		tDragBox.x = vWindowPos.x; tDragBox.y = vWindowPos.y; tOld = tDragBox;
		if (F::Menu.m_bIsOpen)
			FSet(Vars::Menu::SpectatorsDisplay, tDragBox);

		PushFont(F::Render.FontLarge);
		SetCursorPos({ H::Draw.Scale(8), H::Draw.Scale(7) });
		PushStyleColor(ImGuiCol_Text, F::Render.Active.Value);
		FText("Spec");
		SetCursorPos({ H::Draw.Scale(8) + CalcTextSize("Spec").x, H::Draw.Scale(7) });
		PushStyleColor(ImGuiCol_Text, F::Render.Accent.Value);
		FText("tators");
		PopStyleColor(2);
		PopFont();

		PushFont(F::Render.FontSmall);
		int i = 0;
		for (auto& Spectator : vSpectators)
		{
			Color_t tNameColor = Vars::Menu::Theme::Accent.Value;
			if (Spectator.m_bIsFriend)
				tNameColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)].m_tColor;
			else if (Spectator.m_bInParty)
				tNameColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)].m_tColor;
			else if (Spectator.m_bRespawnTimeIncreased)
				tNameColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(CHEATER_TAG)].m_tColor;
			else if (FNV1A::Hash32(Spectator.m_sMode) == FNV1A::Hash32Const("1st"))
				tNameColor = tNameColor.Lerp({ 255, 150, 0, 255 }, 0.5f);

			float flPosX = 0;
			const float flRowY = H::Draw.Scale(36 + 18 * i);

			SetCursorPos({ flPosX += H::Draw.Scale(12), flRowY });
			PushStyleColor(ImGuiCol_Text, ColorByteToVec(tNameColor));
			FText(Spectator.m_sName.c_str());
			PopStyleColor();

			PushStyleColor(ImGuiCol_Text, F::Render.Active.Value);
			SetCursorPos({ flPosX += flNameWidth, flRowY });
			FText(Spectator.m_sMode);
			SetCursorPos({ flPosX += flModeWidth, flRowY });
			FText(vRespawns[i].c_str());
			PopStyleColor();

			i++;
		}
		PopFont();

		End();
	}
	PopStyleVar();
}
