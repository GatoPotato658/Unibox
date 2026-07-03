#include "Entities.h"

#include "../../SDK.h"
#include "../../../Utils/Hash/FNV1A.h"
#include "../../../Features/Players/PlayerUtils.h"
#include "../../../Features/Backtrack/Backtrack.h"
#include "../../../Features/CheatDetection/CheatDetection.h"
#include "../../../Features/Resolver/Resolver.h"
#include "../../../Features/Misc/AutoVote/AutoVote.h"
#include "../../../Features/Configs/Configs.h"

static std::unordered_map<unsigned short, DormantData> s_mDormancy = {};

bool CEntities::UpdatePlayerDetails(int n, CTFPlayer* pPlayer, int iLag)
{
	bool bDormant = ManageDormancy(n, pPlayer);
	float flSimTime = pPlayer->m_flSimulationTime();

	if (float flOldSimTime = m_aSimTimes[n], flSimTime = m_aSimTimes[n] = pPlayer->m_flSimulationTime(); m_aDeltaTimes[n] = flSimTime > flOldSimTime)
	{
		m_aDeltaTimes[n] = m_aLagTimes[n] = TICKS_TO_TIME(std::clamp(TIME_TO_TICKS(flSimTime - flOldSimTime) - iLag, 1, 24));
		m_aSetTicks[n] = I::GlobalVars->tickcount;
		if (!bDormant)
		{
			m_aOrigins[n].emplace_front(pPlayer->m_vecOrigin() + Vec3(0, 0, pPlayer->GetSize().z), flSimTime);
			if (m_aOrigins[n].size() > (size_t)Vars::Aimbot::Projectile::VelocityAverageCount.Value)
				m_aOrigins[n].pop_back();

			if (pPlayer->IsAlive())
				F::CheatDetection.ReportChoke(pPlayer, m_aChokes[n]);
			m_aOldAngles[n] = m_aEyeAngles[n], m_aEyeAngles[n] = pPlayer->GetEyeAngles();
		}
	}
	if (!bDormant)
		m_aChokes[n] = std::max(0, I::GlobalVars->tickcount - m_aSetTicks[n]);
	else
	{
		m_aOrigins[n].clear();
		if (s_mDormancy.contains(n))
			m_aChokes[n] = std::max(0, TIME_TO_TICKS(I::GlobalVars->curtime - s_mDormancy[n].m_flLastUpdate));
	}
	return !bDormant;
}

void CEntities::UpdatePartyAndLobbyInfo(int nLocalIndex)
{
	static Timer tUpdateTimer{};
	if (!tUpdateTimer.Run(1.0f))
		return;

	for (int i = 0; i < PriorityTypeEnum::Count; i++)
	{
		m_aIPriorities[i].clear();
		m_aUPriorities[i].clear();
	}
	m_mIFriends.clear();			m_mUFriends.clear();
	m_mIParty.clear();				m_mUParty.clear();
	m_mIF2P.clear();				m_mUF2P.clear();
	m_mILevels.clear();				m_mULevels.clear();

	auto pResource = GetResource();
	if (!pResource)
	{
		tUpdateTimer -= 1.0f;
		return;
	}

	std::unordered_map<uint32_t, uint64_t> mParties;
	std::unordered_map<uint32_t, bool> mF2P;
	std::unordered_map<uint32_t, int> mLevels;

	if (auto pLobby = I::TFGCClientSystem->GetLobby())
	{
		auto pGameRules = I::TFGameRules();
		auto pMatchDesc = pGameRules ? pGameRules->GetMatchGroupDescription() : nullptr;

		int iMembers = pLobby->GetNumMembers();
		for (int i = 0; i < iMembers; i++)
		{
			CSteamID tSteamID;
			if (pLobby->GetMember(&tSteamID, i))
			{
				uint32_t uAccountID = tSteamID.GetAccountID();
				ConstTFLobbyPlayer pDetails;
				if (pLobby->GetMemberDetails(&pDetails, i))
				{
					auto pProto = pDetails.Proto();
					mF2P[uAccountID] = pProto->chat_suspension;
					mParties[uAccountID] = pProto->original_party_id;
					if (pMatchDesc && pMatchDesc->m_pProgressionDesc)
						mLevels[uAccountID] = std::max((int)pProto->rank, pMatchDesc->GetLevelForSteamID(&tSteamID));
					else
						mLevels[uAccountID] = pProto->rank;
				}
			}
		}
	}
	if (auto pParty = I::TFGCClientSystem->GetParty())
	{
		int iMembers = pParty->GetNumMembers();
		for (int i = 0; i < iMembers; i++)
		{
			CSteamID tSteamID;
			if (pParty->GetMember(&tSteamID, i))
				mParties[tSteamID.GetAccountID()] = 1;
		}
	}

	std::map<uint64_t, std::vector<uint32_t>> mPartiesGrouped;
	for (auto& [uAccountID, uPartyID] : mParties)
	{
		if (uPartyID)
			mPartiesGrouped[uPartyID].push_back(uAccountID);
	}
	mParties.clear();
	uint64_t uPartyCount = 0;
	for (auto& [uPartyID, vAccountIds] : mPartiesGrouped)
	{
		if (vAccountIds.size() <= 1) continue;

		int iPartyIndex = (uPartyID == 1) ? 1 : (++uPartyCount + 1);
		for (auto uAccountID : vAccountIds)
			mParties[uAccountID] = iPartyIndex;
	}
	m_iPartyCount = uPartyCount;

	int nMaxClients = I::EngineClient->GetMaxClients();
	for (int n = 1; n <= nMaxClients; n++)
	{
		if (!pResource->m_bValid(n)) continue;

		uint32_t uAccountID = pResource->m_iAccountID(n);
		bool bLocal = (n == nLocalIndex);
		if (bLocal) m_uAccountID = uAccountID;

		const int iPriority = bLocal ? 0 : F::PlayerUtils.GetPriority(uAccountID, false);

		m_aIPriorities[PriorityTypeEnum::Relationship][n] = m_aUPriorities[PriorityTypeEnum::Relationship][uAccountID] = iPriority;
		m_aIPriorities[PriorityTypeEnum::Follow][n] = m_aUPriorities[PriorityTypeEnum::Follow][uAccountID] = !bLocal ? F::PlayerUtils.GetFollowPriority(uAccountID, false) : 0;
		m_aIPriorities[PriorityTypeEnum::Vote][n] = m_aUPriorities[PriorityTypeEnum::Vote][uAccountID] = !bLocal ? F::PlayerUtils.GetVotePriority(uAccountID, false) : -1;

		m_mIFriends[n] = m_mUFriends[uAccountID] = !pResource->IsFakePlayer(n) && I::SteamFriends->HasFriend({ uAccountID, 1, k_EUniversePublic, k_EAccountTypeIndividual }, k_EFriendFlagImmediate);
		m_mIParty[n] = m_mUParty[uAccountID] = mParties.count(uAccountID) ? mParties[uAccountID] : 0;
		m_mIF2P[n] = m_mUF2P[uAccountID] = mF2P.count(uAccountID) ? mF2P[uAccountID] : false;
		m_mILevels[n] = m_mULevels[uAccountID] = mLevels.count(uAccountID) ? mLevels[uAccountID] : -2;
	}
}

void CEntities::UpdatePlayerAnimations(int nLocalIndex)
{
	F::Resolver.FrameStageNotify();

	const auto& vPlayers = m_aGroups[EntityEnum::PlayerAll];
	if (vPlayers.empty()) return;

	bool bDisableinterpolation = Vars::Visuals::Removals::Interpolation.Value;
	bool bPlayingDemo = I::EngineClient->IsPlayingDemo();

	for (auto pEntity : vPlayers)
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive()) continue;
		if (pPlayer->entindex() == nLocalIndex && !bPlayingDemo) // local player managed in CreateMove
			continue;

		bool bResolved = F::Resolver.GetAngles(pPlayer);
		if (!bDisableinterpolation && !bResolved)
			continue;

		int iDeltaTicks = TIME_TO_TICKS(GetDeltaTime(pPlayer->entindex()));
		if (iDeltaTicks <= 0) continue;

		float flOldFrameTime = I::GlobalVars->frametime;
		I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;

		G::UpdatingAnims = true;
		for (int i = 0; i < iDeltaTicks; i++)
		{
			if (bResolved)
			{
				float flYaw, flPitch;
				F::Resolver.GetAngles(pPlayer, &flYaw, &flPitch, nullptr, i + 1 == iDeltaTicks);

				float flOriginalYaw = pPlayer->m_angEyeAnglesY();
				float flOriginalPitch = pPlayer->m_angEyeAnglesX();

				pPlayer->m_angEyeAnglesY() = flYaw, pPlayer->m_angEyeAnglesX() = flPitch;
				pPlayer->UpdateClientSideAnimation();
				pPlayer->m_angEyeAnglesY() = flOriginalYaw, pPlayer->m_angEyeAnglesX() = flOriginalPitch;
			}
			else
				pPlayer->UpdateClientSideAnimation();
		}
		G::UpdatingAnims = false;
		I::GlobalVars->frametime = flOldFrameTime;
	}
}

void CEntities::Store()
{
	int nLocalIndex = I::EngineClient->GetLocalPlayer();
	auto pLocalEntity = I::ClientEntityList->GetClientEntity(nLocalIndex);
	if (!pLocalEntity)
		return;

	m_bIsSpectated = false;
	m_pLocal = pLocalEntity->As<CTFPlayer>();
	if (!m_pLocal)
		return;

	auto pLocalWeapon = m_pLocal->m_hActiveWeapon().Get();
	m_pLocalWeapon = pLocalWeapon ? pLocalWeapon->As<CTFWeaponBase>() : nullptr;

	int iLocalTeam = m_pLocal->m_iTeamNum();

	int iLag;
	{
		static int iStaticTickcout = I::GlobalVars->tickcount;
		iLag = I::GlobalVars->tickcount - iStaticTickcout - 1;
		iStaticTickcout = I::GlobalVars->tickcount;
	}

	int nMaxClients = I::EngineClient->GetMaxClients();
	int nHighestEntity = I::ClientEntityList->GetHighestEntityIndex();

	for (int n = 1; n <= nHighestEntity; n++)
	{
		auto pEntityHandle = I::ClientEntityList->GetClientEntity(n);
		if (!pEntityHandle)
			continue;

		auto pEntity = pEntityHandle->As<CBaseEntity>();
		if (!pEntity)
			continue;

		auto nClassID = pEntity->GetClassID();
		if (n <= nMaxClients)
		{
			if (nClassID == ETFClassID::CTFPlayer)
			{	
				auto pPlayer = pEntity->As<CTFPlayer>();

				m_aGroups[EntityEnum::PlayerAll].push_back(pPlayer);
				m_aGroups[pPlayer->m_iTeamNum() != iLocalTeam ? EntityEnum::PlayerEnemy : EntityEnum::PlayerTeam].push_back(pPlayer);

				if (n != nLocalIndex)
				{
					if (!UpdatePlayerDetails(n, pPlayer, iLag))
						continue;

					// Check if this player is spectating the local player 
					if (!m_bIsSpectated && !pPlayer->IsAlive() &&
						pPlayer->m_iObserverMode() == OBS_MODE_FIRSTPERSON &&
						pPlayer->m_hObserverTarget().GetEntryIndex() == nLocalIndex)
						m_bIsSpectated = true;
				}
				m_aModels[n] = FNV1A::Hash32(I::ModelInfoClient->GetModelName(pEntity->GetModel()));
			}
		}
		else
		{
			switch (nClassID)
			{
			case ETFClassID::CTFObjectiveResource:
				m_pObjectiveResource = pEntity->As<CBaseTeamObjectiveResource>();
				break;
			case ETFClassID::CCaptureFlag:
			case ETFClassID::CCaptureZone:
			case ETFClassID::CPasstimeBall:
			case ETFClassID::CTFPasstimeLogic:
			case ETFClassID::CFuncPasstimeGoal:
			case ETFClassID::CObjectCartDispenser:
			case ETFClassID::CFuncTrackTrain:
				m_aGroups[EntityEnum::WorldObjective].push_back(pEntity);
				break;
			}

			if (!ManageDormancy(n, pEntity))
			{
				switch (nClassID)
				{
				case ETFClassID::CTFPlayerResource:
					m_pPlayerResource = pEntity->As<CTFPlayerResource>();
					break;
				case ETFClassID::CObjectSentrygun:
				case ETFClassID::CObjectDispenser:
				case ETFClassID::CObjectTeleporter:
					m_aModels[n] = FNV1A::Hash32(I::ModelInfoClient->GetModelName(pEntity->GetModel()));
					m_aGroups[EntityEnum::BuildingAll].push_back(pEntity);
					m_aGroups[pEntity->m_iTeamNum() != iLocalTeam ? EntityEnum::BuildingEnemy : EntityEnum::BuildingTeam].push_back(pEntity);
					break;
				case ETFClassID::CBaseProjectile:
				case ETFClassID::CBaseGrenade:
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
				case ETFClassID::CTFBaseRocket:
				case ETFClassID::CTFFlameRocket:
				case ETFClassID::CTFProjectile_Arrow:
				case ETFClassID::CTFProjectile_GrapplingHook:
				case ETFClassID::CTFProjectile_HealingBolt:
				case ETFClassID::CTFProjectile_Rocket:
				case ETFClassID::CTFProjectile_BallOfFire:
				case ETFClassID::CTFProjectile_MechanicalArmOrb:
				case ETFClassID::CTFProjectile_SentryRocket:
				case ETFClassID::CTFProjectile_SpellFireball:
				case ETFClassID::CTFProjectile_SpellLightningOrb:
				case ETFClassID::CTFProjectile_SpellKartOrb:
				case ETFClassID::CTFProjectile_EnergyBall:
				case ETFClassID::CTFProjectile_Flare:
				case ETFClassID::CTFBaseProjectile:
				case ETFClassID::CTFProjectile_EnergyRing:
				{
					if ((nClassID == ETFClassID::CTFProjectile_Cleaver || nClassID == ETFClassID::CTFStunBall) &&
						pEntity->As<CTFGrenadePipebombProjectile>()->m_bTouched())
						break;

					if ((nClassID == ETFClassID::CTFProjectile_Arrow || nClassID == ETFClassID::CTFProjectile_GrapplingHook) &&
						!pEntity->m_MoveType())
						break;

					m_aGroups[EntityEnum::WorldProjectile].push_back(pEntity);

					if (nClassID == ETFClassID::CTFGrenadePipebombProjectile)
					{
						auto pPipebomb = pEntity->As<CTFGrenadePipebombProjectile>();
						if (pPipebomb->m_hThrower().GetEntryIndex() == nLocalIndex && pPipebomb->m_iType() == TF_GL_MODE_REMOTE_DETONATE)
							m_aGroups[EntityEnum::LocalStickies].push_back(pEntity);
					}
					else if (nClassID == ETFClassID::CTFProjectile_Flare && pEntity->m_hOwnerEntity().GetEntryIndex() == nLocalIndex)
					{
						auto pLauncher = pEntity->As<CTFProjectile_Flare>()->m_hLauncher()->As<CTFWeaponBase>();
						if (pLauncher && pLauncher->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_DETONATE)
							m_aGroups[EntityEnum::LocalFlares].push_back(pEntity);
					}
					break;
				}
				case ETFClassID::CTFBaseBoss:
				case ETFClassID::CTFTankBoss:
				case ETFClassID::CMerasmus:
				case ETFClassID::CEyeballBoss:
				case ETFClassID::CHeadlessHatman:
				case ETFClassID::CZombie:
					m_aGroups[EntityEnum::WorldNPC].push_back(pEntity);
					break;
				case ETFClassID::CTFGenericBomb:
				case ETFClassID::CTFPumpkinBomb:
					m_aGroups[EntityEnum::WorldBomb].push_back(pEntity);
					break;
				case ETFClassID::CBaseAnimating:
					m_aModels[n] = FNV1A::Hash32(I::ModelInfoClient->GetModelName(pEntity->GetModel()));
					break;
				case ETFClassID::CTFAmmoPack:
					m_aGroups[EntityEnum::PickupAmmo].push_back(pEntity);
					break;
				case ETFClassID::CCurrencyPack:
					m_aGroups[EntityEnum::PickupMoney].push_back(pEntity);
					break;
				case ETFClassID::CLaserDot:
					if (pEntity->As<CSniperDot>()->m_hOwnerEntity().GetEntryIndex() == nLocalIndex)
						m_pLocalLaserDot = pEntity->As<CSniperDot>();
					break;
				case ETFClassID::CSniperDot:
					m_aGroups[EntityEnum::SniperDots].push_back(pEntity);
					break;
				}
			}
			else if (nClassID == ETFClassID::CObjectSentrygun ||
				nClassID == ETFClassID::CObjectDispenser)
			{
				m_aGroups[EntityEnum::BuildingAll].push_back(pEntity);
				m_aGroups[pEntity->m_iTeamNum() != iLocalTeam ? EntityEnum::BuildingEnemy : EntityEnum::BuildingTeam].push_back(pEntity);
			}
		}
	}

	UpdatePartyAndLobbyInfo(nLocalIndex);
	UpdatePlayerAnimations(nLocalIndex);
}

void CEntities::Clear(bool bShutdown)
{
	m_pLocal = nullptr;
	m_pLocalWeapon = nullptr;
	m_pLocalLaserDot = nullptr;
	m_pPlayerResource = nullptr;
	m_pObjectiveResource = nullptr;
	m_aGroups = {};

	if (bShutdown)
	{
		m_aDeltaTimes = {};
		m_aLagTimes = {};
		m_aChokes = {};
		m_aSetTicks = {};
		m_aOldAngles = {};
		m_aEyeAngles = {};
		m_aLagCompensation = {};
		m_aAvgVelocities = {};
		m_aOrigins = {};
		m_aModels = {};
		s_mDormancy.clear();

		for (int i = 0; i < PriorityTypeEnum::Count; i++)
		{
			m_aIPriorities[i].clear();
			m_aUPriorities[i].clear();
		}
		m_mIFriends.clear();			m_mUFriends.clear();
		m_mIParty.clear();				m_mUParty.clear();
		m_mIF2P.clear();				m_mUF2P.clear();
		m_mILevels.clear();				m_mULevels.clear();
	}
}

void CEntities::ManualNetwork(const StartSoundParams_t& params)
{
	int n = params.soundsource;
	if (n <= 0 || n > MAX_EDICTS - 1 || !params.origin || n == I::EngineClient->GetLocalPlayer())
		return;

	auto pEntityHandle = I::ClientEntityList->GetClientEntity(n);
	if (!pEntityHandle)
		return;

	auto pEntity = pEntityHandle->As<CBaseEntity>();
	if (!pEntity || !pEntity->IsDormant())
		return;

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CTFPlayer:
		pEntity->As<CTFPlayer>()->m_vecVelocity() = (params.origin - pEntity->m_vecOrigin()) / std::min(I::GlobalVars->curtime - s_mDormancy[n].m_flLastUpdate, 1.f);
		pEntity->SetAbsVelocity(pEntity->As<CTFPlayer>()->m_vecVelocity()); SetAvgVelocity(pEntity->entindex(), pEntity->As<CTFPlayer>()->m_vecVelocity());
	}
	pEntity->SetAbsOrigin(pEntity->m_vecOrigin() = params.origin);

	s_mDormancy[n] = { params.origin, I::GlobalVars->curtime };
}

bool CEntities::ManageDormancy(int nIndex, CBaseEntity* pEntity)
{
	bool bDormant = pEntity->IsDormant();

	float flDuration = 0.f;
	const auto nClassID = pEntity->GetClassID();
	switch (nClassID)
	{
	case ETFClassID::CTFPlayer: flDuration = 1.f; break;
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter: flDuration = 5.f; break;
	}
	if (!flDuration)
		return bDormant;

	int n = pEntity->entindex();
	if (n < 0 || n > MAX_EDICTS - 1)
		return bDormant;

	if (bDormant)
	{
		if (auto pResource = GetResource(); pResource && pEntity->IsPlayer())
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			pPlayer->m_lifeState() = pResource->m_bAlive(n) ? LIFE_ALIVE : LIFE_DEAD;
			pPlayer->m_iHealth() = pResource->m_iHealth(n);
			if (pPlayer->IsAlive() && pPlayer->m_iObserverMode() != OBS_MODE_NONE)
				pPlayer->m_iObserverMode() = OBS_MODE_NONE;
		}
		if (s_mDormancy.contains(n))
		{
			auto& tDormancy = s_mDormancy[n];
			if (tDormancy.m_flLastUpdate + flDuration < I::GlobalVars->curtime || pEntity->IsPlayer() && !pEntity->As<CTFPlayer>()->IsAlive())
				s_mDormancy.erase(n);
		}
	}
	else if (!pEntity->IsPlayer() || pEntity->As<CTFPlayer>()->IsAlive())
		s_mDormancy[n] = { pEntity->m_vecOrigin(), I::GlobalVars->curtime };
	return bDormant;
}

bool CEntities::IsHealth(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("models/items/banana/plate_banana.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_small.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_medium.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_large.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_small_bday.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_medium_bday.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_large_bday.mdl"):
	case FNV1A::Hash32Const("models/items/plate.mdl"):
	case FNV1A::Hash32Const("models/items/plate_sandwich_xmas.mdl"):
	case FNV1A::Hash32Const("models/items/plate_robo_sandwich.mdl"):
	case FNV1A::Hash32Const("models/props_medieval/medieval_meat.mdl"):
	case FNV1A::Hash32Const("models/workshopweapons/c_models/c_chocolate/plate_chocolate.mdl"):
	case FNV1A::Hash32Const("models/workshopweapons/c_models/c_fishcake/plate_fishcake.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/halloween_medkit_small.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/halloween_medkit_medium.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/halloween_medkit_large.mdl"):
	case FNV1A::Hash32Const("models/items/ld1/mushroom_large.mdl"):
	case FNV1A::Hash32Const("models/items/plate_steak.mdl"):
	case FNV1A::Hash32Const("models/props_brine/foodcan.mdl"):
		return true;
	}
	return false;
}

bool CEntities::IsAmmo(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("models/items/ammopack_small.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_medium.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_large.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_large_bday.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_medium_bday.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_small_bday.mdl"):
		return true;
	}
	return false;
}

bool CEntities::IsPowerup(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_agility.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_crit.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_defense.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_haste.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_king.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_knockout.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_plague.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_precision.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_reflect.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_regen.mdl"):
	//case FNV1A::Hash32Const("models/pickups/pickup_powerup_resistance.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_strength.mdl"):
	//case FNV1A::Hash32Const("models/pickups/pickup_powerup_strength_arm.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_supernova.mdl"):
	//case FNV1A::Hash32Const("models/pickups/pickup_powerup_thorns.mdl"):
	//case FNV1A::Hash32Const("models/pickups/pickup_powerup_uber.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_vampire.mdl"):
		return true;
	}
	return false;
}

bool CEntities::IsSpellbook(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("models/props_halloween/hwn_spellbook_flying.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/hwn_spellbook_upright.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/hwn_spellbook_upright_major.mdl"):
	case FNV1A::Hash32Const("models/items/crystal_ball_pickup.mdl"):
	case FNV1A::Hash32Const("models/items/crystal_ball_pickup_major.mdl"):
	case FNV1A::Hash32Const("models/props_monster_mash/flask_vial_green.mdl"):
	case FNV1A::Hash32Const("models/props_monster_mash/flask_vial_purple.mdl"): // prop_dynamic in the map, probably won't work
		return true;
	}
	return false;
}

CTFPlayer* CEntities::GetLocal() { return m_pLocal; }
CTFWeaponBase* CEntities::GetWeapon() { return m_pLocalWeapon; }
CSniperDot* CEntities::GetLaserDot() { return m_pLocalLaserDot; }
CTFPlayerResource* CEntities::GetResource() { return m_pPlayerResource; }
CBaseTeamObjectiveResource* CEntities::GetObjectiveResource( ) { return m_pObjectiveResource; }

const std::vector<CBaseEntity*>& CEntities::GetGroup(uint8_t iGroup) { return m_aGroups[iGroup]; }

float CEntities::GetDeltaTime(uint16_t iIndex) { return iIndex < MAX_PLAYERS ? m_aDeltaTimes[iIndex] : TICK_INTERVAL; }
float CEntities::GetLagTime(uint16_t iIndex) { return iIndex < MAX_PLAYERS ? m_aLagTimes[iIndex] : TICK_INTERVAL; }
int CEntities::GetChoke(uint16_t iIndex) { return iIndex < MAX_PLAYERS ? m_aChokes[iIndex] : 0; }
Vec3 CEntities::GetEyeAngles(uint16_t iIndex) { return iIndex < MAX_PLAYERS ? m_aEyeAngles[iIndex] : Vec3(); }
Vec3 CEntities::GetDeltaAngles(uint16_t iIndex) { return iIndex < MAX_PLAYERS ? m_aEyeAngles[iIndex].DeltaAngle(m_aOldAngles[iIndex]) / GetLagTime(iIndex) * (F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke())) : Vec3(); }
bool CEntities::GetLagCompensation(uint16_t iIndex) { return iIndex < MAX_PLAYERS ? m_aLagCompensation[iIndex] : false; }
void CEntities::SetLagCompensation(uint16_t iIndex, bool bLagComp) { if (iIndex < MAX_PLAYERS) m_aLagCompensation[iIndex] = bLagComp; }
Vec3* CEntities::GetAvgVelocity(uint16_t iIndex) { return iIndex < MAX_PLAYERS && iIndex != I::EngineClient->GetLocalPlayer() ? &m_aAvgVelocities[iIndex] : nullptr; }
void CEntities::SetAvgVelocity(uint16_t iIndex, Vec3 vAvgVelocity) { if (iIndex < MAX_PLAYERS) m_aAvgVelocities[iIndex] = vAvgVelocity; }
std::deque<VelFixRecord>* CEntities::GetOrigins(uint16_t iIndex) { return iIndex < MAX_PLAYERS ? &m_aOrigins[iIndex] : nullptr; }
uint32_t CEntities::GetModel(unsigned short iIndex) { return iIndex < MAX_EDICTS ? m_aModels[iIndex] : 0; }
DormantData* CEntities::GetDormancy(unsigned short iIndex) { return s_mDormancy.contains(iIndex) ? &s_mDormancy[iIndex] : nullptr; }

int CEntities::GetPriority(int iIndex, PriorityTypeEnum::PriorityTypeEnum eType) { return m_aIPriorities[eType][iIndex]; }
int CEntities::GetPriority(uint32_t uAccountID, PriorityTypeEnum::PriorityTypeEnum eType) { return m_aUPriorities[eType][uAccountID]; }
bool CEntities::IsFriend(int iIndex) { return m_mIFriends[iIndex]; }
bool CEntities::IsFriend(uint32_t uAccountID) { return m_mUFriends[uAccountID]; }
bool CEntities::InParty(int iIndex) { return iIndex != I::EngineClient->GetLocalPlayer() && m_mIParty[iIndex] == 1; }
bool CEntities::InParty(uint32_t uAccountID) { return uAccountID != m_uAccountID && m_mUParty[uAccountID] == 1; }
bool CEntities::IsF2P(int iIndex) { return m_mIF2P[iIndex]; }
bool CEntities::IsF2P(uint32_t uAccountID) { return m_mUF2P[uAccountID]; }
int CEntities::GetLevel(int iIndex) { return m_mILevels.contains(iIndex) ? m_mILevels[iIndex] : -2; }
int CEntities::GetLevel(uint32_t uAccountID) { return m_mULevels.contains(uAccountID) ? m_mULevels[uAccountID] : -2; }
int CEntities::GetParty(int iIndex) { return m_mIParty.contains(iIndex) ? m_mIParty[iIndex] : 0; }
int CEntities::GetParty(uint32_t uAccountID) { return m_mUParty.contains(uAccountID) ? m_mUParty[uAccountID] : 0; }
int CEntities::GetPartyCount() { return m_iPartyCount; }
uint32_t CEntities::GetLocalAccountID() { return m_uAccountID; }
bool CEntities::IsSpectated() { return m_bIsSpectated; }
