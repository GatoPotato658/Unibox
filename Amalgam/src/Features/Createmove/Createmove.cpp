#include "Createmove.h"

#include "../Aimbot/Aimbot.h"
#include "../Backtrack/Backtrack.h"
#include "../CritHack/CritHack.h"
#include "../EnginePrediction/EnginePrediction.h"
#include "../Misc/Misc.h"
#include "../NoSpread/NoSpread.h"
#include "../NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../PacketManip/PacketManip.h"
#include "../Resolver/Resolver.h"
#include "../Ticks/Ticks.h"
#include "../Visuals/Visuals.h"
#include "../Visuals/FakeAngle/FakeAngle.h"
#include "../Spectate/Spectate.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../NavBot/NavEngine/Controllers/Controller.h"
#include "../NavBot/NavBotCore.h"
#include "../NavBot/NavEngine/NavEngine.h"
#include "../FollowBot/FollowBot.h"
#include "../AutoJoin/AutoJoin.h"
#include "../Misc/AutoItem/AutoItem.h"
#include "../Misc/AutoVote/AutoVote.h"

MAKE_SIGNATURE(IHasGenericMeter_GetMeterMultiplier, "client.dll", "F3 0F 10 81 ? ? ? ? C3 CC CC CC CC CC CC CC 48 85 D2", 0x0);
MAKE_SIGNATURE(C_BaseAnimating_AutoAllowBoneAccess, "client.dll", "40 53 48 83 EC ? 41 0F B6 C0 44 0F B6 CA", 0x0);
MAKE_SIGNATURE(C_BaseAnimating_AutoAllowBoneAccessOnDelete, "client.dll", "B9 ? ? ? ? E9 ? ? ? ? CC CC CC CC CC CC 48 89 5C 24", 0x0);

void CCreateMove::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	G::PSilentAngles = G::SilentAngles = G::Attacking = G::Throwing = false;
	G::LastUserCmd = G::CurrentUserCmd ? G::CurrentUserCmd : pCmd;
	G::CurrentUserCmd = pCmd;
	G::OriginalCmd = *pCmd;

	if (!pWeapon)
		return;

	SDK::CanAttack(pLocal, pWeapon, pCmd, G::CanPrimaryAttack, G::CanSecondaryAttack, G::Reloading);
	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd);
	G::PrimaryWeaponType = SDK::GetWeaponType(pWeapon, &G::SecondaryWeaponType);
	G::CanHeadshot = pWeapon->CanHeadshot() || pWeapon->AmbassadorCanHeadshot(TICKS_TO_TIME(pLocal->m_nTickBase()));
}

void CCreateMove::Run(int nSequenceNum, float flInputSampleFrametime)
{
	{
		char autoallow[16];
		S::C_BaseAnimating_AutoAllowBoneAccess.Call<void>(autoallow, true, false);
		I::MDLCache->BeginLock();
		I::Input->CreateMove(nSequenceNum, flInputSampleFrametime, !I::ClientState->IsPaused());
		I::MDLCache->EndLock();
		S::C_BaseAnimating_AutoAllowBoneAccessOnDelete.Call<void>(autoallow);
	}

#ifdef DEBUG_HOOKS
	if (!Vars::Hooks::CHLClient_CreateMove[DEFAULT_BIND])
		return;
#endif

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	auto pWeapon = H::Entities.GetWeapon();
	CUserCmd* pCmd = &I::Input->m_pCommands[nSequenceNum % MULTIPLAYER_BACKUP];
	I::Prediction->Update(I::ClientState->m_nDeltaTick, I::ClientState->m_nDeltaTick > 0, I::ClientState->last_command_ack, I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands);

	UpdateInfo(pLocal, pWeapon, pCmd);
#ifndef TEXTMODE
	F::Spectate.CreateMove(pCmd);
#endif
	F::Misc.RunPre(pLocal, pCmd);
	F::AutoJoin.Run(pLocal);
	F::AutoItem.Run(pLocal);
	SDK::RefreshTriggerStorage();
	F::GameObjectiveController.Update();
	F::BotUtils.Run(pLocal, pWeapon, pCmd);
	F::AutoVote.Run(pLocal);
	F::Backtrack.CreateMove(pLocal, pWeapon, pCmd);

	F::Ticks.Start(pLocal, pCmd);
	{
		F::Aimbot.Run(pLocal, pWeapon, pCmd);
	}
	F::Ticks.End(pLocal, pCmd);
	{
		F::FollowBot.Run(pLocal, pCmd);
		F::NavBotCore.Run(pLocal, pWeapon, pCmd);
		F::NavEngine.Run(pLocal, pWeapon, pCmd);
		F::BotUtils.HandleSmartJump(pLocal, pCmd);
		F::CritHack.Run(pLocal, pWeapon, pCmd);
		F::NoSpread.Run(pLocal, pWeapon, pCmd);
		F::Misc.RunPost(pLocal, pCmd);
		F::Misc.AutoFaNJump(pLocal, pWeapon, pCmd);
		F::PacketManip.Run(pLocal, pWeapon, pCmd);
		F::Ticks.CreateMove(pLocal, pWeapon, pCmd);
		F::AntiAim.Run(pLocal, pWeapon, pCmd);
		F::AntiCheatCompatibility.CreateMove(pCmd);
		
#ifndef TEXTMODE
		F::Visuals.CreateMove(pLocal, pWeapon, pCmd);
		F::Visuals.LocalAnimations(pLocal, pWeapon, pCmd);
#endif
	}
	F::EnginePrediction.End(pLocal, pCmd);
		F::Resolver.CreateMove();
		F::NoSpreadHitscan.AskForPlayerPerf();
	G::Choking = !G::SendPacket, G::LastUserCmd = pCmd;
}