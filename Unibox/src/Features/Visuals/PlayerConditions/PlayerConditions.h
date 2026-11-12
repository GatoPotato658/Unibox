#pragma once
#include "../../../SDK/SDK.h"
#include "../../ImGui/IndicatorCache.h"
#include <array>
#include <cstdint>

enum class EResistTier
{
	None,
	Small,
	Full
};

enum class EDefenseBuffTier
{
	None,
	NoCritBlock,
	Normal,
	High
};

inline constexpr uint8_t PLAYER_CONDITION_BUFFS = 1 << 0;
inline constexpr uint8_t PLAYER_CONDITION_DEBUFFS = 1 << 1;
inline constexpr uint8_t PLAYER_CONDITION_FLAGS = 1 << 2;
inline constexpr uint8_t PLAYER_CONDITION_ALL = PLAYER_CONDITION_BUFFS | PLAYER_CONDITION_DEBUFFS | PLAYER_CONDITION_FLAGS;

struct PlayerConditionState_t
{
	bool m_bInvulnerable = false;
	bool m_bInvulnerableWearingOff = false;
	bool m_bMegaHeal = false;
	bool m_bPhase = false;
	bool m_bCrits = false;
	bool m_bMiniCrits = false;
	bool m_bDamageBonus = false;
	bool m_bHeal = false;
	bool m_bOverheal = false;
	EResistTier m_eBulletResist = EResistTier::None;
	EResistTier m_eBlastResist = EResistTier::None;
	EResistTier m_eFireResist = EResistTier::None;
	bool m_bOffenseBuff = false;
	EDefenseBuffTier m_eDefenseBuff = EDefenseBuffTier::None;
	bool m_bRegenBuff = false;
	bool m_bFeignDeathReady = false;
	bool m_bFeignDeath = false;
	bool m_bDisguised = false;
	bool m_bDisguising = false;
	bool m_bUndisguising = false;
	bool m_bAiming = false;
	bool m_bZoomed = false;
	bool m_bBurning = false;
	bool m_bBurningPyro = false;
	bool m_bBleeding = false;
	bool m_bGrappleBleeding = false;
	bool m_bMarked = false;
	bool m_bUrine = false;
	bool m_bMilk = false;
	bool m_bSlowStunned = false;
	bool m_bStunned = false;
	bool m_bMvmStunned = false;
	bool m_bShieldCharge = false;
	bool m_bTaunting = false;
	bool m_bPowerupDominant = false;
	bool m_bKingBuffed = false;
	bool m_bRuneStrength = false;
	bool m_bRuneHaste = false;
	bool m_bRuneRegen = false;
	bool m_bRuneResist = false;
	bool m_bRuneVampire = false;
	bool m_bRuneReflect = false;
	bool m_bRunePrecision = false;
	bool m_bRuneAgility = false;
	bool m_bRuneKnockout = false;
	bool m_bRuneImbalance = false;
	bool m_bRuneKing = false;
	bool m_bRunePlague = false;
	bool m_bRuneSupernova = false;
	std::array<uint8_t, MAX_WEAPONS> m_aSpells = {};
	uint8_t m_uSpellCount = 0;
};

class CPlayerConditions
{
private:
	CIndicatorCache<std::vector<std::string>> m_tDrawCache = {};

public:
	static PlayerConditionState_t Evaluate(CTFPlayer* pEntity, uint8_t uGroups = PLAYER_CONDITION_ALL);
	static const char* GetSpellName(int iSpell);
	std::vector<std::string> Get(CTFPlayer* pEntity);
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();
};

ADD_FEATURE(CPlayerConditions, PlayerConditions);
