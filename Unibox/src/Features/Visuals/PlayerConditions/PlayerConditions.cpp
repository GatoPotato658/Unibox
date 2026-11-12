#include "PlayerConditions.h"

#include "../../ImGui/IndicatorPanel.h"

PlayerConditionState_t CPlayerConditions::Evaluate(CTFPlayer* pEntity, uint8_t uGroups)
{
	PlayerConditionState_t tState = {};
	if (uGroups & PLAYER_CONDITION_BUFFS)
	{
		tState.m_bInvulnerable =
			pEntity->InCond(TF_COND_INVULNERABLE) ||
			pEntity->InCond(TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED) ||
			pEntity->InCond(TF_COND_INVULNERABLE_USER_BUFF) ||
			pEntity->InCond(TF_COND_INVULNERABLE_CARD_EFFECT);
		tState.m_bInvulnerableWearingOff = tState.m_bInvulnerable && pEntity->InCond(TF_COND_INVULNERABLE_WEARINGOFF);
		tState.m_bMegaHeal = pEntity->InCond(TF_COND_MEGAHEAL);
		tState.m_bPhase = pEntity->InCond(TF_COND_PHASE);

		tState.m_bCrits = pEntity->IsCritBoosted();
		tState.m_bMiniCrits = pEntity->IsMiniCritBoosted();
		if (auto pWeapon = pEntity->m_hActiveWeapon()->As<CTFWeaponBase>())
		{
			if (tState.m_bMiniCrits && SDK::AttribHookValue(0, "minicrits_become_crits", pWeapon)
				|| SDK::AttribHookValue(0, "crit_while_airborne", pWeapon) && pEntity->InCond(TF_COND_BLASTJUMPING))
				tState.m_bCrits = true, tState.m_bMiniCrits = false;
			if (tState.m_bCrits && SDK::AttribHookValue(0, "crits_become_minicrits", pWeapon))
				tState.m_bCrits = false, tState.m_bMiniCrits = true;
		}

		tState.m_bDamageBonus = pEntity->InCond(TF_COND_TMPDAMAGEBONUS);
		tState.m_bHeal =
			pEntity->InCond(TF_COND_RADIUSHEAL) ||
			pEntity->InCond(TF_COND_HEALTH_BUFF) ||
			pEntity->InCond(TF_COND_RADIUSHEAL_ON_DAMAGE) ||
			pEntity->InCond(TF_COND_HALLOWEEN_QUICK_HEAL) ||
			pEntity->InCond(TF_COND_HALLOWEEN_HELL_HEAL);
		tState.m_bOverheal = pEntity->InCond(TF_COND_HEALTH_OVERHEALED);

		tState.m_eBulletResist =
			pEntity->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) || pEntity->InCond(TF_COND_BULLET_IMMUNE) ? EResistTier::Full :
			pEntity->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST) ? EResistTier::Small : EResistTier::None;
		tState.m_eBlastResist =
			pEntity->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) || pEntity->InCond(TF_COND_BLAST_IMMUNE) ? EResistTier::Full :
			pEntity->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST) ? EResistTier::Small : EResistTier::None;
		tState.m_eFireResist =
			pEntity->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) || pEntity->InCond(TF_COND_FIRE_IMMUNE) ? EResistTier::Full :
			pEntity->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST) ? EResistTier::Small : EResistTier::None;

		tState.m_bOffenseBuff = pEntity->InCond(TF_COND_OFFENSEBUFF);
		tState.m_eDefenseBuff =
			pEntity->InCond(TF_COND_DEFENSEBUFF_HIGH) ? EDefenseBuffTier::High :
			pEntity->InCond(TF_COND_DEFENSEBUFF) ? EDefenseBuffTier::Normal :
			pEntity->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK) ? EDefenseBuffTier::NoCritBlock : EDefenseBuffTier::None;
		tState.m_bRegenBuff = pEntity->InCond(TF_COND_REGENONDAMAGEBUFF);
		tState.m_bPowerupDominant = pEntity->InCond(TF_COND_POWERUPMODE_DOMINANT);
		tState.m_bKingBuffed = pEntity->InCond(TF_COND_KING_BUFFED);
		tState.m_bRuneStrength = pEntity->InCond(TF_COND_RUNE_STRENGTH);
		tState.m_bRuneHaste = pEntity->InCond(TF_COND_RUNE_HASTE);
		tState.m_bRuneRegen = pEntity->InCond(TF_COND_RUNE_REGEN);
		tState.m_bRuneResist = pEntity->InCond(TF_COND_RUNE_RESIST);
		tState.m_bRuneVampire = pEntity->InCond(TF_COND_RUNE_VAMPIRE);
		tState.m_bRuneReflect = pEntity->InCond(TF_COND_RUNE_REFLECT);
		tState.m_bRunePrecision = pEntity->InCond(TF_COND_RUNE_PRECISION);
		tState.m_bRuneAgility = pEntity->InCond(TF_COND_RUNE_AGILITY);
		tState.m_bRuneKnockout = pEntity->InCond(TF_COND_RUNE_KNOCKOUT);
		tState.m_bRuneImbalance = pEntity->InCond(TF_COND_RUNE_IMBALANCE);
		tState.m_bRuneKing = pEntity->InCond(TF_COND_RUNE_KING);
		tState.m_bRunePlague = pEntity->InCond(TF_COND_RUNE_PLAGUE);
		tState.m_bRuneSupernova = pEntity->InCond(TF_COND_RUNE_SUPERNOVA);

		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pSpellBook = pEntity->GetWeaponFromSlot(i)->As<CTFSpellBook>();
			if (!pSpellBook || pSpellBook->GetWeaponID() != TF_WEAPON_SPELLBOOK || !pSpellBook->m_iSpellCharges())
				continue;

			const int iSpell = pSpellBook->m_iSelectedSpellIndex();
			if (iSpell >= 0 && iSpell < 16 && tState.m_uSpellCount < tState.m_aSpells.size())
				tState.m_aSpells[tState.m_uSpellCount++] = uint8_t(iSpell);
		}
	}

	if (uGroups & PLAYER_CONDITION_DEBUFFS)
	{
		tState.m_bBurning = pEntity->InCond(TF_COND_BURNING);
		tState.m_bBurningPyro = pEntity->InCond(TF_COND_BURNING_PYRO);
		tState.m_bBleeding = pEntity->InCond(TF_COND_BLEEDING);
		tState.m_bGrappleBleeding = pEntity->InCond(TF_COND_GRAPPLINGHOOK_BLEEDING);
		tState.m_bMarked =
			pEntity->InCond(TF_COND_MARKEDFORDEATH) ||
			pEntity->InCond(TF_COND_MARKEDFORDEATH_SILENT) ||
			pEntity->InCond(TF_COND_PASSTIME_PENALTY_DEBUFF);
		tState.m_bUrine = pEntity->InCond(TF_COND_URINE);
		tState.m_bMilk = pEntity->InCond(TF_COND_MAD_MILK);
		tState.m_bSlowStunned = pEntity->InCond(TF_COND_STUNNED) && !(pEntity->m_iStunFlags() & (TF_STUN_CONTROLS | TF_STUN_LOSER_STATE));
		tState.m_bStunned = pEntity->InCond(TF_COND_STUNNED);
		tState.m_bMvmStunned = pEntity->InCond(TF_COND_MVM_BOT_STUN_RADIOWAVE);
	}

	if (uGroups & PLAYER_CONDITION_FLAGS)
	{
		tState.m_bFeignDeathReady = pEntity->m_bFeignDeathReady();
		tState.m_bFeignDeath = pEntity->InCond(TF_COND_FEIGN_DEATH);
		tState.m_bDisguised = pEntity->InCond(TF_COND_DISGUISED);
		tState.m_bDisguising = pEntity->InCond(TF_COND_DISGUISING);
		tState.m_bUndisguising = pEntity->InCond(TF_COND_DISGUISE_WEARINGOFF);
		tState.m_bAiming = pEntity->InCond(TF_COND_AIMING);
		tState.m_bZoomed = pEntity->InCond(TF_COND_ZOOMED);
		tState.m_bShieldCharge = pEntity->InCond(TF_COND_SHIELD_CHARGE);
		tState.m_bTaunting = pEntity->InCond(TF_COND_TAUNTING);
	}
	return tState;
}

const char* CPlayerConditions::GetSpellName(int iSpell)
{
	switch (iSpell)
	{
	case 0: return "Fireball";
	case 1: return "Bats";
	case 2: return "Heal";
	case 3: return "Pumpkins";
	case 4: return "Jump";
	case 5: return "Stealth";
	case 6: return "Teleport";
	case 7: return "Lightning";
	case 8: return "Minify";
	case 9: return "Meteors";
	case 10: return "Monoculus";
	case 11: return "Skeletons";
	case 12: return "Glove";
	case 13: return "Parachute";
	case 14: return "Heal";
	case 15: return "Bomb";
	default: return nullptr;
	}
}

std::vector<std::string> CPlayerConditions::Get(CTFPlayer* pEntity)
{
	std::vector<std::string> vConditions = {};
	const PlayerConditionState_t tState = Evaluate(pEntity);

	if (pEntity->entindex() != I::EngineClient->GetLocalPlayer())
	{
		if (auto pWeapon = pEntity->m_hActiveWeapon()->As<CTFWeaponBase>())
		{
			bool bFlip = pEntity->m_bFlipViewModels();
			vConditions.emplace_back(std::format("{}{}{}", bFlip ? "< " : "", SDK::ConvertWideToUTF8(pWeapon->GetWeaponName()), bFlip ? "" : " >"));
		}
	}

	if (tState.m_bInvulnerable)
	{
		if (tState.m_bInvulnerableWearingOff)
			vConditions.emplace_back("Invulnerable-");
		else
			vConditions.emplace_back("Invulnerable");
	}

	if (tState.m_bCrits)
		vConditions.emplace_back("Crits");
	else if (tState.m_bMiniCrits)
		vConditions.emplace_back("Mini crits");

	if (tState.m_bDamageBonus)
		vConditions.emplace_back("Damage bonus");

	if (tState.m_bMegaHeal)
		vConditions.emplace_back("Megaheal");
	else if (tState.m_bHeal)
		vConditions.emplace_back("Heal");

	if (tState.m_bOverheal)
		vConditions.emplace_back("Overheal");



	if (tState.m_eBulletResist == EResistTier::Full)
		vConditions.emplace_back("Bullet+");
	else if (tState.m_eBulletResist == EResistTier::Small)
		vConditions.emplace_back("Bullet");

	if (tState.m_eBlastResist == EResistTier::Full)
		vConditions.emplace_back("Blast+");
	else if (tState.m_eBlastResist == EResistTier::Small)
		vConditions.emplace_back("Blast");

	if (tState.m_eFireResist == EResistTier::Full)
		vConditions.emplace_back("Fire+");
	else if (tState.m_eFireResist == EResistTier::Small)
		vConditions.emplace_back("Fire");

	if (tState.m_bOffenseBuff)
		vConditions.emplace_back("Banner");

	if (tState.m_eDefenseBuff == EDefenseBuffTier::High)
		vConditions.emplace_back("Battalions+");
	else if (tState.m_eDefenseBuff == EDefenseBuffTier::Normal)
		vConditions.emplace_back("Battalions");
	else if (tState.m_eDefenseBuff == EDefenseBuffTier::NoCritBlock)
		vConditions.emplace_back("Battalions-");

	if (tState.m_bRegenBuff)
		vConditions.emplace_back("Conch");



	if (tState.m_bFeignDeathReady)
		vConditions.emplace_back("Deadringer");

	if (tState.m_bFeignDeath)
		vConditions.emplace_back("Feign");

	if (pEntity->InCond(TF_COND_STEALTHED))
		vConditions.emplace_back("Cloak");

	if (pEntity->InCond(TF_COND_STEALTHED_USER_BUFF))
		vConditions.emplace_back("Stealth");

	if (pEntity->InCond(TF_COND_STEALTHED_USER_BUFF_FADING))
		vConditions.emplace_back("Stealth+");

	if (tState.m_bDisguised)
		vConditions.emplace_back("Disguise");
	else if (tState.m_bDisguising)
		vConditions.emplace_back("Disguising");
	else if (tState.m_bUndisguising)
		vConditions.emplace_back("Undisguising");

	if (pEntity->InCond(TF_COND_STEALTHED_BLINK))
		vConditions.emplace_back("Blink");



	if (pEntity->InCond(TF_COND_SPEED_BOOST) ||
		pEntity->InCond(TF_COND_HALLOWEEN_SPEED_BOOST))
		vConditions.emplace_back("Speed boost");

	if (pEntity->InCond(TF_COND_SODAPOPPER_HYPE))
		vConditions.emplace_back("Hype");

	if (pEntity->InCond(TF_COND_SNIPERCHARGE_RAGE_BUFF))
		vConditions.emplace_back("Focus");

	if (tState.m_bAiming)
		vConditions.emplace_back("Aiming");

	if (tState.m_bZoomed)
		vConditions.emplace_back("Zoom");

	if (tState.m_bShieldCharge)
		vConditions.emplace_back("Charging");

	if (tState.m_bPhase)
		vConditions.emplace_back("Bonk");

	if (pEntity->InCond(TF_COND_AFTERBURN_IMMUNE))
		vConditions.emplace_back("No afterburn");

	if (pEntity->InCond(TF_COND_BLASTJUMPING))
		vConditions.emplace_back("Blastjump");

	if (pEntity->InCond(TF_COND_ROCKETPACK))
		vConditions.emplace_back("Rocketpack");

	if (pEntity->InCond(TF_COND_PARACHUTE_ACTIVE) ||
		pEntity->InCond(TF_COND_PARACHUTE_DEPLOYED))
		vConditions.emplace_back("Parachute");

	if (pEntity->InCond(TF_COND_OBSCURED_SMOKE))
		vConditions.emplace_back("Dodge");



	if (tState.m_bSlowStunned)
		vConditions.emplace_back("Slowed");
	else if (tState.m_bStunned || tState.m_bMvmStunned)
		vConditions.emplace_back("Stun");

	if (tState.m_bMarked)
		vConditions.emplace_back("Marked for death");

	if (tState.m_bUrine)
		vConditions.emplace_back("Jarate");

	if (tState.m_bMilk)
		vConditions.emplace_back("Milk");

	if (pEntity->InCond(TF_COND_GAS))
		vConditions.emplace_back("Gas");

	if (tState.m_bBurning || tState.m_bBurningPyro)
		vConditions.emplace_back("Burn");

	if (tState.m_bBleeding || tState.m_bGrappleBleeding)
		vConditions.emplace_back("Bleed");

	if (pEntity->InCond(TF_COND_KNOCKED_INTO_AIR))
		vConditions.emplace_back("Airblast");

	if (pEntity->InCond(TF_COND_AIR_CURRENT))
		vConditions.emplace_back("Air");

	if (pEntity->InCond(TF_COND_LOST_FOOTING))
		vConditions.emplace_back("Slide");

	if (pEntity->InCond(TF_COND_HEALING_DEBUFF))
		vConditions.emplace_back("Heal debuff");

	if (pEntity->InCond(TF_COND_CANNOT_SWITCH_FROM_MELEE) ||
		pEntity->InCond(TF_COND_MELEE_ONLY))
		vConditions.emplace_back("Only melee");



	if (pEntity->InCond(TF_COND_HALLOWEEN_GIANT))
		vConditions.emplace_back("Giant");

	if (pEntity->InCond(TF_COND_HALLOWEEN_TINY))
		vConditions.emplace_back("Tiny");

	if (pEntity->InCond(TF_COND_HALLOWEEN_BOMB_HEAD))
		vConditions.emplace_back("Bomb");

	if (pEntity->InCond(TF_COND_BALLOON_HEAD))
		vConditions.emplace_back("Balloon");

	if (pEntity->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
		vConditions.emplace_back("Ghost");

	if (pEntity->InCond(TF_COND_HALLOWEEN_KART))
		vConditions.emplace_back("Kart");

	if (pEntity->InCond(TF_COND_HALLOWEEN_KART_DASH))
		vConditions.emplace_back("Dash");

	if (pEntity->InCond(TF_COND_SWIMMING_CURSE) ||
		pEntity->InCond(TF_COND_SWIMMING_NO_EFFECTS))
		vConditions.emplace_back("Swim");

	if (pEntity->InCond(TF_COND_HALLOWEEN_KART_CAGE))
		vConditions.emplace_back("Cage");



//	if (pEntity->InCond(TF_COND_HASRUNE))
//		vConditions.emplace_back("Rune");

	if (tState.m_bPowerupDominant)
		vConditions.emplace_back("Dominant");

	if (tState.m_bRuneStrength)
		vConditions.emplace_back("Strength");

	if (tState.m_bRuneHaste)
		vConditions.emplace_back("Haste");

	if (tState.m_bRuneRegen)
		vConditions.emplace_back("Regen");

	if (tState.m_bRuneResist)
		vConditions.emplace_back("Resist");

	if (tState.m_bRuneVampire)
		vConditions.emplace_back("Vampire");

	if (tState.m_bRuneReflect)
		vConditions.emplace_back("Reflect");

	if (tState.m_bRunePrecision)
		vConditions.emplace_back("Precision");

	if (tState.m_bRuneAgility)
		vConditions.emplace_back("Agility");

	if (tState.m_bRuneKnockout)
		vConditions.emplace_back("Knockout");

	if (tState.m_bRuneImbalance)
		vConditions.emplace_back("Imbalance");

	if (tState.m_bRuneKing)
		vConditions.emplace_back("King");

	if (tState.m_bRunePlague)
		vConditions.emplace_back("Plague");

	if (tState.m_bRuneSupernova)
		vConditions.emplace_back("Supernova");

	if (pEntity->InCond(TF_COND_PLAGUE))
		vConditions.emplace_back("Plagued");

	if (tState.m_bKingBuffed)
		vConditions.emplace_back("King buff");

	for (uint8_t i = 0; i < tState.m_uSpellCount; i++)
	{
		const char* pSpell = GetSpellName(tState.m_aSpells[i]);
		if (!pSpell)
			continue;
		vConditions.emplace_back(pSpell);
	}



	if (pEntity->InCond(TF_COND_HALLOWEEN_IN_HELL))
		vConditions.emplace_back("Hell");

	if (pEntity->InCond(TF_COND_PURGATORY))
		vConditions.emplace_back("Purgatory");

	if (pEntity->InCond(TF_COND_PASSTIME_INTERCEPTION))
		vConditions.emplace_back("Interception");

	if (pEntity->InCond(TF_COND_TEAM_GLOWS))
		vConditions.emplace_back("Team glows");



	if (pEntity->InCond(TF_COND_PREVENT_DEATH))
		vConditions.emplace_back("Prevent death");

	if (pEntity->InCond(TF_COND_GRAPPLINGHOOK))
		vConditions.emplace_back("Grapple");

	if (pEntity->InCond(TF_COND_GRAPPLINGHOOK_SAFEFALL))
		vConditions.emplace_back("Safefall");

	if (pEntity->InCond(TF_COND_GRAPPLINGHOOK_LATCHED))
		vConditions.emplace_back("Latched");

	if (pEntity->InCond(TF_COND_GRAPPLED_TO_PLAYER))
		vConditions.emplace_back("To player");

	if (pEntity->InCond(TF_COND_GRAPPLED_BY_PLAYER))
		vConditions.emplace_back("By player");

	if (pEntity->InCond(TF_COND_TELEPORTED))
		vConditions.emplace_back("Teleported");

	if (pEntity->InCond(TF_COND_SELECTED_TO_TELEPORT))
		vConditions.emplace_back("Teleporting");

	if (pEntity->InCond(TF_COND_MEDIGUN_DEBUFF))
		vConditions.emplace_back("Medigun debuff");

	if (pEntity->InCond(TF_COND_SAPPED))
		vConditions.emplace_back("Sapped");

	if (pEntity->InCond(TF_COND_DISGUISED_AS_DISPENSER))
		vConditions.emplace_back("Dispenser");

	if (tState.m_bTaunting)
		vConditions.emplace_back("Taunt");

	if (pEntity->InCond(TF_COND_HALLOWEEN_THRILLER))
		vConditions.emplace_back("Thriller");

	if (pEntity->InCond(TF_COND_FREEZE_INPUT))
		vConditions.emplace_back("Freeze input");

	if (pEntity->InCond(TF_COND_REPROGRAMMED))
		vConditions.emplace_back("Reprogrammed");

	return vConditions;
}

void CPlayerConditions::CacheDrawInfo(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Conditions) || !pLocal)
	{
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
	if (!pTarget || !pTarget->IsPlayer() || !pTarget->IsAlive())
	{
		m_tDrawCache.Set({});
		return;
	}

	m_tDrawCache.Set(Get(pTarget));
}

void CPlayerConditions::Draw()
{
	static IndicatorAnim_t tAnim;
	static std::shared_ptr<const std::vector<std::string>> s_pLastValidConditions;

	const auto pConditions = m_tDrawCache.Get();
	if (!pConditions->empty())
		s_pLastValidConditions = pConditions;

	const bool bEnabled = (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Conditions) != 0;
	const bool bVisible = bEnabled && I::EngineClient->IsInGame() && !pConditions->empty();

	bool bOpening = true;
	const float flAnim = tAnim.Update(bVisible, bOpening);
	if (!tAnim.ShouldDraw(bVisible))
		return;

	const auto& vDraw = !pConditions->empty() ? *pConditions : *s_pLastValidConditions;

	int x = Vars::Menu::ConditionsDisplay.Value.x;
	int y = Vars::Menu::ConditionsDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.IndicatorScale(1);
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.IndicatorScale(50, Scale_Round))
	{
		x -= H::Draw.IndicatorScale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.IndicatorScale(50, Scale_Round))
	{
		x += H::Draw.IndicatorScale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const int iVtxStart = pDrawList->VtxBuffer.Size;
	const int iCmdStart = pDrawList->CmdBuffer.Size;

	int iOffset = 0;
	for (const std::string& sCondition : vDraw)
	{
		DrawIndicatorText(pDrawList, x, y + iOffset, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, sCondition);
		iOffset += nTall;
	}

	TransformIndicatorVertices(pDrawList, iVtxStart, pDrawList->VtxBuffer.Size, iCmdStart, pDrawList->CmdBuffer.Size, flAnim, bOpening, ImGui::GetIO().DisplaySize);
}
