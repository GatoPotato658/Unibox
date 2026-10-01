#include "ESP.h"

#include "../Groups/Groups.h"
#include "../../Players/PlayerUtils.h"
#include "../../Spectate/Spectate.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../ImGui/IndicatorPanel.h"
#include "../../ImGui/Render.h"
#include "../../ImGui/RenderSync.h"
#ifndef TEXTMODE
#include <wrl/client.h>
#endif

static inline bool GetDistanceThing(Vector vTargetPos, Vector vLocalPos, Group_t* pGroup, float& flOut)
{
	// distance things
	const Vec3 vDelta = vTargetPos - vLocalPos;
	const float flDistance = vDelta.Length();
	if (flDistance < pGroup->m_tESP.Start || flDistance > pGroup->m_tESP.End) 
		return false;

	flOut = pGroup->m_tColor.a;
	if (pGroup->m_tESP.SmoothAlpha)
	{
		flOut = Math::RemapVal(flDistance, pGroup->m_tESP.End - 256.f, pGroup->m_tESP.End, flOut, 0.f);
		if (pGroup->m_tESP.Start)
			flOut = Math::RemapVal(flDistance, pGroup->m_tESP.Start + 256.f, pGroup->m_tESP.Start, flOut, 0.f);
	}
	flOut /= 255.f;
	return true;
}

static inline void StorePlayer(CTFPlayer* pPlayer, CTFPlayer* pLocal, Group_t* pGroup, std::unordered_map<CBaseEntity*, PlayerCache_t>& mCache)
{
	int iIndex = pPlayer->entindex();

	if (int iObserverMode = pLocal->m_iObserverMode(); iObserverMode == OBS_MODE_FIRSTPERSON || iObserverMode == OBS_MODE_THIRDPERSON
		? iObserverMode == OBS_MODE_FIRSTPERSON && pLocal->m_hObserverTarget().GetEntryIndex() == iIndex
		: !I::Input->CAM_IsThirdPerson() && iIndex == I::EngineClient->GetLocalPlayer())
		return;

	auto pWeapon = pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>();
	auto pResource = H::Entities.GetResource();
	bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();
	int iClassNum = pPlayer->m_iClass();

	float flAlpha;
	if (!GetDistanceThing(pPlayer->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	PlayerCache_t& tCache = mCache[pPlayer];
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pPlayer, pGroup).Alpha(255);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;
	tCache.m_iBoxStyle = pGroup->m_tESP.BoxStyle;
	tCache.m_bBones = pGroup->m_tESP.Draw & ESPEnum::Bones;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance && !bLocal)
	{
		Vec3 vDelta = pPlayer->m_vecOrigin() - pLocal->m_vecOrigin();
		tCache.m_vText.emplace_back(ALIGN_BOTTOM, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pResource)
	{
		if (pGroup->m_tESP.Draw & ESPEnum::Name)
			tCache.m_vText.emplace_back(ALIGN_TOP, F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);

		if (pGroup->m_tESP.Draw & (ESPEnum::Labels | ESPEnum::Priority) && !pResource->IsFakePlayer(iIndex))
		{
			uint32_t uAccountID = pResource->m_iAccountID(iIndex);

			if (pGroup->m_tESP.Draw & ESPEnum::Priority)
			{
				if (auto pTag = F::PlayerUtils.GetSignificantTag(uAccountID, 1))
					tCache.m_vText.emplace_back(ALIGN_TOP, pTag->m_sName, pTag->m_tColor, pTag->m_tColor.IsColorDark() ? Color_t(255, 255, 255) : Color_t(0, 0, 0));
			}

			if (pGroup->m_tESP.Draw & ESPEnum::Labels)
			{
				static thread_local std::vector<std::tuple<std::string, Color_t, int>> vTags;
				vTags.clear();
				for (auto& iID : F::PlayerUtils.GetPlayerTags(uAccountID))
				{
					auto pTag = F::PlayerUtils.GetTag(iID);
					if (pTag && pTag->m_bLabel)
						vTags.emplace_back(pTag->m_sName, pTag->m_tColor, pTag->m_iPriority);
				}
				if (H::Entities.IsFriend(uAccountID))
				{
					auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)];
					if (pTag->m_bLabel)
						vTags.emplace_back(pTag->m_sName, pTag->m_tColor, pTag->m_iPriority);
				}
				if (auto iParty = H::Entities.GetParty(uAccountID))
				{
					auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)];
					if (int iPartyCount = H::Entities.GetPartyCount() + 1; pTag->m_bLabel)
					{
						if (!--iParty)
							vTags.emplace_back(pTag->m_sName, pTag->m_tColor, pTag->m_iPriority);
						else
							vTags.emplace_back(std::format("{}: {}", pTag->m_sName, iParty), pTag->m_tColor.HueShift(iParty * 360.f / iPartyCount), pTag->m_iPriority);
					}
				}
				if (H::Entities.IsF2P(uAccountID))
				{
					auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(F2P_TAG)];
					if (pTag->m_bLabel)
						vTags.emplace_back(pTag->m_sName, pTag->m_tColor, pTag->m_iPriority);
				}

				if (!vTags.empty())
				{
					std::sort(vTags.begin(), vTags.end(), [&](const auto a, const auto b) -> bool
					{
						// sort by priority if unequal
						if (std::get<2>(a) != std::get<2>(b))
							return std::get<2>(a) > std::get<2>(b);

						return std::get<0>(a) < std::get<0>(b);
					});

					for (auto& [sName, tColor, _] : vTags)
						tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, sName, tColor, tColor.IsColorDark() ? Color_t(255, 255, 255) : Color_t(0, 0, 0));
				}
			}
		}
	}

	float flHealth = pPlayer->m_iHealth(), flMaxHealth = pPlayer->GetMaxHealth();
	if (pGroup->m_tESP.Draw & ESPEnum::HealthBar)
	{
		tCache.m_flHealth = flHealth > flMaxHealth
			? 1.f + std::clamp((flHealth - flMaxHealth) / (floorf(flMaxHealth / 10.f) * 5), 0.f, 1.f)
			: std::clamp(flHealth / flMaxHealth, 0.f, 1.f);
			
		Color_t tColor = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorGood.Value, std::clamp(tCache.m_flHealth, 0.f, 1.f), LerpEnum::HSV);
		Bar_t& tBar = tCache.m_vBars.emplace_back();
		tBar.m_iMode = ALIGN_LEFT;
		tBar.m_flPercent = tCache.m_flHealth;
		tBar.m_tColor = tColor;
		tBar.m_tOverfill = Vars::Colors::IndicatorMisc.Value;
		tBar.m_tBackground = Color_t(0, 0, 0, 120);
		tBar.m_bSmooth = true;
	}
	if (pGroup->m_tESP.Draw & ESPEnum::HealthText)
		tCache.m_vText.emplace_back(ALIGN_LEFT, std::format("{}", flHealth), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

	if (pGroup->m_tESP.Draw & (ESPEnum::UberBar | ESPEnum::UberText) && iClassNum == TF_CLASS_MEDIC)
	{
		auto pMediGun = pPlayer->GetWeaponFromSlot(SLOT_SECONDARY);
		if (pMediGun && pMediGun->GetClassID() == ETFClassID::CWeaponMedigun)
		{
			float flUber = std::clamp(pMediGun->As<CWeaponMedigun>()->m_flChargeLevel(), 0.f, 1.f);
			if (pGroup->m_tESP.Draw & ESPEnum::UberBar)
			{
				Bar_t& bar = tCache.m_vBars.emplace_back();
				bar.m_iMode = ALIGN_BOTTOM;
				bar.m_flPercent = flUber;
				bar.m_tColor = Vars::Colors::IndicatorMisc.Value;
				bar.m_tBackground = Color_t(0, 0, 0, 120);
				bar.m_bSmooth = false;
			}
			if (pGroup->m_tESP.Draw & ESPEnum::UberText)
				tCache.m_vText.emplace_back(ALIGN_BOTTOMRIGHT, std::format("{:.0f}%", flUber * 100), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		}
	}

	if (pGroup->m_tESP.Draw & ESPEnum::ClassIcon)
		tCache.m_iClassIcon = iClassNum;
	if (pGroup->m_tESP.Draw & ESPEnum::ClassText)
		tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, SDK::GetClassByIndex(iClassNum, false), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

	if (pGroup->m_tESP.Draw & ESPEnum::WeaponIcon && pWeapon)
		tCache.m_pWeaponIcon = pWeapon->GetWeaponIcon();
	if (pGroup->m_tESP.Draw & ESPEnum::WeaponText && pWeapon)
		tCache.m_vText.emplace_back(ALIGN_BOTTOM, SDK::ConvertWideToUTF8(pWeapon->GetWeaponName()), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

	if (pGroup->m_tESP.Draw & ESPEnum::LagCompensation && !pPlayer->IsDormant() && !bLocal)
	{
		if (H::Entities.GetLagCompensation(iIndex))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Lagcomp", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Ping && pResource && !bLocal)
	{
		int iPing = pResource->m_iPing(iIndex);
		if (iPing && (iPing >= 200 || iPing <= 5))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("{}MS", iPing), Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::KDR && pResource && !bLocal)
	{
		int iKills = pResource->m_iScore(iIndex), iDeaths = pResource->m_iDeaths(iIndex);
		if (iKills >= 20)
		{
			int iKDR = iKills / std::max(iDeaths, 1);
			if (iKDR >= 10)
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("High KD [{} / {}]", iKills, iDeaths), Vars::Colors::IndicatorTextMid.Value, Vars::Menu::Theme::Background.Value);
		}
	}

	// Add the Mafia Works feature implementation
	if (pGroup->m_tESP.Draw & ESPEnum::ThatsHowMafiaWorks && pResource && !bLocal)
	{
		int iKills = pResource->m_iScore(iIndex);
		int iDeaths = pResource->m_iDeaths(iIndex);
		int iDamage = pResource->m_iDamage(iIndex);

		// Calculate player level based on stats
		int iLevel = 1;
		if (iKills >= 30 && iDamage >= 10000) iLevel = 6;
		else if (iKills >= 20 && iDamage >= 5000) iLevel = 5;
		else if (iKills >= 15 && iDamage >= 3000) iLevel = 4;
		else if (iKills >= 10 && iDamage >= 2000) iLevel = 3;
		else if (iKills >= 5 && iDamage >= 500) iLevel = 2;

		// Define title based on level
		std::string sTitle;
		Color_t tTitleColor;
		switch (iLevel)
		{
		case 1:
			sTitle = "Lv.1 Crook";
			tTitleColor = Color_t(150, 150, 150, 255); // Grey
			break;
		case 2:
			sTitle = "Lv.10 Gangster";
			tTitleColor = Color_t(76, 175, 80, 255); // Green
			break;
		case 3:
			sTitle = "Lv.35 Hitman";
			tTitleColor = Color_t(33, 150, 243, 255); // Blue
			break;
		case 4:
			sTitle = "Lv.50 Boss";
			tTitleColor = Color_t(156, 39, 176, 255); // Purple
			break;
		case 5:
			sTitle = "Lv.80 Godfather";
			tTitleColor = Color_t(211, 47, 47, 255); // Red
			break;
		case 6:
			sTitle = "Lv.100 BOSS OF ALL BOSSES";
			tTitleColor = Color_t(255, 193, 7, 255); // Gold
			break;
		}

		tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, sTitle, tTitleColor, Color_t(0, 0, 0, 200));
	}

	// Buffs
	if (pGroup->m_tESP.Draw & ESPEnum::Buffs)
	{
		if (pPlayer->InCond(TF_COND_INVULNERABLE) ||
			pPlayer->InCond(TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED) ||
			pPlayer->InCond(TF_COND_INVULNERABLE_USER_BUFF) ||
			pPlayer->InCond(TF_COND_INVULNERABLE_CARD_EFFECT))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Uber", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_MEGAHEAL))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Megaheal", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_PHASE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bonk", Vars::Colors::IndicatorTextMid.Value, Vars::Menu::Theme::Background.Value);

		bool bCrits = pPlayer->IsCritBoosted(), bMiniCrits = pPlayer->IsMiniCritBoosted();
		if (pWeapon)
		{
			if (bMiniCrits && SDK::AttribHookValue(0, "minicrits_become_crits", pWeapon)
				|| SDK::AttribHookValue(0, "crit_while_airborne", pWeapon) && pPlayer->InCond(TF_COND_BLASTJUMPING))
				bCrits = true, bMiniCrits = false;
			if (bCrits && SDK::AttribHookValue(0, "crits_become_minicrits", pWeapon))
				bCrits = false, bMiniCrits = true;
		}
		if (bCrits)
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crits", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (bMiniCrits)
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Mini-crits", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);

		/* vaccinator effects */
		if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) || pPlayer->InCond(TF_COND_BULLET_IMMUNE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bullet+", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bullet", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) || pPlayer->InCond(TF_COND_BLAST_IMMUNE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Blast+", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Blast", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) || pPlayer->InCond(TF_COND_FIRE_IMMUNE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Fire+", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Fire", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_OFFENSEBUFF))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Banner", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_DEFENSEBUFF))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Battalions", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_REGENONDAMAGEBUFF))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Conch", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_RUNE_STRENGTH))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Strength", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_HASTE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Haste", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_REGEN))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Regen", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_RESIST))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Resistance", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_VAMPIRE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Vampire", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_REFLECT))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflect", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_PRECISION))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Precision", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_AGILITY))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Agility", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_KNOCKOUT))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Knockout", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_KING))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "King", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_PLAGUE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Plague", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_SUPERNOVA))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Supernova", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_POWERUPMODE_DOMINANT))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Dominant", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pWeapon = pPlayer->GetWeaponFromSlot(i)->As<CTFSpellBook>();
			if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_SPELLBOOK || !pWeapon->m_iSpellCharges())
				continue;

			switch (pWeapon->m_iSelectedSpellIndex())
			{
			case 0: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Fireball", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 1: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bats", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 2: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Heal", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 3: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Pumpkins", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 4: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Jump", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 5: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Stealth", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 6: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Teleport", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 7: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Lightning", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 8: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Minify", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 9: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Meteors", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 10: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Monoculus", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 11: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Skeletons", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 12: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Glove", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 13: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Parachute", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 14: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Heal", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 15: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bomb", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			}
		}

		if (pPlayer->InCond(TF_COND_RADIUSHEAL) ||
			pPlayer->InCond(TF_COND_HEALTH_BUFF) ||
			pPlayer->InCond(TF_COND_RADIUSHEAL_ON_DAMAGE) ||
			pPlayer->InCond(TF_COND_HALLOWEEN_QUICK_HEAL) ||
			pPlayer->InCond(TF_COND_HALLOWEEN_HELL_HEAL) ||
			pPlayer->InCond(TF_COND_KING_BUFFED))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Heal", Vars::Colors::IndicatorTextGood.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_HEALTH_OVERHEALED))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "HP+", Vars::Colors::IndicatorTextGood.Value, Vars::Menu::Theme::Background.Value);

		//if (pPlayer->InCond(TF_COND_BLASTJUMPING))
		//	tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Blastjump", Vars::Colors::IndicatorTextMid.Value, Vars::Menu::Theme::Background.Value);
	}

	// Debuffs
	if (pGroup->m_tESP.Draw & ESPEnum::Debuffs)
	{
		if (pPlayer->InCond(TF_COND_MARKEDFORDEATH)
			|| pPlayer->InCond(TF_COND_MARKEDFORDEATH_SILENT)
			|| pPlayer->InCond(TF_COND_PASSTIME_PENALTY_DEBUFF))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Marked", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_URINE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Jarate", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_MAD_MILK))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Milk", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_STUNNED))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Stun", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_BURNING))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Burn", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_BLEEDING))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bleed", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	// Misc
	if (pGroup->m_tESP.Draw & ESPEnum::Flags)
	{
		if (pPlayer->m_bFeignDeathReady())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "DR", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_FEIGN_DEATH))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Feign", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (float flInvis = pPlayer->GetEffectiveInvisibilityLevel())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("Invis {:.0f}%", flInvis * 100), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_DISGUISED))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Disguise", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_AIMING) || pPlayer->InCond(TF_COND_ZOOMED))
		{
			switch (pWeapon ? pWeapon->GetWeaponID() : -1)
			{
			case TF_WEAPON_MINIGUN:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Rev", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			case TF_WEAPON_SNIPERRIFLE:
			case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			case TF_WEAPON_SNIPERRIFLE_DECAP:
			{
				if (bLocal)
				{
					tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("Charging {:.0f}%", Math::RemapVal(pWeapon->As<CTFSniperRifle>()->m_flChargedDamage(), 0.f, 150.f, 0.f, 100.f)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
					break;
				}
				else
				{
					auto fGetSniperDot = [](CBaseEntity* pEntity) -> CSniperDot*
					{
						for (auto pDot : H::Entities.GetGroup(EntityEnum::SniperDots))
						{
							if (pDot->m_hOwnerEntity().Get() == pEntity)
								return pDot->As<CSniperDot>();
						}
						return nullptr;
					};
					if (CSniperDot* pPlayerDot = fGetSniperDot(pPlayer))
					{
						float flChargeTime = std::max(SDK::AttribHookValue(3.f, "mult_sniper_charge_per_sec", pWeapon), 1.5f);
						tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("Charging {:.0f}%", Math::RemapVal(TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick) - pPlayerDot->m_flChargeStartTime() - 0.3f, 0.f, flChargeTime, 0.f, 100.f)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
						break;
					}
				}
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charging", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			}
			case TF_WEAPON_COMPOUND_BOW:
				if (bLocal)
				{
					tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("Charging {:.0f}%", Math::RemapVal(TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick) - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(), 0.f, 1.f, 0.f, 100.f)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
					break;
				}
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charging", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			default:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charging", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
			}
		}

		if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charging", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (Vars::Visuals::Removals::Taunts.Value && pPlayer->InCond(TF_COND_TAUNTING))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Taunt", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (Vars::Debug::Info.Value && !bLocal /*&& !pPlayer->IsDormant()*/)
		{
			int iAverage = TIME_TO_TICKS(F::MoveSim.GetPredictedDelta(pPlayer));
			int iCurrent = H::Entities.GetChoke(iIndex);
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("Lag {}, {}", iAverage, iCurrent), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		}
	}
}

static inline void StoreBuilding(CBaseObject* pBuilding, CTFPlayer* pLocal, Group_t* pGroup, std::unordered_map<CBaseEntity*, BuildingCache_t>& mCache)
{
	auto pOwner = pBuilding->m_hBuilder().Get();
	int iIndex = pOwner ? pOwner->entindex() : -1;

	bool bIsMini = pBuilding->m_bMiniBuilding();

	float flAlpha;
	if (!GetDistanceThing(pBuilding->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	BuildingCache_t& tCache = mCache[pBuilding];
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pOwner ? pOwner : pBuilding, pGroup).Alpha(255);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;
	tCache.m_iBoxStyle = pGroup->m_tESP.BoxStyle;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance)
	{
		Vec3 vDelta = pBuilding->m_vecOrigin() - pLocal->m_vecOrigin();
		tCache.m_vText.emplace_back(ALIGN_BOTTOM, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Name)
	{
		const char* sName = "Building";
		switch (pBuilding->GetClassID())
		{
		case ETFClassID::CObjectSentrygun: sName = bIsMini ? "Mini-Sentry" : "Sentry"; break;
		case ETFClassID::CObjectDispenser: sName = "Dispenser"; break;
		case ETFClassID::CObjectTeleporter: sName = pBuilding->m_iObjectMode() ? "Teleporter Exit" : "Teleporter Entrance";
		}
		tCache.m_vText.emplace_back(ALIGN_TOP, sName, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);
	}

	float flHealth = pBuilding->m_iHealth(), flMaxHealth = pBuilding->m_iMaxHealth();
	if (pGroup->m_tESP.Draw & ESPEnum::HealthBar)
	{
		tCache.m_flHealth = std::clamp(flHealth / flMaxHealth, 0.f, 1.f);
		
		Color_t tColor = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorGood.Value, std::clamp(tCache.m_flHealth, 0.f, 1.f), LerpEnum::HSV);
		Bar_t& tBar = tCache.m_vBars.emplace_back();
		tBar.m_iMode = ALIGN_LEFT;
		tBar.m_flPercent = tCache.m_flHealth;
		tBar.m_tColor = tColor;
		tBar.m_tOverfill = Vars::Colors::IndicatorMisc.Value;
		tBar.m_tBackground = Color_t(0, 0, 0, 120);
		tBar.m_bSmooth = true;
	}
	if (pGroup->m_tESP.Draw & ESPEnum::HealthText)
		tCache.m_vText.emplace_back(ALIGN_LEFT, std::format("{}", flHealth), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

	if (pGroup->m_tESP.Draw & (ESPEnum::AmmoBars | ESPEnum::AmmoText) && pBuilding->IsSentrygun() && !pBuilding->m_bBuilding())
	{
		int iShells, iMaxShells, iRockets, iMaxRockets; pBuilding->As<CObjectSentrygun>()->GetAmmoCount(iShells, iMaxShells, iRockets, iMaxRockets);

		if (pGroup->m_tESP.Draw & ESPEnum::AmmoBars)
		{
			Bar_t& shellBar = tCache.m_vBars.emplace_back();
			shellBar.m_iMode = ALIGN_BOTTOM;
			shellBar.m_flPercent = float(iShells) / iMaxShells;
			shellBar.m_tColor = Vars::Menu::Theme::Inactive.Value;
			shellBar.m_bSmooth = false;
			
			if (iMaxRockets)
			{
				Bar_t& rocketBar = tCache.m_vBars.emplace_back();
				rocketBar.m_iMode = ALIGN_BOTTOM;
				rocketBar.m_flPercent = float(iRockets) / iMaxRockets;
				rocketBar.m_tColor = Vars::Menu::Theme::Inactive.Value;
				rocketBar.m_bSmooth = false;
			}
		}
		if (pGroup->m_tESP.Draw & ESPEnum::AmmoText)
		{
			tCache.m_vText.emplace_back(ALIGN_BOTTOMRIGHT, std::format("{}", iShells), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
			if (iMaxRockets)
				tCache.m_vText.back().m_sText += std::format(", {}", iRockets);
		}
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Owner && !pBuilding->m_bWasMapPlaced() && pOwner)
	{
		if (auto pResource = H::Entities.GetResource(); pResource)
			tCache.m_vText.emplace_back(ALIGN_TOP, F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Level && !bIsMini)
		tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("Level {}", pBuilding->m_iUpgradeLevel()), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

	if (pGroup->m_tESP.Draw & ESPEnum::Flags)
	{
		if (!pBuilding->IsDormant() && pBuilding->m_bBuilding())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("{:.0f}%", pBuilding->m_flPercentageConstructed() * 100), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pBuilding->IsSentrygun() && pBuilding->As<CObjectSentrygun>()->m_bPlayerControlled())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Wrangled", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);

		if (pBuilding->m_bHasSapper())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Sapped", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		else if (pBuilding->m_bDisabled())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Disabled", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}
}

static inline const char* GetProjectileName(CBaseEntity* pProjectile)
{
	const char* sReturn = "Projectile";
	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFWeaponBaseMerasmusGrenade: sReturn = "Bomb"; break;
	case ETFClassID::CTFGrenadePipebombProjectile: sReturn = pProjectile->As<CTFGrenadePipebombProjectile>()->HasStickyEffects() ? "Sticky" : "Pipe"; break;
	case ETFClassID::CTFStunBall: sReturn = "Baseball"; break;
	case ETFClassID::CTFBall_Ornament: sReturn = "Bauble"; break;
	case ETFClassID::CTFProjectile_Jar: sReturn = "Jarate"; break;
	case ETFClassID::CTFProjectile_Cleaver: sReturn = "Cleaver"; break;
	case ETFClassID::CTFProjectile_JarGas: sReturn = "Gas"; break;
	case ETFClassID::CTFProjectile_JarMilk:
	case ETFClassID::CTFProjectile_ThrowableBreadMonster: sReturn = "Milk"; break;
	case ETFClassID::CTFProjectile_SpellBats:
	case ETFClassID::CTFProjectile_SpellKartBats: sReturn = "Bats"; break;
	case ETFClassID::CTFProjectile_SpellMeteorShower: sReturn = "Meteors"; break;
	case ETFClassID::CTFProjectile_SpellMirv:
	case ETFClassID::CTFProjectile_SpellPumpkin: sReturn = "Pumpkin"; break;
	case ETFClassID::CTFProjectile_SpellSpawnBoss: sReturn = "Monoculus"; break;
	case ETFClassID::CTFProjectile_SpellSpawnHorde:
	case ETFClassID::CTFProjectile_SpellSpawnZombie: sReturn = "Skeleton"; break;
	case ETFClassID::CTFProjectile_SpellTransposeTeleport: sReturn = "Teleport"; break;
	case ETFClassID::CTFProjectile_Arrow: sReturn = pProjectile->As<CTFProjectile_Arrow>()->m_iProjectileType() == TF_PROJECTILE_BUILDING_REPAIR_BOLT ? "Repair" : "Arrow"; break;
	case ETFClassID::CTFProjectile_GrapplingHook: sReturn = "Grapple"; break;
	case ETFClassID::CTFProjectile_HealingBolt: sReturn = "Heal"; break;
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_EnergyBall:
	case ETFClassID::CTFProjectile_SentryRocket: sReturn = "Rocket"; break;
	case ETFClassID::CTFProjectile_BallOfFire: sReturn = "Fire"; break;
	case ETFClassID::CTFProjectile_MechanicalArmOrb: sReturn = "Short circuit"; break;
	case ETFClassID::CTFProjectile_SpellFireball: sReturn = "Fireball"; break;
	case ETFClassID::CTFProjectile_SpellLightningOrb: sReturn = "Lightning"; break;
	case ETFClassID::CTFProjectile_SpellKartOrb: sReturn = "Fist"; break;
	case ETFClassID::CTFProjectile_Flare: sReturn = "Flare"; break;
	case ETFClassID::CTFProjectile_EnergyRing: sReturn = "Energy"; break;
	}
	return sReturn;
}
static inline void StoreProjectile(CBaseEntity* pProjectile, CTFPlayer* pLocal, Group_t* pGroup, std::unordered_map<CBaseEntity*, EntityCache_t>& mCache)
{
	auto pOwner = F::ProjSim.GetEntities(pProjectile).second;
	int iIndex = pOwner ? pOwner->entindex() : -1;

	float flAlpha;
	if (!GetDistanceThing(pProjectile->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	EntityCache_t& tCache = mCache[pProjectile];
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pOwner ? pOwner : pProjectile, pGroup);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;
	tCache.m_iBoxStyle = pGroup->m_tESP.BoxStyle;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance)
	{
		Vec3 vDelta = pProjectile->m_vecOrigin() - pLocal->m_vecOrigin();
		tCache.m_vText.emplace_back(ALIGN_BOTTOM, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Name)
		tCache.m_vText.emplace_back(ALIGN_TOP, GetProjectileName(pProjectile), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);

	if (pGroup->m_tESP.Draw & ESPEnum::Owner && pOwner)
	{
		if (auto pResource = H::Entities.GetResource(); pResource)
			tCache.m_vText.emplace_back(ALIGN_TOP, F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Flags)
	{
		switch (pProjectile->GetClassID())
		{
		case ETFClassID::CTFWeaponBaseGrenadeProj:
		case ETFClassID::CTFWeaponBaseMerasmusGrenade:
		case ETFClassID::CTFGrenadePipebombProjectile:
		case ETFClassID::CTFStunBall:
		case ETFClassID::CTFBall_Ornament:
		case ETFClassID::CTFProjectile_Jar:
		case ETFClassID::CTFProjectile_Cleaver:
		case ETFClassID::CTFProjectile_JarGas:
		case ETFClassID::CTFProjectile_JarMilk:
		case ETFClassID::CTFProjectile_SpellBats:
		case ETFClassID::CTFProjectile_SpellKartBats:
		case ETFClassID::CTFProjectile_SpellMeteorShower:
		case ETFClassID::CTFProjectile_SpellMirv:
		case ETFClassID::CTFProjectile_SpellPumpkin:
		case ETFClassID::CTFProjectile_SpellSpawnBoss:
		case ETFClassID::CTFProjectile_SpellSpawnHorde:
		case ETFClassID::CTFProjectile_SpellSpawnZombie:
		case ETFClassID::CTFProjectile_SpellTransposeTeleport:
		case ETFClassID::CTFProjectile_Throwable:
		case ETFClassID::CTFProjectile_ThrowableBreadMonster:
		case ETFClassID::CTFProjectile_ThrowableBrick:
		case ETFClassID::CTFProjectile_ThrowableRepel:
			if (pProjectile->As<CTFWeaponBaseGrenadeProj>()->m_bCritical())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crit", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFWeaponBaseGrenadeProj>()->m_iDeflected() && (pProjectile->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile || !pProjectile->GetAbsVelocity().IsZero()))
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		case ETFClassID::CTFProjectile_Arrow:
		case ETFClassID::CTFProjectile_GrapplingHook:
		case ETFClassID::CTFProjectile_HealingBolt:
			if (pProjectile->As<CTFProjectile_Arrow>()->m_bCritical())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crit", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFProjectile_Arrow>()->m_bArrowAlight())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Alight", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		case ETFClassID::CTFProjectile_Rocket:
		case ETFClassID::CTFProjectile_BallOfFire:
		case ETFClassID::CTFProjectile_MechanicalArmOrb:
		case ETFClassID::CTFProjectile_SentryRocket:
		case ETFClassID::CTFProjectile_SpellFireball:
		case ETFClassID::CTFProjectile_SpellLightningOrb:
		case ETFClassID::CTFProjectile_SpellKartOrb:
			if (pProjectile->As<CTFProjectile_Rocket>()->m_bCritical())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crit", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		case ETFClassID::CTFProjectile_EnergyBall:
			if (pProjectile->As<CTFProjectile_EnergyBall>()->m_bChargedShot())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charge", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		case ETFClassID::CTFProjectile_Flare:
			if (pProjectile->As<CTFProjectile_Flare>()->m_bCritical())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crit", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		}
	}
}

static inline void StoreObjective(CBaseEntity* pObjective, CTFPlayer* pLocal, Group_t* pGroup, std::unordered_map<CBaseEntity*, EntityCache_t>& mCache)
{
	auto pOwner = pObjective->m_hOwnerEntity()->As<CTFPlayer>();
	if (pOwner == pLocal)
		return;

	float flAlpha;
	if (!GetDistanceThing(pObjective->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	EntityCache_t& tCache = mCache[pObjective];
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pObjective, pGroup);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;
	tCache.m_iBoxStyle = pGroup->m_tESP.BoxStyle;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance)
	{
		Vec3 vDelta = pObjective->m_vecOrigin() - pLocal->m_vecOrigin();
		tCache.m_vText.emplace_back(ALIGN_BOTTOM, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	switch (pObjective->GetClassID())
	{
	case ETFClassID::CCaptureFlag:
	{
		auto pIntel = pObjective->As<CCaptureFlag>();

		if (pGroup->m_tESP.Draw & ESPEnum::Name)
			tCache.m_vText.emplace_back(ALIGN_TOP, "Intel", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);

		if (pGroup->m_tESP.Draw & ESPEnum::Flags)
		{
			switch (pIntel->m_nFlagStatus())
			{
			case TF_FLAGINFO_HOME:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Home", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			case TF_FLAGINFO_DROPPED:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Dropped", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			default:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Stolen", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			}
		}

		if (pGroup->m_tESP.Draw & ESPEnum::IntelReturnTime && pIntel->m_nFlagStatus() == TF_FLAGINFO_DROPPED)
		{
			float flReturnTime = std::max(pIntel->m_flResetTime() - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick), 0.f);
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, std::format("Return {:.1f}s", pIntel->m_flResetTime() - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick)).c_str(), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		}

		break;
	}
	}
}

static inline void StoreMisc(CBaseEntity* pEntity, CTFPlayer* pLocal, Group_t* pGroup, std::unordered_map<CBaseEntity*, EntityCache_t>& mCache)
{
	float flAlpha;
	if (!GetDistanceThing(pEntity->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	EntityCache_t& tCache = mCache[pEntity];
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pEntity, pGroup);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;
	tCache.m_iBoxStyle = pGroup->m_tESP.BoxStyle;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance)
	{
		Vec3 vDelta = pEntity->m_vecOrigin() - pLocal->m_vecOrigin();
		tCache.m_vText.emplace_back(ALIGN_BOTTOM, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Name)
	{
		const char* sName = "Unknown";
		switch (pEntity->GetClassID())
		{
		case ETFClassID::CTFBaseBoss: sName = "NPC"; break;
		case ETFClassID::CTFTankBoss: sName = "Tank"; break;
		case ETFClassID::CMerasmus: sName = "Merasmus"; break;
		case ETFClassID::CEyeballBoss: sName = "Monoculus"; break;
		case ETFClassID::CHeadlessHatman: sName = "Horseless Headless Horsemann"; break;
		case ETFClassID::CZombie: sName = "Skeleton"; break;
		case ETFClassID::CBaseAnimating:
		{
			auto uHash = H::Entities.GetModel(pEntity->entindex());
			if (H::Entities.IsHealth(uHash))
				sName = "Health";
			else if (H::Entities.IsAmmo(uHash))
				sName = "Ammo";
			else if (H::Entities.IsSpellbook(uHash))
				sName = "Spellbook";
			else if (H::Entities.IsPowerup(uHash))
			{
				sName = "Powerup";
				switch (uHash)
				{
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_agility.mdl"): sName = "Agility"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_crit.mdl"): sName = "Revenge"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_defense.mdl"): sName = "Resistance"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_haste.mdl"): sName = "Haste"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_king.mdl"): sName = "King"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_knockout.mdl"): sName = "Knockout"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_plague.mdl"): sName = "Plague"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_precision.mdl"): sName = "Precision"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_reflect.mdl"): sName = "Reflect"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_regen.mdl"): sName = "Regeneration"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_strength.mdl"): sName = "Strength"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_supernova.mdl"): sName = "Supernova"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_vampire.mdl"): sName = "Vampire";
				}
			}
			break;
		}
		case ETFClassID::CTFAmmoPack: sName = "Ammo"; break;
		case ETFClassID::CCurrencyPack: sName = "Money"; break;
		case ETFClassID::CTFGenericBomb:
		case ETFClassID::CTFPumpkinBomb: sName = "Bomb"; break;
		case ETFClassID::CHalloweenGiftPickup: sName = "Gargoyle"; break;
		}

		tCache.m_vText.emplace_back(ALIGN_TOP, sName, pGroup->m_tColor, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);
	}
}

void CESP::Store(CTFPlayer* pLocal)
{
	m_mPlayerCache.clear();
	m_mBuildingCache.clear();
	m_mEntityCache.clear();
	if (!pLocal || !F::Groups.GroupsActive())
		return;

	for (auto& [pEntity, pGroup] : F::Groups.GetGroup(false))
	{
		if (!pGroup->m_tESP.Draw)
			continue;

		if (pEntity->IsPlayer())
			StorePlayer(pEntity->As<CTFPlayer>(), pLocal, pGroup, m_mPlayerCache);
		else if (pEntity->IsBuilding())
			StoreBuilding(pEntity->As<CBaseObject>(), pLocal, pGroup, m_mBuildingCache);
		else if (pEntity->IsProjectile())
			StoreProjectile(pEntity, pLocal, pGroup, m_mEntityCache);
		else if (pEntity->GetClassID() == ETFClassID::CCaptureFlag)
			StoreObjective(pEntity, pLocal, pGroup, m_mEntityCache);
		else
			StoreMisc(pEntity, pLocal, pGroup, m_mEntityCache);
	}
}

static matrix3x4 s_aBones[MAXSTUDIOBONES];
static matrix3x4 s_mTransform = {};

static const char* GetClassIconTexture(int iClassNum)
{
	switch (iClassNum)
	{
	case TF_CLASS_SCOUT: return "hud/leaderboard_class_scout";
	case TF_CLASS_SOLDIER: return "hud/leaderboard_class_soldier";
	case TF_CLASS_PYRO: return "hud/leaderboard_class_pyro";
	case TF_CLASS_DEMOMAN: return "hud/leaderboard_class_demo";
	case TF_CLASS_HEAVY: return "hud/leaderboard_class_heavy";
	case TF_CLASS_ENGINEER: return "hud/leaderboard_class_engineer";
	case TF_CLASS_MEDIC: return "hud/leaderboard_class_medic";
	case TF_CLASS_SNIPER: return "hud/leaderboard_class_sniper";
	case TF_CLASS_SPY: return "hud/leaderboard_class_spy";
	}
	return "vgui/glyph_multiplayer";
}

void CESP::CacheDrawInfo(CTFPlayer* pLocal)
{
	ESPDrawCache_t tCache = {};

	if (!pLocal)
	{
		m_mMotion.clear();
		F::RenderSync.Set(m_tDrawCache, std::move(tCache));
		return;
	}

	Math::AngleMatrix({ 0.f, I::EngineClient->GetViewAngles().y, 0.f }, s_mTransform, false);

	const double dNow = SDK::PlatFloatTime();
	const int iLocal = I::EngineClient->GetLocalPlayer();
	int iObserved = -1;
	switch (pLocal->m_iObserverMode())
	{
	case OBS_MODE_FIRSTPERSON:
	case OBS_MODE_THIRDPERSON:
		iObserved = pLocal->m_hObserverTarget().GetEntryIndex();
	}
	auto fShouldSmooth = [&](CBaseEntity* pEntity)
	{
		const int iIndex = pEntity->entindex();
		return iIndex != iLocal && iIndex != iObserved;
	};

	auto fFillCommon = [](ESPDrawEntity_t& tOut, const EntityCache_t& tSource, CBaseEntity* pEntity, float x, float y, float w, float h)
	{
		tOut.m_pKey = pEntity;
		tOut.m_flX = x; tOut.m_flY = y; tOut.m_flW = w; tOut.m_flH = h;
		tOut.m_flAlpha = tSource.m_flAlpha;
		tOut.m_tColor = tSource.m_tColor;
		tOut.m_bBox = tSource.m_bBox;
		tOut.m_iBoxStyle = tSource.m_iBoxStyle;
		tOut.m_vText = tSource.m_vText;
	};

	Vec3 vOffset;
	for (auto& [pEntity, tSource] : m_mEntityCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, fShouldSmooth(pEntity), dNow, vOffset, x, y, w, h))
			continue;

		ESPDrawEntity_t& tOut = tCache.m_vWorld.emplace_back();
		fFillCommon(tOut, tSource, pEntity, x, y, w, h);
	}

	for (auto& [pEntity, tSource] : m_mBuildingCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, fShouldSmooth(pEntity), dNow, vOffset, x, y, w, h))
			continue;

		ESPDrawEntity_t& tOut = tCache.m_vBuildings.emplace_back();
		fFillCommon(tOut, tSource, pEntity, x, y, w, h);
		tOut.m_vBars = tSource.m_vBars;
		tOut.m_flHealth = tSource.m_flHealth;
	}

	for (auto& [pEntity, tSource] : m_mPlayerCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, fShouldSmooth(pEntity), dNow, vOffset, x, y, w, h))
			continue;

		ESPDrawEntity_t& tOut = tCache.m_vPlayers.emplace_back();
		fFillCommon(tOut, tSource, pEntity, x, y, w, h);
		tOut.m_vBars = tSource.m_vBars;
		tOut.m_flHealth = tSource.m_flHealth;

		if (tSource.m_iClassIcon)
			tOut.m_vBadges.emplace_back(EESPBadge::Class, GetClassIconTexture(tSource.m_iClassIcon), Color_t(255, 255, 255, 255), 18.f, 18.f);
		if (const CHudTexture* pIcon = tSource.m_pWeaponIcon; pIcon && !pIcon->bRenderUsingFont && pIcon->szTextureFile[0])
			tOut.m_vBadges.emplace_back(EESPBadge::Weapon, pIcon->szTextureFile, Vars::Menu::Theme::Active.Value,
				float(pIcon->Width()), float(pIcon->Height()), pIcon->texCoords[0], pIcon->texCoords[1], pIcon->texCoords[2], pIcon->texCoords[3]);

		if (tSource.m_bBones)
			CacheBones(pEntity->As<CTFPlayer>(), vOffset, tOut);
	}

	for (auto it = m_mMotion.begin(); it != m_mMotion.end();)
	{
		if (dNow - it->second.m_dTime > 1.0)
			it = m_mMotion.erase(it);
		else
			++it;
	}

	F::RenderSync.Set(m_tDrawCache, std::move(tCache));
}

void CESP::CacheBones(CTFPlayer* pPlayer, const Vec3& vOffset, ESPDrawEntity_t& tOut)
{
	if (!pPlayer->SetupBones(s_aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
		return;

	const int iHead = pPlayer->GetBaseToHitbox(HITBOX_HEAD);
	const int iSpine2 = pPlayer->GetBaseToHitbox(HITBOX_SPINE2);
	const int iPelvis = pPlayer->GetBaseToHitbox(HITBOX_PELVIS);
	const int iLeftUpperarm = pPlayer->GetBaseToHitbox(HITBOX_LEFT_UPPERARM);
	const int iLeftForearm = pPlayer->GetBaseToHitbox(HITBOX_LEFT_FOREARM);
	const int iLeftHand = pPlayer->GetBaseToHitbox(HITBOX_LEFT_HAND);
	const int iRightUpperarm = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_UPPERARM);
	const int iRightForearm = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_FOREARM);
	const int iRightHand = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_HAND);
	const int iLeftThigh = pPlayer->GetBaseToHitbox(HITBOX_LEFT_THIGH);
	const int iLeftCalf = pPlayer->GetBaseToHitbox(HITBOX_LEFT_CALF);
	const int iLeftFoot = pPlayer->GetBaseToHitbox(HITBOX_LEFT_FOOT);
	const int iRightThigh = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_THIGH);
	const int iRightCalf = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_CALF);
	const int iRightFoot = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_FOOT);

	CacheBoneChain(pPlayer, s_aBones, { iHead, iSpine2, iPelvis }, vOffset, tOut);
	CacheBoneChain(pPlayer, s_aBones, { iSpine2, iLeftUpperarm, iLeftForearm, iLeftHand }, vOffset, tOut);
	CacheBoneChain(pPlayer, s_aBones, { iSpine2, iRightUpperarm, iRightForearm, iRightHand }, vOffset, tOut);
	CacheBoneChain(pPlayer, s_aBones, { iPelvis, iLeftThigh, iLeftCalf, iLeftFoot }, vOffset, tOut);
	CacheBoneChain(pPlayer, s_aBones, { iPelvis, iRightThigh, iRightCalf, iRightFoot }, vOffset, tOut);
}

void CESP::CacheBoneChain(CTFPlayer* pPlayer, matrix3x4* aBones, const std::vector<int>& vBones, const Vec3& vOffset, ESPDrawEntity_t& tOut)
{
	for (size_t n = 1; n < vBones.size(); n++)
	{
		const Vec3 vBone1 = pPlayer->GetHitboxCenter(aBones, vBones[n]) + vOffset;
		const Vec3 vBone2 = pPlayer->GetHitboxCenter(aBones, vBones[n - 1]) + vOffset;

		Vec3 vScreen1, vScreen2;
		if (SDK::W2S(vBone1, vScreen1) && SDK::W2S(vBone2, vScreen2))
			tOut.m_vBones.emplace_back(Vec2(vScreen1.x, vScreen1.y), Vec2(vScreen2.x, vScreen2.y));
	}
}

static inline float SmoothingAlpha(float flDelta, float flCutoff)
{
	const float flTau = 1.f / (2.f * 3.14159265f * flCutoff);
	return 1.f / (1.f + flTau / flDelta);
}

static inline void UpdateMotion(ESPMotion_t& tMotion, bool bReset, const Vec3& vOrigin, const Vec3& vMins, const Vec3& vMaxs, double dNow)
{
	const float flDelta = float(dNow - tMotion.m_dTime);
	if (bReset || flDelta > 0.25f || tMotion.m_vPos.DistTo(vOrigin) > 64.f)
	{
		tMotion = { vOrigin, {}, vMins, vMaxs, dNow };
		return;
	}
	if (flDelta < 0.0001f)
		return;

	const Vec3 vRawVelocity = (vOrigin - tMotion.m_vPos) / flDelta;
	tMotion.m_vVelocity += (vRawVelocity - tMotion.m_vVelocity) * SmoothingAlpha(flDelta, 1.f);

	const float flCutoff = 3.5f + 0.03f * tMotion.m_vVelocity.Length();
	tMotion.m_vPos += (vOrigin - tMotion.m_vPos) * SmoothingAlpha(flDelta, flCutoff);

	const float flExtents = SmoothingAlpha(flDelta, 4.f);
	tMotion.m_vMins += (vMins - tMotion.m_vMins) * flExtents;
	tMotion.m_vMaxs += (vMaxs - tMotion.m_vMaxs) * flExtents;
	tMotion.m_dTime = dNow;
}

bool CESP::GetDrawBounds(CBaseEntity* pEntity, bool bSmooth, double dNow, Vec3& vOffset, float& x, float& y, float& w, float& h)
{
	const Vec3 vRawOrigin = pEntity->GetAbsOrigin();
	Vec3 vOrigin = vRawOrigin, vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();
	if (bSmooth)
	{
		auto [it, bNew] = m_mMotion.try_emplace(pEntity);
		UpdateMotion(it->second, bNew, vRawOrigin, vMins, vMaxs, dNow);
		vOrigin = it->second.m_vPos;
		vMins = it->second.m_vMins;
		vMaxs = it->second.m_vMaxs;
	}
	else
		m_mMotion.erase(pEntity);
	vOffset = vOrigin - vRawOrigin;

	Math::MatrixInitialize(s_mTransform, vOrigin, false);

	const Vec3 vPoints[] = {
		Vec3(0.f, 0.f, vMins.z),
		Vec3(0.f, 0.f, vMaxs.z),
		Vec3(vMins.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMins.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f)
	};

	float flLeft = 0.f, flRight = 0.f, flTop = 0.f, flBottom = 0.f;
	for (int n = 0; n < 6; n++)
	{
		Vec3 vPoint; Math::VectorTransform(vPoints[n], s_mTransform, vPoint);

		Vec3 vScreen;
		if (!SDK::W2S(vPoint, vScreen))
			return false;

		flLeft = n ? std::min(flLeft, vScreen.x) : vScreen.x;
		flRight = n ? std::max(flRight, vScreen.x) : vScreen.x;
		flTop = n ? std::max(flTop, vScreen.y) : vScreen.y;
		flBottom = n ? std::min(flBottom, vScreen.y) : vScreen.y;
	}

	x = flLeft;
	y = flBottom;
	w = flRight - flLeft;
	h = flTop - flBottom;

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CTFPlayer:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter:
		x += w * 0.125f;
		w *= 0.75f;
	}

	return !(x > H::Draw.m_nScreenW || x + w < 0 || y > H::Draw.m_nScreenH || y + h < 0);
}

ESPAnim_t& CESP::GetAnim(const void* pKey)
{
	auto [it, _] = m_mAnims.try_emplace(pKey);
	it->second.m_flLastSeen = m_flAnimTime;
	return it->second;
}

void CESP::CleanupAnims()
{
	for (auto it = m_mAnims.begin(); it != m_mAnims.end();)
	{
		if (m_flAnimTime - it->second.m_flLastSeen > 1.f)
			it = m_mAnims.erase(it);
		else
			++it;
	}
}

static inline Color_t FadeColor(Color_t tColor, float flAlpha)
{
	tColor.a = byte(std::clamp(float(tColor.a) * flAlpha, 0.f, 255.f));
	return tColor;
}

static inline float Approach(float flCurrent, float flTarget, float flRate, float flDelta)
{
	const float flStep = std::clamp(flDelta * flRate, 0.f, 1.f);
	float flOut = std::lerp(flCurrent, flTarget, flStep);
	if (std::fabs(flOut - flTarget) <= 0.001f)
		flOut = flTarget;
	return flOut;
}

static inline float EaseOutCubic(float t)
{
	t = std::clamp(t, 0.f, 1.f);
	const float f = 1.f - t;
	return 1.f - f * f * f;
}

static void DrawCornerBox(ImDrawList* pDrawList, float x, float y, float w, float h, Color_t tColor, float flAlpha, float flGrow, bool bAccent)
{
	if (w <= 1.f || h <= 1.f)
		return;

	const float flThickness = std::max(roundf(H::Draw.Scale(1.f)), 1.f);
	const float flLen = std::max(std::min(w, h) * 0.26f, H::Draw.Scale(4.f)) * flGrow;
	const ImU32 uShadow = ColorToU32(FadeColor(Color_t(0, 0, 0, 190), flAlpha));
	const ImU32 uMain = ColorToU32(FadeColor(tColor, flAlpha));

	x += 0.5f, y += 0.5f;
	const float aCorners[4][2] = { { x, y }, { x + w, y }, { x, y + h }, { x + w, y + h } };
	const float aDirs[4][2] = { { 1.f, 1.f }, { -1.f, 1.f }, { 1.f, -1.f }, { -1.f, -1.f } };

	for (int i = 0; i < 4; i++)
	{
		const float cx = aCorners[i][0], cy = aCorners[i][1];
		const float dx = aDirs[i][0], dy = aDirs[i][1];

		pDrawList->AddLine({ cx + 1.f, cy + 1.f }, { cx + flLen * dx + 1.f, cy + 1.f }, uShadow, flThickness);
		pDrawList->AddLine({ cx + 1.f, cy + 1.f }, { cx + 1.f, cy + flLen * dy + 1.f }, uShadow, flThickness);

		pDrawList->AddLine({ cx, cy }, { cx + flLen * dx, cy }, uMain, flThickness);
		pDrawList->AddLine({ cx, cy }, { cx, cy + flLen * dy }, uMain, flThickness);
	}

	if (bAccent)
		pDrawList->AddRect({ x, y }, { x + w, y + h }, ColorToU32(FadeColor(tColor, flAlpha * 0.22f)), H::Draw.Scale(2.f), 0, flThickness * 0.75f);
}

static void DrawVerticalBar(ImDrawList* pDrawList, float x, float y, float w, float h,
	float flFill, float flGhost, Color_t tColor, Color_t tOverfill, Color_t tBackground, float flAlpha)
{
	const float flRound = w * 0.5f;

	if (tBackground.a)
	{
		pDrawList->AddRectFilled({ x - 1.f, y - 1.f }, { x + w + 1.f, y + h + 1.f },
			ColorToU32(FadeColor(tBackground, flAlpha)), flRound + 1.f);
	}

	if (flGhost > flFill)
	{
		const float flGhostH = h * std::clamp(flGhost, 0.f, 1.f);
		const float flFillH = h * std::clamp(flFill, 0.f, 1.f);
		pDrawList->AddRectFilled({ x, y + h - flGhostH }, { x + w, y + h - flFillH },
			ColorToU32(FadeColor(Color_t(235, 70, 70, 160), flAlpha)), flRound);
	}

	const float flClamped = std::clamp(flFill, 0.f, 1.f);
	if (flClamped > 0.f)
	{
		const float flFillH = std::max(h * flClamped, 1.f);
		const Color_t tTop = tColor.Lerp(Color_t(255, 255, 255, tColor.a), 0.3f, LerpEnum::NoAlpha);
		pDrawList->AddRectFilledMultiColor(
			{ x, y + h - flFillH }, { x + w, y + h },
			ColorToU32(FadeColor(tTop, flAlpha)), ColorToU32(FadeColor(tTop, flAlpha)),
			ColorToU32(FadeColor(tColor, flAlpha)), ColorToU32(FadeColor(tColor, flAlpha)));
	}

	if (flFill > 1.f && tOverfill.a)
	{
		const float flOverH = std::clamp(flFill - 1.f, 0.f, 1.f) * h;
		pDrawList->AddRectFilled({ x, y + h - flOverH }, { x + w, y + h },
			ColorToU32(FadeColor(tOverfill, flAlpha * 0.85f)), flRound);
	}
}

static void DrawHorizontalBar(ImDrawList* pDrawList, float x, float y, float w, float h,
	float flFill, Color_t tColor, Color_t tBackground, float flAlpha, float flTime)
{
	const float flRound = h * 0.5f;

	if (tBackground.a)
	{
		pDrawList->AddRectFilled({ x - 1.f, y - 1.f }, { x + w + 1.f, y + h + 1.f },
			ColorToU32(FadeColor(tBackground, flAlpha)), flRound + 1.f);
	}

	const float flClamped = std::clamp(flFill, 0.f, 1.f);
	if (flClamped <= 0.f)
		return;

	const float flFillW = std::max(w * flClamped, 1.f);
	const Color_t tRight = tColor.Lerp(Color_t(255, 255, 255, tColor.a), 0.35f, LerpEnum::NoAlpha);
	pDrawList->AddRectFilledMultiColor(
		{ x, y }, { x + flFillW, y + h },
		ColorToU32(FadeColor(tColor, flAlpha)), ColorToU32(FadeColor(tRight, flAlpha)),
		ColorToU32(FadeColor(tRight, flAlpha)), ColorToU32(FadeColor(tColor, flAlpha)));

	if (flClamped >= 0.999f)
	{
		const float flPulse = 0.35f + 0.35f * sinf(flTime * 6.f);
		pDrawList->AddRect({ x - 1.f, y - 1.f }, { x + w + 1.f, y + h + 1.f },
			ColorToU32(FadeColor(Color_t(255, 255, 255, 255), flAlpha * flPulse)), flRound + 1.f, 0, 1.f);
	}
}

static void DrawShadowText(ImDrawList* pDrawList, float x, float y, Color_t tColor, Color_t tShadow, EAlign eAlign, const char* sText, float flAlpha)
{
	if (!sText || !sText[0])
		return;

	ImVec2 vPos = GetIndicatorTextPos(x, y, sText, eAlign);
	vPos = { floorf(vPos.x), floorf(vPos.y) };
	const float flOffset = std::max(roundf(H::Draw.Scale(1.f)), 1.f);
	Color_t tShadowColor = tShadow;
	tShadowColor.a = std::min<byte>(tShadowColor.a, 200);
	pDrawList->AddText({ vPos.x + flOffset, vPos.y + flOffset }, ColorToU32(FadeColor(tShadowColor, flAlpha)), sText);
	pDrawList->AddText(vPos, ColorToU32(FadeColor(tColor, flAlpha)), sText);
}

static void DrawTextPill(ImDrawList* pDrawList, float x, float y, Color_t tColor, Color_t tBackground, EAlign eAlign, const char* sText, float flAlpha)
{
	if (!sText || !sText[0])
		return;

	ImVec2 vPos = GetIndicatorTextPos(x, y, sText, eAlign);
	vPos = { floorf(vPos.x), floorf(vPos.y) };
	const ImVec2 vSize = ImGui::CalcTextSize(sText);
	const float flPadX = H::Draw.Scale(4.f), flPadY = H::Draw.Scale(1.f);

	pDrawList->AddRectFilled({ vPos.x - flPadX, vPos.y - flPadY }, { vPos.x + vSize.x + flPadX, vPos.y + vSize.y + flPadY },
		ColorToU32(FadeColor(tBackground, flAlpha)), H::Draw.Scale(3.f));
	pDrawList->AddText(vPos, ColorToU32(FadeColor(tColor, flAlpha)), sText);
}

#ifndef TEXTMODE
static IDirect3DDevice9* s_pIconDevice = nullptr;
static std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDirect3DTexture9>> s_mIconTextures;
static std::unordered_map<std::string, IMaterial*> s_mIconMaterials;

void InvalidateESPIconTextures()
{
	s_mIconTextures.clear();
	s_pIconDevice = nullptr;
}

static IDirect3DTexture9* GetIconTexture(const std::string& sTexture)
{
	IDirect3DDevice9* pDevice = F::Render.GetDevice();
	if (!pDevice)
		return nullptr;
	if (pDevice != s_pIconDevice)
		InvalidateESPIconTextures(), s_pIconDevice = pDevice;

	std::string sPath = sTexture;
	if (sPath.starts_with("materials/"))
		sPath.erase(0, 10);
	if (sPath.ends_with(".vtf"))
		sPath.resize(sPath.size() - 4);
	if (sPath.empty())
		return nullptr;

	auto [it, bNew] = s_mIconTextures.try_emplace(sPath);
	if (!bNew)
		return it->second.Get();

	IMaterial*& pMaterial = s_mIconMaterials[sPath];
	if (!pMaterial)
	{
		auto pKV = new KeyValues("UnlitGeneric");
		pKV->SetString("$basetexture", sPath.c_str());
		pKV->SetString("$translucent", "1");
		const std::string sName = std::format("unibox_esp_icon_{:08x}", FNV1A::Hash32(sPath.c_str()));
		pMaterial = I::MaterialSystem->CreateMaterial(sName.c_str(), pKV);
	}
	if (IsErrorMaterial(pMaterial))
		return nullptr;

	int nWidth = 0, nHeight = 0;
	ImageFormat eFormat = IMAGE_FORMAT_UNKNOWN;
	bool bTranslucent = false;
	if (pMaterial->GetPreviewImageProperties(&nWidth, &nHeight, &eFormat, &bTranslucent) != MATERIAL_PREVIEW_IMAGE_OK
		|| nWidth <= 0 || nHeight <= 0 || nWidth > 1024 || nHeight > 1024)
		return nullptr;

	std::vector<byte> vPixels(size_t(nWidth) * nHeight * 4);
	if (pMaterial->GetPreviewImage(vPixels.data(), nWidth, nHeight, IMAGE_FORMAT_RGBA8888) != MATERIAL_PREVIEW_IMAGE_OK)
		return nullptr;

	Microsoft::WRL::ComPtr<IDirect3DTexture9> pTexture;
	bool bDefaultPool = false;
	if (FAILED(pDevice->CreateTexture(nWidth, nHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, nullptr)))
	{
		bDefaultPool = true;
		if (FAILED(pDevice->CreateTexture(nWidth, nHeight, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTexture, nullptr)))
			return nullptr;
	}

	D3DLOCKED_RECT tRect = {};
	if (FAILED(pTexture->LockRect(0, &tRect, nullptr, bDefaultPool ? D3DLOCK_DISCARD : 0)))
		return nullptr;
	for (int y = 0; y < nHeight; ++y)
	{
		const byte* pSource = vPixels.data() + size_t(y) * nWidth * 4;
		byte* pTarget = static_cast<byte*>(tRect.pBits) + size_t(y) * tRect.Pitch;
		for (int x = 0; x < nWidth; ++x)
		{
			pTarget[x * 4 + 0] = pSource[x * 4 + 2];
			pTarget[x * 4 + 1] = pSource[x * 4 + 1];
			pTarget[x * 4 + 2] = pSource[x * 4 + 0];
			pTarget[x * 4 + 3] = pSource[x * 4 + 3];
		}
	}
	pTexture->UnlockRect(0);
	it->second = std::move(pTexture);
	return it->second.Get();
}
#endif

void CESP::Draw(ImDrawList* pDrawList)
{
	if (!pDrawList)
		return;

	const ESPDrawCache_t tCache = m_tDrawCache.Get();
	m_flAnimTime += std::clamp(ImGui::GetIO().DeltaTime, 0.f, 0.1f);

	DrawEntities(pDrawList, tCache.m_vWorld, false);
	DrawEntities(pDrawList, tCache.m_vBuildings, false);
	DrawEntities(pDrawList, tCache.m_vPlayers, true);

	CleanupAnims();
}

void CESP::DrawEntities(ImDrawList* pDrawList, const std::vector<ESPDrawEntity_t>& vEntities, bool bPlayers)
{
	if (vEntities.empty())
		return;

	const float flDelta = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 0.1f);
	const float nTall = ImGui::GetTextLineHeight() + H::Draw.Scale(2.f);

	for (const auto& tEntity : vEntities)
	{
		ESPAnim_t& tAnim = GetAnim(tEntity.m_pKey);
		tAnim.m_flAppear = Approach(tAnim.m_flAppear, 1.f, 12.f, flDelta);

		const float flEase = EaseOutCubic(tAnim.m_flAppear);
		const float flAlpha = std::clamp(tEntity.m_flAlpha, 0.f, 1.f) * flEase;
		if (flAlpha <= 0.004f)
			continue;

		const float flInset = (1.f - flEase) * H::Draw.Scale(10.f);
		const float x = roundf(tEntity.m_flX + flInset);
		const float y = roundf(tEntity.m_flY + flInset);
		const float w = std::max(roundf(tEntity.m_flW - flInset * 2.f), 1.f);
		const float h = std::max(roundf(tEntity.m_flH - flInset * 2.f), 1.f);

		const float l = x - roundf(H::Draw.Scale(6.f)), r = x + w + roundf(H::Draw.Scale(6.f)), m = floorf(x + w / 2.f);
		const float t = y - roundf(H::Draw.Scale(5.f)), b = y + h + roundf(H::Draw.Scale(5.f));
		float lOffset = 0.f, rOffset = 0.f, bOffset = 0.f, tOffset = 0.f;

		if (tEntity.m_vBones.size())
		{
			const ImU32 uBone = ColorToU32(FadeColor(tEntity.m_tColor, flAlpha * 0.9f));
			const ImU32 uBoneShadow = ColorToU32(FadeColor(Color_t(0, 0, 0, 160), flAlpha));
			const float flBoneThickness = std::max(H::Draw.Scale(1.2f), 1.f);
			for (const auto& [vFrom, vTo] : tEntity.m_vBones)
			{
				pDrawList->AddLine({ vFrom.x + 1.f, vFrom.y + 1.f }, { vTo.x + 1.f, vTo.y + 1.f }, uBoneShadow, flBoneThickness);
				pDrawList->AddLine({ vFrom.x, vFrom.y }, { vTo.x, vTo.y }, uBone, flBoneThickness);
			}
		}

		if (tEntity.m_bBox)
		{
			if (tEntity.m_iBoxStyle == 1 || tEntity.m_iBoxStyle == 2)
				DrawCornerBox(pDrawList, x, y, w, h, tEntity.m_tColor, flAlpha, flEase, tEntity.m_iBoxStyle == 1);
			else
				pDrawList->AddRect({ x + 0.5f, y + 0.5f }, { x + w + 0.5f, y + h + 0.5f },
					ColorToU32(FadeColor(tEntity.m_tColor, flAlpha)), 0.f, 0, std::max(roundf(H::Draw.Scale(1.f)), 1.f));
		}

		float flHealthForText = std::min(tEntity.m_flHealth, 1.f);

		const float flSpace = roundf(H::Draw.Scale(4.f));
		const float flThickness = std::max(roundf(H::Draw.Scale(3.f)), 2.f);
		if (tAnim.m_vBars.size() < tEntity.m_vBars.size())
			tAnim.m_vBars.resize(tEntity.m_vBars.size(), -1.f);

		for (size_t i = 0; i < tEntity.m_vBars.size(); i++)
		{
			const Bar_t& tBar = tEntity.m_vBars[i];

			float flPercent = tBar.m_flPercent;
			if (tBar.m_bSmooth)
			{
				float& flStored = tAnim.m_vBars[i];
				if (flStored < 0.f)
					flStored = flPercent;
				else
					flStored = Approach(flStored, flPercent, 11.f, flDelta);
				flPercent = flStored;

				if (tBar.m_iMode == ALIGN_LEFT)
				{
					if (tAnim.m_flHealthGhost < 0.f)
						tAnim.m_flHealthGhost = flPercent;
					tAnim.m_flHealthGhost = flPercent > tAnim.m_flHealthGhost
						? flPercent
						: Approach(tAnim.m_flHealthGhost, flPercent, 2.2f, flDelta);
					flHealthForText = std::min(flPercent, 1.f);
				}
			}

			switch (tBar.m_iMode)
			{
			case ALIGN_LEFT:
				DrawVerticalBar(pDrawList, x - flSpace - flThickness - lOffset, y, flThickness, h,
					flPercent, tBar.m_bSmooth ? tAnim.m_flHealthGhost : flPercent,
					tBar.m_tColor, tBar.m_tOverfill, tBar.m_tBackground, flAlpha);
				lOffset += flSpace + flThickness;
				break;
			case ALIGN_BOTTOM:
				DrawHorizontalBar(pDrawList, x, y + h + flSpace + bOffset, w, flThickness,
					flPercent, tBar.m_tColor, tBar.m_tBackground, flAlpha, m_flAnimTime);
				bOffset += flSpace + flThickness;
				break;
			}
		}

		for (const auto& [iMode, sText, tColor, tOutline, ucBackgroundAlpha] : tEntity.m_vText)
		{
			switch (iMode)
			{
			case ALIGN_TOP:
				if (ucBackgroundAlpha)
				{
					Color_t tBackground = tOutline;
					tBackground.a = ucBackgroundAlpha;
					DrawTextPill(pDrawList, m, t - tOffset, tColor, tBackground, ALIGN_BOTTOM, sText.c_str(), flAlpha);
				}
				else
					DrawShadowText(pDrawList, m, t - tOffset, tColor, tOutline, ALIGN_BOTTOM, sText.c_str(), flAlpha);
				tOffset += nTall;
				break;
			case ALIGN_BOTTOM:
				DrawShadowText(pDrawList, m, b + bOffset, tColor, tOutline, ALIGN_TOP, sText.c_str(), flAlpha);
				bOffset += nTall;
				break;
			case ALIGN_LEFT:
				DrawShadowText(pDrawList, l - lOffset, y - H::Draw.Scale(2.f) + h - h * flHealthForText, tColor, tOutline, ALIGN_TOPRIGHT, sText.c_str(), flAlpha);
				break;
			case ALIGN_TOPRIGHT:
				DrawShadowText(pDrawList, r, y - H::Draw.Scale(2.f) + rOffset, tColor, tOutline, ALIGN_TOPLEFT, sText.c_str(), flAlpha);
				rOffset += nTall;
				break;
			case ALIGN_BOTTOMRIGHT:
				DrawShadowText(pDrawList, r, y + h, tColor, tOutline, ALIGN_TOPLEFT, sText.c_str(), flAlpha);
				break;
			}
		}

#ifndef TEXTMODE
		if (bPlayers)
		{
			for (const auto& tBadge : tEntity.m_vBadges)
			{
				IDirect3DTexture9* pTexture = GetIconTexture(tBadge.m_sTexture);
				if (!pTexture || tBadge.m_flW <= 0.f || tBadge.m_flH <= 0.f)
					continue;
				const ImTextureID pImage = reinterpret_cast<ImTextureID>(pTexture);
				const ImVec2 vUV0(tBadge.m_flU0, tBadge.m_flV0), vUV1(tBadge.m_flU1, tBadge.m_flV1);
				const ImU32 uTint = ColorToU32(FadeColor(tBadge.m_tColor, flAlpha));
				if (tBadge.m_eType == EESPBadge::Class)
				{
					const float flSize = roundf(H::Draw.Scale(18.f));
					const float flTop = t - tOffset - flSize;
					pDrawList->AddImage(pImage, { floorf(m - flSize / 2.f), flTop },
						{ floorf(m - flSize / 2.f) + flSize, flTop + flSize }, vUV0, vUV1, uTint);
					tOffset += flSize;
				}
				else if (tBadge.m_eType == EESPBadge::Weapon)
				{
					const float flScale = H::Draw.Scale(std::min((w + 40.f) / 2.f, 80.f) / std::max(tBadge.m_flW, tBadge.m_flH * 2.f));
					const float flW = tBadge.m_flW * flScale, flH = tBadge.m_flH * flScale;
					const float flLeft = floorf(m - flW / 2.f), flTop = b + bOffset;
					pDrawList->AddImage(pImage, { flLeft, flTop }, { flLeft + flW, flTop + flH }, vUV0, vUV1, uTint);
					bOffset += flH;
				}
			}
		}
#endif
	}
}
