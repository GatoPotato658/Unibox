#include "PacketManip.h"

#include "../Visuals/FakeAngle/FakeAngle.h"
#include "../Ticks/Ticks.h"
#include "../Misc/Misc.h"

static inline bool AntiAimCheck(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	return F::AntiAim.YawOn() && F::AntiAim.ShouldRun(pLocal, pWeapon, pCmd) && !F::Ticks.m_bRecharge
		&& !F::Misc.IsDuckSpeedActive()
		&& I::ClientState->chokedcommands < F::AntiAim.AntiAimTicks();
}

void CPacketManip::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	F::FakeAngle.bDrawChams = Vars::Fakelag::Fakelag.Value || F::AntiAim.AntiAimOn();

	G::SendPacket = true;
	F::FakeLag.Run(pLocal, pWeapon, pCmd);
	if (AntiAimCheck(pLocal, pWeapon, pCmd))
		G::SendPacket = false;
}