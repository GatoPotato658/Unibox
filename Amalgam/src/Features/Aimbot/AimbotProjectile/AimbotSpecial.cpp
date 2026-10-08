#include "AimbotProjectile.h"

#include "../Aimbot.h"


namespace
{
	int GetEventTicks(float flDelay)
	{
		return int(flDelay / TICK_INTERVAL) + 1;
	}

	constexpr int kPressTicks = 2;
	constexpr int kTailTicks = 2;

	constexpr int kIdleSolveInterval = 4;
	constexpr int kFailMemoryTicks = 8;
	constexpr int kMaxSolveAttempts = 4;

	int GetSolveInterval(int iDelay, bool bWindup)
	{
		if (bWindup && iDelay <= 1)
			return 1;
		return iDelay <= 3 ? 2 : 4;
	}

	constexpr float kSpitMaxChannel = 5.f + 1.f;
	constexpr float kSpitOverloadStart = 3.5f;
	constexpr float kSpitMinHold = 0.5f;
	constexpr int kSpewBlobs = 4;
	constexpr float kSpewInterval = 0.175f;

	const char* GetOverlay(CTFPlayer* pLocal)
	{
		static int iOffset = U::NetVars.GetNetVar("CBasePlayer", "m_szScriptOverlayMaterial");
		return iOffset ? reinterpret_cast<const char*>(uintptr_t(pLocal) + iOffset) : "";
	}

	int GetClassSpecial(CTFPlayer* pLocal)
	{
		switch (pLocal->m_iClass())
		{
		case TF_CLASS_SNIPER: return ProjSpecialEnum::SniperSpit;
		case TF_CLASS_SPY: return ProjSpecialEnum::SpyEmp;
		case TF_CLASS_PYRO: return ProjSpecialEnum::PyroSpew;
		case TF_CLASS_HEAVY: return ProjSpecialEnum::HeavyRock;
		}
		return ProjSpecialEnum::None;
	}

	bool IsSpecialEnabled(int iSpecial)
	{
		const int iAbilities = Vars::Aimbot::Zombie::Abilities.Value;
		switch (iSpecial)
		{
		case ProjSpecialEnum::SniperSpit: return iAbilities & Vars::Aimbot::Zombie::AbilitiesEnum::SniperSpit;
		case ProjSpecialEnum::SpyEmp: return iAbilities & Vars::Aimbot::Zombie::AbilitiesEnum::SpyEmp;
		case ProjSpecialEnum::PyroSpew: return iAbilities & Vars::Aimbot::Zombie::AbilitiesEnum::PyroSpew;
		case ProjSpecialEnum::HeavyRock: return iAbilities & Vars::Aimbot::Zombie::AbilitiesEnum::HeavyRock;
		}
		return false;
	}
}

bool CAimbotProjectile::IsZombieMap()
{
	const char* sLevel = I::EngineClient->GetLevelName();
	if (!sLevel)
		return false;

	const char* sName = strrchr(sLevel, '/');
	return !strncmp(sName ? sName + 1 : sLevel, "zi_", 3);
}

bool CAimbotProjectile::IsAbilityReady(CTFPlayer* pLocal)
{
	const char* sOverlay = GetOverlay(pLocal);
	if (!strstr(sOverlay, "zability_"))
		return false;

	const size_t iLength = strlen(sOverlay);
	return iLength >= 7 && !strcmp(sOverlay + iLength - 7, "_on.vtf");
}

void CAimbotProjectile::HoldAngles(CUserCmd* pCmd, const Vec3& vAngles)
{
	Vec3 vAim = vAngles;
	Math::ClampAngles(vAim);

	switch (Vars::Aimbot::General::AimType.Value)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		pCmd->viewangles = vAim;
		I::EngineClient->SetViewAngles(vAim);
		break;
	default:
		SDK::FixMovement(pCmd, vAim);
		pCmd->viewangles = vAim;
		G::SilentAngles = true;
	}
}

bool CAimbotProjectile::SolveAbility(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iSpecial, int iDelayTicks, Target_t& tOut)
{
	struct Scope_t
	{
		Scope_t(int iSpecial) { F::ProjSim.m_pSpecial = F::ProjSim.GetSpecial(iSpecial); }
		~Scope_t() { F::ProjSim.m_pSpecial = nullptr; }
	} tScope(iSpecial);

	m_iWeaponID = pWeapon->GetWeaponID();
	m_iMethod = Vars::Aimbot::General::AimTypeEnum::Silent;
	m_iLaunchDelay = iDelayTicks;
	m_flSpecialDrag = F::ProjSim.GetSpecialDrag(F::ProjSim.m_pSpecial);
	m_vBestPlayerPath.clear();
	m_bBestPlayerPathSet = false;
	m_bBlockAimAnglesDraw = false;

	auto vTargets = GetAbilityTargets(pLocal, pWeapon);
	if (m_tZombie.m_iTargetEnt)
	{
		std::stable_partition(vTargets.begin(), vTargets.end(), [&](const Target_t& tTarget)
			{
				return tTarget.m_pEntity->entindex() == m_tZombie.m_iTargetEnt;
			});
	}

	const float flMaxDist = Vars::Aimbot::Zombie::MaxDistance.Value;
	const int iTick = I::GlobalVars->tickcount;
	bool bReturn = false;
	int iAttempts = 0;
	for (auto& tTarget : vTargets)
	{
		if (flMaxDist && tTarget.m_flDistTo > flMaxDist * flMaxDist)
			continue;

		const int iEntity = tTarget.m_pEntity->entindex();
		if (iEntity != m_tZombie.m_iTargetEnt)
		{
			auto it = m_tZombie.m_mFailTick.find(iEntity);
			if (it != m_tZombie.m_mFailTick.end() && iTick - it->second < kFailMemoryTicks)
				continue;
			if (iAttempts >= kMaxSolveAttempts)
				break;
		}
		iAttempts++;

		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();
		if (CanHit(tTarget, pLocal, pWeapon, true) != 1)
		{
			m_tZombie.m_mFailTick[iEntity] = iTick;
			continue;
		}

		m_tZombie.m_mFailTick.erase(iEntity);
		tOut = tTarget;
		bReturn = true;
		break;
	}

	m_iLaunchDelay = 0;
	return bReturn;
}

bool CAimbotProjectile::RunZombieAbility(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	auto& tState = m_tZombie;
	if (!pWeapon || pLocal->m_iTeamNum() != TF_TEAM_BLUE || !Vars::Aimbot::Zombie::Abilities.Value || !Vars::Aimbot::General::AimType.Value
		|| !pLocal->IsAlive() || pLocal->IsAGhost() || !IsZombieMap())
	{
		tState.Reset();
		return false;
	}

	const int iTick = I::GlobalVars->tickcount;
	const bool bAuto = Vars::Aimbot::Zombie::AutoCast.Value;
	const bool bUserHolding = G::OriginalCmd.buttons & IN_ATTACK2;

	if (tState.m_iPhase != ZombiePhaseEnum::Idle
		&& (tState.m_iSpecial != GetClassSpecial(pLocal) || iTick - tState.m_iStartTick > GetEventTicks(kSpitMaxChannel) || iTick < tState.m_iStartTick))
		tState.Reset();

	if (tState.m_iPhase == ZombiePhaseEnum::Idle)
	{
		const int iSpecial = GetClassSpecial(pLocal);
		if (!IsSpecialEnabled(iSpecial) || !IsAbilityReady(pLocal) || (!bAuto && !bUserHolding) || iTick < tState.m_iNextIdleTick)
			return false;

		int iDelay = 0, iEnd = 0;
		switch (iSpecial)
		{
		case ProjSpecialEnum::SniperSpit:
			iDelay = GetEventTicks(kSpitMinHold);
			break;
		case ProjSpecialEnum::HeavyRock:
			iDelay = iEnd = GetEventTicks(F::ProjSim.GetSpecial(iSpecial)->m_flLaunchDelay);
			break;
		case ProjSpecialEnum::SpyEmp:
			iDelay = iEnd = GetEventTicks(0.f);
			break;
		case ProjSpecialEnum::PyroSpew:
			iEnd = (kSpewBlobs - 1) * GetEventTicks(kSpewInterval);
			break;
		}

		tState.m_iTargetEnt = 0;
		Target_t tTarget;
		if (!SolveAbility(pLocal, pWeapon, iSpecial, iDelay, tTarget))
		{
			tState.m_iNextIdleTick = iTick + kIdleSolveInterval;
			return false;
		}

		tState.m_iSpecial = iSpecial;
		tState.m_iStartTick = iTick;
		tState.m_iLastSolveTick = iTick;
		tState.m_iEndTick = iTick + iEnd;
		tState.m_iTargetEnt = tTarget.m_pEntity->entindex();
		tState.m_iPhase = iSpecial == ProjSpecialEnum::SniperSpit ? ZombiePhaseEnum::Charge
			: iSpecial == ProjSpecialEnum::PyroSpew ? ZombiePhaseEnum::Burst : ZombiePhaseEnum::Windup;
		tState.m_vAngle = tTarget.m_vAngleTo;
		tState.m_bHasAngle = true;

		ShowTarget(1, tTarget);
		HoldAngles(pCmd, tState.m_vAngle);
		if (bAuto)
			pCmd->buttons |= IN_ATTACK2;
		F::Aimbot.m_eRanType = EWeaponType::PROJECTILE;
		return true;
	}

	const int iElapsed = iTick - tState.m_iStartTick;
	bool bPress = bAuto && iElapsed < kPressTicks;
	int iDelay = 0;

	switch (tState.m_iPhase)
	{
	case ZombiePhaseEnum::Charge:
	{
		bPress = bAuto;
		iDelay = std::max(GetEventTicks(kSpitMinHold) - iElapsed, 0);

		bool bSolved = false;
		if (iTick - tState.m_iLastSolveTick >= GetSolveInterval(iDelay, false))
		{
			tState.m_iLastSolveTick = iTick;
			Target_t tTarget;
			bSolved = SolveAbility(pLocal, pWeapon, tState.m_iSpecial, iDelay, tTarget);
			if (bSolved)
			{
				tState.m_iTargetEnt = tTarget.m_pEntity->entindex();
				tState.m_vAngle = tTarget.m_vAngleTo;
				ShowTarget(1, tTarget);
			}
		}

		const bool bRelease = bAuto
			? !iDelay && bSolved || iElapsed >= GetEventTicks(kSpitOverloadStart) - 1
			: !bUserHolding;
		if (bRelease)
		{
			tState.m_iPhase = ZombiePhaseEnum::Tail;
			tState.m_iEndTick = iTick + kTailTicks;
			bPress = false;
		}
		break;
	}
	case ZombiePhaseEnum::Windup:
	case ZombiePhaseEnum::Burst:
	{
		iDelay = tState.m_iPhase == ZombiePhaseEnum::Windup ? std::max(tState.m_iEndTick - iTick, 0) : 0;

		if (iTick - tState.m_iLastSolveTick >= GetSolveInterval(iDelay, tState.m_iPhase == ZombiePhaseEnum::Windup))
		{
			tState.m_iLastSolveTick = iTick;
			Target_t tTarget;
			if (SolveAbility(pLocal, pWeapon, tState.m_iSpecial, iDelay, tTarget))
			{
				tState.m_iTargetEnt = tTarget.m_pEntity->entindex();
				tState.m_vAngle = tTarget.m_vAngleTo;
				ShowTarget(1, tTarget);
			}
		}

		if (iTick >= tState.m_iEndTick)
		{
			tState.m_iPhase = ZombiePhaseEnum::Tail;
			tState.m_iEndTick = iTick + kTailTicks;
		}
		break;
	}
	case ZombiePhaseEnum::Tail:
		if (iTick >= tState.m_iEndTick)
		{
			tState.Reset();
			return false;
		}
		break;
	}

	if (tState.m_bHasAngle)
		HoldAngles(pCmd, tState.m_vAngle);
	if (bPress)
		pCmd->buttons |= IN_ATTACK2;
	else if (bAuto)
		pCmd->buttons &= ~IN_ATTACK2;
	F::Aimbot.m_eRanType = EWeaponType::PROJECTILE;
	return true;
}
