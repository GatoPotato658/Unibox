#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"
#include "../Features/CheatDetection/CheatDetection.h"
#include "../Features/NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Features/Resolver/Resolver.h"


MAKE_HOOK(FX_FireBullets, S::FX_FireBullets(), void,
	CTFWeaponBase* pWpn, int iPlayer, const Vec3& vecOrigin, const Vec3& vecAngles, int iWeapon, int iMode, int iSeed, float flSpread, float flDamage, bool bCritical)
{
	DEBUG_RETURN(FX_FireBullets, pWpn, iPlayer, vecOrigin, vecAngles, iWeapon, iMode, iSeed, flSpread, flDamage, bCritical);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFWeaponBaseGun_FireBullet_FireBullets_Call();

	if (iPlayer != I::EngineClient->GetLocalPlayer())
	{
		F::Backtrack.ReportShot(iPlayer);
		F::CheatDetection.OnFireBullets(iPlayer);
	}
	else if (Vars::Aimbot::General::NoSpread.Value && !F::NoSpreadHitscan.UsesClientSeed() && dwRetAddr == dwDesired)
		iSeed = F::NoSpreadHitscan.m_iSeed;

	return CALL_ORIGINAL(pWpn, iPlayer, vecOrigin, vecAngles, iWeapon, iMode, iSeed, flSpread, flDamage, bCritical);
}

#ifdef SEEDPRED_DEBUG

MAKE_HOOK(FX_FireBullets_Server, S::FX_FireBullets_Server(), void,
	CTFWeaponBase* pWpn, int iPlayer, const Vec3& vecOrigin, const Vec3& vecAngles, int iWeapon, int iMode, int iSeed, float flSpread, float flDamage, bool bCritical)
{
	DEBUG_RETURN(FX_FireBullets_Server, pWpn, iPlayer, vecOrigin, vecAngles, iWeapon, iMode, iSeed, flSpread, flDamage, bCritical);

	if (Vars::Aimbot::General::NoSpread.Value)
		SDK::Output("FX_FireBullets", std::format("{}", iSeed).c_str(), { 0, 255, 0 });
	return CALL_ORIGINAL(pWpn, iPlayer, vecOrigin, vecAngles, iWeapon, iMode, iSeed, flSpread, flDamage, bCritical);
}

MAKE_HOOK(CBasePlayer_ProcessUsercmds, S::CBasePlayer_ProcessUsercmds(), void,
	void* rcx, CUserCmd* cmds, int numcmds, int totalcmds, int dropped_packets, bool paused)
{
	DEBUG_RETURN(CBasePlayer_ProcessUsercmds, rcx, cmds, numcmds, totalcmds, dropped_packets, paused);

	bool bInAttack = false;
	for (int i = totalcmds - 1; i >= 0; i--)
	{
		CUserCmd* pCmd = &cmds[totalcmds - 1 - i];
		if (pCmd && pCmd->buttons & IN_ATTACK)
			bInAttack = true;
	}
	if (bInAttack)
	{
		double dFloatTime = SDK::PlatFloatTime();
		float flTime = float(SDK::PlatFloatTime() * 1000.0);
		int iSeed = *reinterpret_cast<int*>((char*)&flTime) & 255;
		SDK::Output("ProcessUsercmds", std::format("{}: {}", dFloatTime, iSeed).c_str(), { 0, 255, 0, 100 });
	}

	return CALL_ORIGINAL(rcx, cmds, numcmds, totalcmds, dropped_packets, paused);
}
#endif