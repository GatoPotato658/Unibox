#include "CritHack.h"

#include "../Ticks/Ticks.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../ImGui/IndicatorPanel.h"

#define WEAPON_RANDOM_RANGE				10000
#define TF_DAMAGE_CRIT_MULTIPLIER		3.0f
#define TF_DAMAGE_CRIT_CHANCE			0.02f
#define TF_DAMAGE_CRIT_CHANCE_RAPID		0.02f
#define TF_DAMAGE_CRIT_CHANCE_MELEE		0.15f
#define TF_DAMAGE_CRIT_DURATION_RAPID	2.0f

#define STATS_SEND_FREQUENCY 1.f

#define SEED_ATTEMPTS 4096
#define BUCKET_ATTEMPTS 1000

//#define SERVER_CRIT_DATA

int CCritHack::GetCritCommand(CTFWeaponBase* pWeapon, int iCommandNumber, bool bCrit, bool bSafe)
{
	for (int i = iCommandNumber; i < iCommandNumber + SEED_ATTEMPTS; i++)
	{
		if (IsCritCommand(i, pWeapon, bCrit, bSafe))
			return i;
	}
	return 0;
}

bool CCritHack::IsCritCommand(int iCommandNumber, CTFWeaponBase* pWeapon, bool bCrit, bool bSafe)
{
	int iSeed = CommandToSeed(iCommandNumber);
	return IsCritSeed(iSeed, pWeapon, bCrit, bSafe);
}

bool CCritHack::IsCritSeed(int iSeed, CTFWeaponBase* pWeapon, bool bCrit, bool bSafe)
{
	if (iSeed == pWeapon->m_iCurrentSeed())
		return false;

	SDK::RandomSeed(iSeed);
	int iRandom = SDK::RandomInt(0, WEAPON_RANDOM_RANGE - 1);

	if (bSafe)
	{
		int iLower, iUpper;
		if (m_bMelee)
			iLower = 1500, iUpper = 6000;
		else
			iLower = 100, iUpper = 800;
		iLower *= m_flMultCritChance, iUpper *= m_flMultCritChance;

		if (bCrit ? iLower >= 0 : iUpper < WEAPON_RANDOM_RANGE)
			return bCrit ? iRandom < iLower : !(iRandom < iUpper);
	}

	int iRange = m_flCritChance * WEAPON_RANDOM_RANGE;
	return bCrit ? iRandom < iRange : !(iRandom < iRange);
}

int CCritHack::CommandToSeed(int iCommandNumber)
{
	int iSeed = MD5_PseudoRandom(iCommandNumber) & std::numeric_limits<int>::max();
	int iMask = m_bMelee
		? m_iEntIndex << 16 | I::EngineClient->GetLocalPlayer() << 8
		: m_iEntIndex << 8 | I::EngineClient->GetLocalPlayer();
	return iSeed ^ iMask;
}



void CCritHack::UpdateWeaponInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_iEntIndex = pWeapon->entindex();
	m_bMelee = pWeapon->GetSlot() == SLOT_MELEE;
	if (m_bMelee)
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_MELEE * pLocal->GetCritMult();
	else if (pWeapon->IsRapidFire())
	{
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_RAPID * pLocal->GetCritMult();
		float flNonCritDuration = (TF_DAMAGE_CRIT_DURATION_RAPID / m_flCritChance) - TF_DAMAGE_CRIT_DURATION_RAPID;
		m_flCritChance = 1.f / flNonCritDuration;
	}
	else
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE * pLocal->GetCritMult();
	m_flMultCritChance = SDK::AttribHookValue(1.f, "mult_crit_chance", pWeapon);
	m_flCritChance *= m_flMultCritChance;



	static CTFWeaponBase* pStaticWeapon = nullptr;
	const CTFWeaponBase* pOldWeapon = pStaticWeapon;
	pStaticWeapon = pWeapon;

	static float flStaticBucket = 0.f;
	const float flLastBucket = flStaticBucket;
	const float flBucket = flStaticBucket = pWeapon->m_flCritTokenBucket();

	static int iStaticCritChecks = 0.f;
	const int iLastCritChecks = iStaticCritChecks;
	const int iCritChecks = iStaticCritChecks = pWeapon->m_nCritChecks();

	static int iStaticCritSeedRequests = 0.f;
	const int iLastCritSeedRequests = iStaticCritSeedRequests;
	const int iCritSeedRequests = iStaticCritSeedRequests = pWeapon->m_nCritSeedRequests();

	if (pWeapon == pOldWeapon && flBucket == flLastBucket && iCritChecks == iLastCritChecks && iCritSeedRequests == iLastCritSeedRequests)
		return;

	static auto tf_weapon_criticals_bucket_cap = H::ConVars.FindVar("tf_weapon_criticals_bucket_cap");
	const float flBucketCap = tf_weapon_criticals_bucket_cap->GetFloat();
	bool bRapidFire = pWeapon->IsRapidFire();
	float flFireRate = pWeapon->GetFireRate();

	float flDamage = pWeapon->GetDamage();
	int nProjectilesPerShot = pWeapon->GetBulletsPerShot(false);
	if (!m_bMelee && nProjectilesPerShot > 0)
		nProjectilesPerShot = SDK::AttribHookValue(nProjectilesPerShot, "mult_bullets_per_shot", pWeapon);
	else
		nProjectilesPerShot = 1;
	float flBaseDamage = flDamage *= nProjectilesPerShot;
	if (bRapidFire)
	{
		flDamage *= TF_DAMAGE_CRIT_DURATION_RAPID / flFireRate;
		if (flDamage * TF_DAMAGE_CRIT_MULTIPLIER > flBucketCap)
			flDamage = flBucketCap / TF_DAMAGE_CRIT_MULTIPLIER;
	}

	float flMult = m_bMelee ? 0.5f : Math::RemapVal(float(iCritSeedRequests + 1) / (iCritChecks + 1), 0.1f, 1.f, 1.f, 3.f);
	float flCost = flDamage * TF_DAMAGE_CRIT_MULTIPLIER;

	int iPotentialCrits = (std::max(flBucketCap, flBucket) - flBaseDamage) / (TF_DAMAGE_CRIT_MULTIPLIER * flDamage / (m_bMelee ? 2 : 1) - flBaseDamage);
	int iAvailableCrits = 0;
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		for (int i = 0; i < BUCKET_ATTEMPTS; i++)
		{
			iTestShots++; iTestCrits++;

			float flTestMult = m_bMelee ? 0.5f : Math::RemapVal(float(iTestCrits) / iTestShots, 0.1f, 1.f, 1.f, 3.f);
			if (flTestBucket < flBucketCap)
				flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);
			flTestBucket -= flCost * flTestMult;
			if (flTestBucket < 0.f)
				break;

			iAvailableCrits++;
		}
	}

	int iNextCrit = 0;
	if (iAvailableCrits != iPotentialCrits)
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		float flTickBase = I::GlobalVars->curtime;
		float flLastRapidFireCritCheckTime = pWeapon->m_flLastRapidFireCritCheckTime();
		for (int i = 0; i < BUCKET_ATTEMPTS; i++)
		{
			int iCrits = 0;
			{
				int iTestShots2 = iTestShots, iTestCrits2 = iTestCrits;
				float flTestBucket2 = flTestBucket;
				for (int j = 0; j < BUCKET_ATTEMPTS; j++)
				{
					iTestShots2++; iTestCrits2++;

					float flTestMult = m_bMelee ? 0.5f : Math::RemapVal(float(iTestCrits2) / iTestShots2, 0.1f, 1.f, 1.f, 3.f);
					if (flTestBucket2 < flBucketCap)
						flTestBucket2 = std::min(flTestBucket2 + flBaseDamage, flBucketCap);
					flTestBucket2 -= flCost * flTestMult;
					if (flTestBucket2 < 0.f)
						break;

					iCrits++;
				}
			}
			if (iAvailableCrits < iCrits)
				break;

			if (!bRapidFire)
				iTestShots++;
			else
			{
				flTickBase += std::ceilf(flFireRate / TICK_INTERVAL) * TICK_INTERVAL;
				if (flTickBase >= flLastRapidFireCritCheckTime + 1.f || !i && flTestBucket == flBucketCap)
				{
					iTestShots++;
					flLastRapidFireCritCheckTime = flTickBase;
				}
			}

			if (flTestBucket < flBucketCap)
				flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);

			iNextCrit++;
		}
	}

	m_flDamage = flBaseDamage;
	m_flCost = flCost * flMult;
	m_iPotentialCrits = iPotentialCrits;
	m_iAvailableCrits = iAvailableCrits;
	m_iNextCrit = iNextCrit;
}

void CCritHack::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	UpdateWeaponInfo(pLocal, pWeapon);

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;
	if (!m_bMelee)
	{
		const float flNormalizedDamage = m_iCritDamage / TF_DAMAGE_CRIT_MULTIPLIER;
		float flCritChance = m_flCritChance + 0.1f;
		if (m_iRangedDamage && m_iCritDamage)
		{
			const float flObservedCritChance = flNormalizedDamage / (flNormalizedDamage + m_iRangedDamage - m_iCritDamage);
			m_bCritBanned = flObservedCritChance > flCritChance;
		}

		if (m_bCritBanned)
			m_flDamageTilFlip = flNormalizedDamage / flCritChance + flNormalizedDamage * 2 - m_iRangedDamage;
		else
			m_flDamageTilFlip = TF_DAMAGE_CRIT_MULTIPLIER * (flNormalizedDamage - flCritChance * (flNormalizedDamage + m_iRangedDamage - m_iCritDamage)) / (flCritChance - 1);
	}

	if (auto pResource = H::Entities.GetResource())
	{
		m_iResourceDamage = pResource->m_iDamage(I::EngineClient->GetLocalPlayer());
		/* // more of a proof of concept for resyncing crit damage
		if (m_flLastDamageTime < I::GlobalVars->curtime + STATS_SEND_FREQUENCY * 2)
		{	// attempt to resync damages
			m_iRangedDamage = m_iResourceDamage - m_iMeleeDamage;

			float flObservedCritChance = pWeapon->m_flObservedCritChance();
			m_iCritDamage = (TF_DAMAGE_CRIT_MULTIPLIER * flObservedCritChance * m_iResourceDamage) / (1 + 2 * flObservedCritChance);
			SDK::Output("Info", std::format("{}, {}", m_iRangedDamage, m_iCritDamage).c_str());
		}
		*/
		m_iDesyncDamage = m_iRangedDamage + m_iMeleeDamage - m_iResourceDamage;
	}
}

bool CCritHack::WeaponCanCrit(CTFWeaponBase* pWeapon, bool bWeaponOnly)
{
	if (!bWeaponOnly && !pWeapon->AreRandomCritsEnabled() || SDK::AttribHookValue(1.f, "mult_crit_chance", pWeapon) <= 0.f)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_PDA_SPY_BUILD:
	case TF_WEAPON_BUILDER:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_FLAME_BALL:
	case TF_WEAPON_ROCKETPACK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
	case TF_WEAPON_KNIFE:
	case TF_WEAPON_PASSTIME_GUN:
		return false;
	}

	return true;
}

void CCritHack::Reset()
{
	m_iCritDamage = 0;
	m_iRangedDamage = 0;
	m_iMeleeDamage = 0;
	m_iResourceDamage = 0;
	m_iDesyncDamage = 0;

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;

	m_mHealthHistory.clear();

	//m_flLastDamageTime = 0.f;
}



int CCritHack::GetCritRequest(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	bool bCanCrit = m_iAvailableCrits > 0 && !m_bCritBanned;

	bool bForce = bCanCrit && Vars::CritHack::ForceCrits.Value;
	if (bCanCrit && m_bMelee && Vars::CritHack::AlwaysMeleeCrit.Value
		&& (Vars::Aimbot::General::AutoShoot.Value ? pCmd->buttons & IN_ATTACK && !(G::OriginalCmd.buttons & IN_ATTACK) : Vars::Aimbot::General::AimType.Value)
		&& G::AimTarget.m_iEntIndex)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(G::AimTarget.m_iEntIndex)->As<CBaseEntity>();
		if (pEntity && pEntity->IsPlayer() && (SDK::FriendlyFire() || pLocal->m_iTeamNum() != pEntity->m_iTeamNum()))
			bForce = true;
	}

	bool bSkip = Vars::CritHack::AvoidRandomCrits.Value;
	bool bDesync = CommandToSeed(pCmd->command_number) == pWeapon->m_iCurrentSeed();

	return bForce ? CritRequestEnum::Crit : bSkip || bDesync ? CritRequestEnum::Skip : CritRequestEnum::Any;
}

void CCritHack::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost())
		return;

	UpdateInfo(pLocal, pWeapon);
	if (pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !WeaponCanCrit(pWeapon))
		return;

	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && pCmd->buttons & IN_ATTACK)
		pCmd->buttons &= ~IN_ATTACK2;

	bool bAttacking = G::Attacking /*== 1*/ || F::Ticks.m_bDoubletap || F::Ticks.m_bSpeedhack;
	if (m_bMelee)
	{
		bAttacking = G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK;
		if (!bAttacking && pWeapon->GetWeaponID() == TF_WEAPON_FISTS)
			bAttacking = G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK2;
	}
	else if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && !(G::LastUserCmd->buttons & IN_ATTACK))
		bAttacking = false;
	else if (!bAttacking)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			if (pWeapon->IsInReload() && G::CanPrimaryAttack && SDK::AttribHookValue(0, "can_overload", pWeapon))
			{
				int iClip1 = pWeapon->m_iClip1();
				if (pWeapon->m_bRemoveable() && iClip1 > 0)
					bAttacking = true;
				else if (iClip1 >= pWeapon->GetMaxClip1() || iClip1 > 0 && pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType()) == 0)
					bAttacking = true;
			}
		}
	}
	if (!bAttacking || pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
		return;

	int iRequest = GetCritRequest(pCmd, pLocal, pWeapon);
	if (iRequest == CritRequestEnum::Any)
		return;

	if (!F::AntiCheatCompatibility.Active())
	{
		if (int iCommand = GetCritCommand(pWeapon, pCmd->command_number, iRequest == CritRequestEnum::Crit))
		{
			pCmd->command_number = iCommand;
			pCmd->random_seed = MD5_PseudoRandom(iCommand) & std::numeric_limits<int>::max();
		}
	}
	else if (Vars::Misc::Game::AntiCheatCritHack.Value)
	{
		if (!IsCritCommand(pCmd->command_number, pWeapon, iRequest == CritRequestEnum::Crit, false))
		{
			pCmd->buttons &= ~IN_ATTACK;
			pCmd->viewangles = G::OriginalCmd.viewangles;
			G::PSilentAngles = false;
		}
	}
}

int CCritHack::PredictCmdNum(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	auto fGetCmdNum = [&](int iCommandNumber)
	{
		if (!pWeapon || !pLocal->IsAlive() || !I::EngineClient->IsInGame() || F::AntiCheatCompatibility.Active()
			|| pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !WeaponCanCrit(pWeapon))
			return iCommandNumber;

		UpdateInfo(pLocal, pWeapon);
		if (pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
			return iCommandNumber;

		int iRequest = GetCritRequest(pCmd, pLocal, pWeapon);
		if (iRequest == CritRequestEnum::Any)
			return iCommandNumber;

		if (int iCommand = GetCritCommand(pWeapon, iCommandNumber, iRequest == CritRequestEnum::Crit))
			return iCommand;
		return iCommandNumber;
	};

	static int iCommandNumber = 0; // cache, don't constantly test

	static int iStaticCommand = 0;
	if (pCmd->command_number != iStaticCommand)
	{
		iCommandNumber = fGetCmdNum(pCmd->command_number);
		iStaticCommand = pCmd->command_number;
	}

	return iCommandNumber;
}

bool CCritHack::ShouldForceEffects(CTFPlayer* pLocal)
{
	if (!Vars::CritHack::CritEffects.Value || !pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->IsCritBoosted())
		return false;

	auto pWeapon = H::Entities.GetWeapon();
	if (!pWeapon || !WeaponCanCrit(pWeapon))
		return false;

	float flTickBase = TICKS_TO_TIME(pLocal->m_nTickBase());
	return Vars::CritHack::ForceCrits.Value && !m_bCritBanned && m_iAvailableCrits > 0 || pWeapon->m_flCritTime() > flTickBase;
}

void CCritHack::Event(IGameEvent* pEvent, uint32_t uHash, CTFPlayer* pLocal)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("player_hurt"):
	{
		if (!pLocal)
			return;

		int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
		bool bCrit = pEvent->GetBool("crit") || pEvent->GetBool("minicrit");
		int iDamage = pEvent->GetInt("damageamount");
		int iHealth = pEvent->GetInt("health");
		int iWeaponID = pEvent->GetInt("weaponid");

		if (m_mHealthHistory.contains(iVictim))
		{
			auto& tHistory = m_mHealthHistory[iVictim];
			CTFPlayer* pVictim = nullptr;
			auto pVictimEntity = iVictim > 0 && iVictim <= I::EngineClient->GetMaxClients() ? I::ClientEntityList->GetClientEntity(iVictim) : nullptr;
			if (uintptr_t(pVictimEntity) > 0x10000 && pVictimEntity->GetClassID() == ETFClassID::CTFPlayer)
				pVictim = pVictimEntity->As<CTFPlayer>();

			if (!iHealth)
			{
				iDamage = std::clamp(iDamage, 0, tHistory.m_iNewHealth);
				tHistory.m_iSpawnCounter = -1;
			}
			else if (pVictim && (pVictim->m_bFeignDeathReady() || pVictim->InCond(TF_COND_FEIGN_DEATH))) // damage number is spoofed upon sending, correct it
			{
				int iOldHealth = (tHistory.m_mHistory.contains(iHealth) ? tHistory.m_mHistory[iHealth].m_iOldHealth : tHistory.m_iNewHealth) % 32768;
				if (iHealth > iOldHealth)
				{
					for (auto& tOldHealth : tHistory.m_mHistory | std::views::values)
					{
						int iOldHealth2 = tOldHealth.m_iOldHealth % 32768;
						if (iOldHealth2 > iHealth)
							iOldHealth = iHealth > iOldHealth ? iOldHealth2 : std::min(iOldHealth, iOldHealth2);
					}
				}
				iDamage = std::clamp(iOldHealth - iHealth, 0, iDamage);
			}
		}
		if (iHealth)
			StoreHealthHistory(iVictim, iHealth);

		if (iVictim == iAttacker || iAttacker != I::EngineClient->GetLocalPlayer())
			return;

		if (auto pGameRules = I::TFGameRules())
		{
			auto pMatchDesc = pGameRules->GetMatchGroupDescription();
			if (pMatchDesc && pGameRules->m_iRoundState() != GR_STATE_RND_RUNNING)
			{
				switch (pMatchDesc->m_eMatchType)
				{
				case MATCH_TYPE_COMPETITIVE:
				case MATCH_TYPE_CASUAL:
					return;
				}
			}

			const int iInsanePlayerDamage = pGameRules->m_bPlayingMannVsMachine() ? 5000 : 1500;
			if (iDamage > iInsanePlayerDamage)
				return;
		}

		//m_flLastDamageTime = I::GlobalVars->curtime;

		CTFWeaponBase* pWeapon = nullptr;
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pWeapon2 = pLocal->GetWeaponFromSlot(i);
			if (!pWeapon2 || pWeapon2->GetWeaponID() != iWeaponID)
				continue;

			pWeapon = pWeapon2;
			break;
		}

		if (!pWeapon || pWeapon->GetSlot() != SLOT_MELEE)
		{
			m_iRangedDamage += iDamage;
			if (bCrit && !pLocal->IsCritBoosted())
				m_iCritDamage += iDamage;
		}
		else
			m_iMeleeDamage += iDamage;

		return;
	}
	case FNV1A::Hash32Const("player_spawn"):
	{
		int iIndex = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));

		if (m_mHealthHistory.contains(iIndex))
		{
			auto& tHistory = m_mHealthHistory[iIndex];

			tHistory.m_iSpawnCounter = -1;
		}

		return;
	}
	case FNV1A::Hash32Const("scorestats_accumulated_update"):
	case FNV1A::Hash32Const("mvm_reset_stats"):
	{
		m_iRangedDamage = m_iCritDamage = m_iMeleeDamage = 0;
		return;
	}
	case FNV1A::Hash32Const("client_beginconnect"):
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("game_newmap"):
	{
		Reset();
	}
	}
}

void CCritHack::Store()
{
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		CTFPlayer* pPlayer = nullptr;
		auto pEntity = I::ClientEntityList->GetClientEntity(n);
		if (uintptr_t(pEntity) > 0x10000 && pEntity->GetClassID() == ETFClassID::CTFPlayer)
			pPlayer = pEntity->As<CTFPlayer>();

		if (pPlayer && pPlayer->IsAlive() && !pPlayer->IsAGhost())
			StoreHealthHistory(n, pPlayer->m_iHealth(), pPlayer);
	}
}

void CCritHack::StoreHealthHistory(int iIndex, int iHealth, CTFPlayer* pPlayer)
{
	bool bContains = m_mHealthHistory.contains(iIndex);
	auto& tHistory = m_mHealthHistory[iIndex];

	if (bContains && pPlayer)
	{	// deal with instant respawn damage desync better
		if (pPlayer->IsDormant())
			tHistory.m_iSpawnCounter = -1;
		else if (tHistory.m_iSpawnCounter == -1)
			tHistory.m_iSpawnCounter = pPlayer->m_iSpawnCounter();
		else if (tHistory.m_iSpawnCounter != pPlayer->m_iSpawnCounter())
			return; // wait for spawn
	}

	if (!bContains)
		tHistory = { iHealth, iHealth };
	else if (iHealth != tHistory.m_iNewHealth)
	{
		tHistory.m_iOldHealth = std::max(tHistory.m_iNewHealth, iHealth);
		tHistory.m_iNewHealth = iHealth;
	}

	tHistory.m_mHistory[iHealth % 32768] = { tHistory.m_iOldHealth, float(SDK::PlatFloatTime()) };
	while (tHistory.m_mHistory.size() > 3)
	{
		int iIndex2; float flMin = std::numeric_limits<float>::max();
		for (auto& [i, tStorage] : tHistory.m_mHistory)
		{
			if (tStorage.m_flTime < flMin)
				flMin = tStorage.m_flTime, iIndex2 = i;
		}
		tHistory.m_mHistory.erase(iIndex2);
	}
}

#ifdef SERVER_CRIT_DATA
MAKE_SIGNATURE(CTFGameStats_FindPlayerStats, "server.dll", "4C 8B C1 48 85 D2 75", 0x0);
MAKE_SIGNATURE(UTIL_PlayerByIndex, "server.dll", "48 83 EC ? 8B D1 85 C9 7E ? 48 8B 05", 0x0);

static void* s_pCTFGameStats = nullptr;
MAKE_HOOK(CTFGameStats_FindPlayerStats, S::CTFGameStats_FindPlayerStats(), void*,
	void* rcx, CBasePlayer* pPlayer)
{
	DEBUG_RETURN(CTFGameStats_FindPlayerStats, rcx, pPlayer);

	s_pCTFGameStats = rcx;
	return CALL_ORIGINAL(rcx, pPlayer);
}
#endif

void CCritHack::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::CritHack) || !I::EngineClient->IsInGame())
		return;

	auto pWeapon = H::Entities.GetWeapon();
	if (!pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost())
		return;

	if (!WeaponCanCrit(pWeapon, true))
	{
		DrawIndicatorPanel(
			ImGui::GetForegroundDrawList(),
			{
				static_cast<float>(Vars::Menu::CritsDisplay.Value.x) - H::Draw.Scale(180.f) / 2.f,
				static_cast<float>(Vars::Menu::CritsDisplay.Value.y)
			},
			H::Draw.Scale(180.f),
			H::Draw.Scale(29.f),
			"Cannot crit",
			"DISABLED",
			Color_t(255, 255, 255, 255),
			Color_t(200, 40, 40, 255),
			Color_t(200, 40, 40, 255),
			1.f);
		return;
	}

	const ImVec2 vPanelPos =
	{
		static_cast<float>(Vars::Menu::CritsDisplay.Value.x) - H::Draw.Scale(180.f) / 2.f,
		static_cast<float>(Vars::Menu::CritsDisplay.Value.y)
	};
	const float flPanelWidth = H::Draw.Scale(180.f);
	const float flPanelHeight = H::Draw.Scale(29.f);
	const float flTickBase = TICKS_TO_TIME(pLocal->m_nTickBase());
	float flTargetProgress = 0.f;
	static float flCurrentProgress = 0.f;
	std::string sLeftText = "Calculating";
	std::string sRightText = "";
	Color_t tLeftColor = Vars::Menu::Theme::Active.Value;
	Color_t tRightColor = Vars::Menu::Theme::Active.Value;
	Color_t tBarColor = Color_t(0, 255, 100, 255);

	if (!pWeapon->AreRandomCritsEnabled())
	{
		sLeftText = "Random crits";
		sRightText = "DISABLED";
		tRightColor = Color_t(200, 40, 40, 255);
		tBarColor = Color_t(200, 40, 40, 255);
		flTargetProgress = 1.f;
	}
	else if (F::AntiCheatCompatibility.Active())
	{
		sLeftText = "Anticheat";
		sRightText = "COMPAT";
		tRightColor = Color_t(255, 150, 0, 255);
		tBarColor = Color_t(255, 150, 0, 255);
		flTargetProgress = 1.f;
	}
	else if (pWeapon->m_flCritTime() > flTickBase)
	{
		const float flTime = pWeapon->m_flCritTime() - flTickBase;
		sLeftText = std::format("Crits: {} / {}", std::max(0, m_iAvailableCrits), m_iPotentialCrits);
		sRightText = "STREAMING";
		tRightColor = Color_t(100, 255, 255, 255);
		tBarColor = Color_t(100, 255, 255, 255);
		flTargetProgress = std::clamp(flTime / TF_DAMAGE_CRIT_DURATION_RAPID, 0.f, 1.f);
	}
	else if (pLocal->IsCritBoosted())
	{
		sLeftText = "Crit Boosted";
		sRightText = "READY";
		tRightColor = Color_t(100, 255, 255, 255);
		tBarColor = Color_t(100, 255, 255, 255);
		flTargetProgress = 1.f;
	}
	else if (!m_bCritBanned || m_bMelee)
	{
		sLeftText = std::format("Crits: {} / {}", std::max(0, m_iAvailableCrits), m_iPotentialCrits);
		if (pWeapon->IsRapidFire() && flTickBase < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
		{
			const float flTime = pWeapon->m_flLastRapidFireCritCheckTime() + 1.f - flTickBase;
			sRightText = std::format("WAIT {:.2f}s", flTime);
			tRightColor = m_iAvailableCrits > 0 ? Color_t(40, 200, 40, 255) : Color_t(200, 40, 40, 255);
			tBarColor = Color_t(255, 150, 0, 255);
			flTargetProgress = std::clamp(flTime, 0.f, 1.f);
		}
		else if (m_iAvailableCrits >= m_iPotentialCrits)
		{
			sRightText = "READY";
			tRightColor = Color_t(40, 200, 40, 255);
			flTargetProgress = 1.f;
		}
		else if (m_iPotentialCrits > 0)
		{
			const float flBucket = pWeapon->m_flCritTokenBucket();
			static auto bucketCap = H::ConVars.FindVar("tf_weapon_criticals_bucket_cap");
			const int iDamageNeeded = static_cast<int>(std::ceil(m_flCost - flBucket));
			sRightText = std::format("DMG: {}", std::max(0, iDamageNeeded));
			tRightColor = m_iAvailableCrits > 0 ? Color_t(40, 200, 40, 255) : Color_t(200, 40, 40, 255);
			flTargetProgress = bucketCap ? std::clamp(flBucket / bucketCap->GetFloat(), 0.f, 1.f) : 0.f;
		}
	}
	else
	{
		sLeftText = std::format("DMG: {}", static_cast<int>(std::ceil(m_flDamageTilFlip)));
		sRightText = "BANNED";
		tRightColor = Color_t(200, 40, 40, 255);
		tBarColor = Color_t(200, 40, 40, 255);
		flTargetProgress = 0.2f;
	}

	if (m_flDamage <= 0.f && sRightText.empty())
	{
		sLeftText = "Calculating";
		flTargetProgress = 0.f;
	}

	flCurrentProgress = std::lerp(flCurrentProgress, flTargetProgress, ImGui::GetIO().DeltaTime * 10.f);
	DrawIndicatorPanel(ImGui::GetForegroundDrawList(), vPanelPos, flPanelWidth, flPanelHeight, sLeftText.c_str(), sRightText.c_str(), tLeftColor, tRightColor, tBarColor, flCurrentProgress, F::Ticks.m_iWait > 0, "Not Ready");
}
