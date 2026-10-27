#include "SpectatorList.h"

#include "../../ImGui/Menu/Components.h"
#include "../../Players/PlayerUtils.h"
#include "../../Spectate/Spectate.h"
#include "../../ImGui/IndicatorPanel.h"

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

	static IndicatorAnim_t tAnim;
	static std::vector<Spectator_t> s_vLastValidSpectators;

	const std::vector<Spectator_t> vSpectators = m_tDrawCache.Get();
	if (!vSpectators.empty())
		s_vLastValidSpectators = vSpectators;

	const bool bEnabled = (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Spectators) != 0;
	const bool bVisible = bEnabled && (!vSpectators.empty() || F::Menu.m_bIsOpen);

	bool bOpening = true;
	const float flAnim = tAnim.Update(bVisible, bOpening);
	if (!tAnim.ShouldDraw(bVisible))
		return;

	const auto& vActiveSpectators = !vSpectators.empty() ? vSpectators : s_vLastValidSpectators;

	ImFont* pSmall = F::Render.FontSmall, *pRegular = F::Render.FontRegular;
	auto fTextWidth = [](ImFont* pFont, const char* sText) { return pFont->CalcTextSizeA(pFont->LegacySize, FLT_MAX, 0.f, sText).x; };
	std::vector<std::string> vRespawns = {};
	float flNameWidth = 0.f, flRespawnWidth = 0.f, flChipWidth = 0.f;
	for (auto& tSpectator : vActiveSpectators)
	{
		vRespawns.push_back(tSpectator.m_flRespawnIn >= 0.f ? std::format("{}s", static_cast<int>(tSpectator.m_flRespawnIn)) : "");
		flNameWidth = std::max(flNameWidth, fTextWidth(pRegular, tSpectator.m_sName.c_str()));
		flRespawnWidth = std::max(flRespawnWidth, fTextWidth(pSmall, vRespawns.back().c_str()));
		flChipWidth = std::max(flChipWidth, OverlayChipWidth(tSpectator.m_sMode));
	}

	const float flPad = H::Draw.Scale(12), flHeader = H::Draw.Scale(32), flRow = H::Draw.Scale(26), flTop = flHeader + H::Draw.Scale(6);
	const float flWidth = std::max(flPad * 2 + flNameWidth + H::Draw.Scale(18) + (flRespawnWidth ? flRespawnWidth + H::Draw.Scale(10) : 0.f) + flChipWidth, H::Draw.Scale(170));
	const float flHeight = flTop + flRow * vActiveSpectators.size() + H::Draw.Scale(vActiveSpectators.empty() ? 0 : 6);
	SetNextWindowSize({ flWidth, flHeight }, ImGuiCond_Always);

	static DragBox_t tOld = { -2147483648, -2147483648 };
	DragBox_t tDragBox = F::Menu.m_bIsOpen ? FGet(Vars::Menu::SpectatorsDisplay, true) : Vars::Menu::SpectatorsDisplay.Value;
	if (tDragBox != tOld)
		SetNextWindowPos({ float(tDragBox.x), float(tDragBox.y) }, ImGuiCond_Always);

	PushStyleVar(ImGuiStyleVar_WindowMinSize, { H::Draw.Scale(40), H::Draw.Scale(32) });
	ImGuiWindowFlags iWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings;
	if (flAnim < 1.0f)
		iWindowFlags |= ImGuiWindowFlags_NoInputs;

	if (Begin("Spectators", nullptr, iWindowFlags))
	{
		const ImVec2 vWindowPos = GetWindowPos();

		RenderOverlayWindow(flHeader, ICON_MD_VISIBILITY, "Spectators");

		if (flAnim >= 1.0f)
		{
			tDragBox.x = vWindowPos.x; tDragBox.y = vWindowPos.y; tOld = tDragBox;
			if (F::Menu.m_bIsOpen)
				FSet(Vars::Menu::SpectatorsDisplay, tDragBox);
		}

		ImDrawList* pDrawList = GetWindowDrawList();
		int i = 0;
		for (auto& tSpectator : vActiveSpectators)
		{
			ImVec4 tName = F::Render.Active.Value;
			if (tSpectator.m_bIsFriend)
				tName = ColorByteToVec(F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)].m_tColor);
			else if (tSpectator.m_bInParty)
				tName = ColorByteToVec(F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)].m_tColor);
			else if (tSpectator.m_bRespawnTimeIncreased)
				tName = ColorByteToVec(F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(CHEATER_TAG)].m_tColor);
			const bool bFirstPerson = FNV1A::Hash32(tSpectator.m_sMode) == FNV1A::Hash32Const("1st");

			const float flCenterY = vWindowPos.y + flTop + flRow * i + flRow / 2;
			pDrawList->AddText(pRegular, pRegular->LegacySize, { vWindowPos.x + flPad, floorf(flCenterY - pRegular->LegacySize / 2) }, FadeColor(tName), tSpectator.m_sName.c_str());
			const float flChipX = DrawOverlayChip(pDrawList, vWindowPos.x + flWidth - flPad, flCenterY, tSpectator.m_sMode, bFirstPerson, F::Render.Danger.Value, 1.f, flChipWidth);
			if (!vRespawns[i].empty())
			{
				const float flRespawn = fTextWidth(pSmall, vRespawns[i].c_str());
				pDrawList->AddText(pSmall, pSmall->LegacySize, { flChipX - H::Draw.Scale(10) - flRespawn, floorf(flCenterY - pSmall->LegacySize / 2) }, FadeColor(F::Render.Inactive), vRespawns[i].c_str());
			}

			i++;
		}

		End();

		if (flAnim < 1.0f)
		{
			if (ImGuiWindow* pWindow = FindWindowByName("Spectators"))
				TransformWindowDrawList(pWindow, flAnim, bOpening, GetIO().DisplaySize);
		}
	}
	PopStyleVar();
}
