#include "Ticks.h"

#include "../PacketManip/AntiAim/AntiAim.h"
#include "../EnginePrediction/EnginePrediction.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../Backtrack/Backtrack.h"
#include "../Createmove/Createmove.h"

MAKE_SIGNATURE(Host_ShouldRun, "engine.dll", "48 83 EC ? 48 8B 05 ? ? ? ? 83 78 ? ? 74 ? 48 8B 05", 0x0);
MAKE_SIGNATURE(net_time, "engine.dll", "F2 0F 10 05 ? ? ? ? 66 0F 2F 05 ? ? ? ? 72", 0x0);
MAKE_SIGNATURE(host_frametime_unbounded, "engine.dll", "F3 0F 10 05 ? ? ? ? F3 0F 11 45 ? F3 0F 11 4D ? 89 45", 0x0);
MAKE_SIGNATURE(host_frametime_stddeviation, "engine.dll", "F3 0F 10 0D ? ? ? ? 48 8D 54 24 ? 0F 57 C0 48 89 44 24 ? 8B 05", 0x0);
MAKE_SIGNATURE(Con_NXPrintf, "engine.dll", "48 89 54 24 ? 4C 89 44 24 ? 4C 89 4C 24 ? 53 57 B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 48 8B D9", 0x0);

void CTicks::Reset()
{
	m_bSpeedhack = m_bDoubletap = m_bRecharge = m_bWarp = false;
	m_iShiftedTicks = m_iShiftedGoal = 0;
}

void CTicks::Recharge(CTFPlayer* pLocal)
{
	if (!m_bGoalReached)
		return;

	bool bPassive = m_bRecharge = false;

	static float flPassiveTime = 0.f;
	flPassiveTime = std::max(flPassiveTime - TICK_INTERVAL, -TICK_INTERVAL);
	if (Vars::Doubletap::PassiveRecharge.Value && 0.f >= flPassiveTime)
	{
		bPassive = true;
		flPassiveTime += 1.f / Vars::Doubletap::PassiveRecharge.Value;
	}

	if (m_iDeficit)
	{
		bPassive = true;
		m_iDeficit--, m_iShiftedTicks--;
	}

	if (!Vars::Doubletap::RechargeTicks.Value && !bPassive && !m_bRechargeQueue
		|| m_bDoubletap || m_bWarp || m_iShiftedTicks == m_iMaxShift || m_bSpeedhack)
		return;

	m_bRecharge = true;
	m_bRechargeQueue = false;
	m_iShiftedGoal = m_iShiftedTicks + 1;
}

void CTicks::Warp()
{
	if (!m_bGoalReached)
		return;

	m_bWarp = false;
	if (!Vars::Doubletap::Warp.Value
		|| !m_iShiftedTicks || m_bDoubletap || m_bRecharge || m_bSpeedhack)
		return;

	m_bWarp = true;
	m_iShiftedGoal = std::max(m_iShiftedTicks - Vars::Doubletap::WarpRate.Value + 1, 0);
}

void CTicks::Doubletap(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!m_bGoalReached)
		return;

	if (!Vars::Doubletap::Doubletap.Value
		|| m_iWait || m_bWarp || m_bRecharge || m_bSpeedhack)
		return;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	auto pWeapon = H::Entities.GetWeapon();
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		return;

	bool bAttacking = G::PrimaryWeaponType == EWeaponType::MELEE ? pCmd->buttons & IN_ATTACK : G::Attacking;
	if (!G::CanPrimaryAttack && !G::Reloading || !bAttacking && !m_bDoubletap || F::AutoRocketJump.IsRunning())
		return;

	m_bDoubletap = true;
	m_iShiftedGoal = std::max(m_iShiftedTicks - Vars::Doubletap::TickLimit.Value + 1, 0);
	if (Vars::Doubletap::AntiWarp.Value)
		m_bAntiWarp = pLocal->m_hGroundEntity();
}

void CTicks::Speedhack()
{
	m_bSpeedhack = Vars::Speedhack::Scale.Value != 1;
	if (!m_bSpeedhack)
		return;

	m_bDoubletap = m_bWarp = m_bRecharge = false;
}

static Vec3 s_vVelocity = {};
static int s_iMaxTicks = 0;
void CTicks::AntiWarp(CTFPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove, int iTicks)
{
	s_iMaxTicks = std::max(iTicks + 1, s_iMaxTicks);

	Vec3 vAngles; Math::VectorAngles(s_vVelocity, vAngles);
	vAngles.y = flYaw - vAngles.y;
	Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
	vForward *= s_vVelocity.Length2D();

	if (iTicks > std::max(s_iMaxTicks - 8, 3))
		flForwardMove = -vForward.x, flSideMove = -vForward.y;
	else if (iTicks > 3)
		flForwardMove = flSideMove = 0.f;
	else
		flForwardMove = vForward.x, flSideMove = vForward.y;
}
void CTicks::AntiWarp(CTFPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove)
{
	AntiWarp(pLocal, flYaw, flForwardMove, flSideMove, GetTicks());
}
void CTicks::AntiWarp(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bAntiWarp)
		AntiWarp(pLocal, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
	else
	{
		s_vVelocity = pLocal->m_vecVelocity();
		s_iMaxTicks = 0;
	}
}

bool CTicks::ValidWeapon(CTFWeaponBase* pWeapon)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_PDA_SPY_BUILD:
	case TF_WEAPON_BUILDER:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_GRAPPLINGHOOK:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_ROCKETPACK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
		return false;
	}

	return true;
}

void CTicks::SendMoveFunc()
{
	CLC_Move moveMsg;
	byte data[4000];
	moveMsg.m_DataOut.StartWriting(data, sizeof(data));

	int nCommands = 1 + I::ClientState->chokedcommands;
	moveMsg.m_nNewCommands = std::clamp(nCommands, 0, MAX_NEW_COMMANDS);
	int nExtraCommands = nCommands - moveMsg.m_nNewCommands;
	moveMsg.m_nBackupCommands = std::clamp(nExtraCommands, 2, MAX_BACKUP_COMMANDS);

	int nNumCmds = moveMsg.m_nNewCommands + moveMsg.m_nBackupCommands;

	if (!m_bSpeedhack)
	{
		const int iAllowedNewCommands = std::max(m_iMaxUsrCmdProcessTicks - m_iShiftedTicks, 0);
		const int iCmdCount = nNumCmds - 3;
		if (iCmdCount > iAllowedNewCommands)
		{
			SDK::Output("clc_Move", std::format("{:d} sent <{:d} | {:d}>, max was {:d}.", iCmdCount + 3, moveMsg.m_nNewCommands, moveMsg.m_nBackupCommands, iAllowedNewCommands).c_str(), { 255, 0, 0, 255 });
			m_iDeficit = iCmdCount - iAllowedNewCommands;
		}
	}

	bool bOk = true;
	{
		const int nNextCommandNr = I::ClientState->lastoutgoingcommand + nCommands;
		for (int nFrom = -1, nTo = nNextCommandNr - nNumCmds + 1; nTo <= nNextCommandNr; nTo++)
		{
			const bool bIsNewCmd = nTo >= nNextCommandNr - moveMsg.m_nNewCommands + 1;
			bOk = bOk && I::Client->WriteUsercmdDeltaToBuffer(&moveMsg.m_DataOut, nFrom, nTo, bIsNewCmd);
			nFrom = nTo;
		}
	}

	if (bOk)
	{
		if (nExtraCommands)
			I::ClientState->m_NetChannel->m_nChokedPackets -= nExtraCommands;

		I::ClientState->m_NetChannel->SendNetMsg(moveMsg);
	}
}

void CTicks::MoveFunc(float accumulated_extra_samples, bool bFinalTick)
{
	m_iShiftedTicks--;
	if (m_iWait > 0)
		m_iWait--;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	auto pWeapon = H::Entities.GetWeapon();
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		m_iWait = -1;

	m_bGoalReached = bFinalTick && m_iShiftedTicks == m_iShiftedGoal;

	if (I::ClientState->m_nSignonState < SIGNONSTATE_CONNECTED || !S::Host_ShouldRun.Call<bool>())
		return;

	G::SendPacket = true;

	if (I::DemoPlayer->IsPlayingBack())
	{
		if (!I::ClientState->ishltv && !I::ClientState->isreplay)
			return;

		G::SendPacket = false;
	}

	static auto host_limitlocal = H::ConVars.FindVar("host_limitlocal");
	double net_time = *reinterpret_cast<double*>(U::Memory.RelToAbs(S::net_time(), 4));
	if ((!I::ClientState->m_NetChannel->IsLoopback() || host_limitlocal->GetInt()) &&
		(net_time < I::ClientState->m_flNextCmdTime || !I::ClientState->m_NetChannel->CanPacket() || !bFinalTick))
		G::SendPacket = false;

	if (I::ClientState->m_nSignonState == SIGNONSTATE_FULL)
	{
		int nNextCommandNr = I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands + 1;
		F::CreateMove.Run(nNextCommandNr, TICK_INTERVAL - accumulated_extra_samples);

		if (I::DemoRecorder->IsRecording())
			I::DemoRecorder->RecordUserInput(nNextCommandNr);

		if (!G::SendPacket)
		{
			I::ClientState->m_NetChannel->SetChoked();
			I::ClientState->chokedcommands++;
		}
		else
			SendMoveFunc();
	}

	if (!G::SendPacket)
		return;

	if (I::ClientState->m_nSignonState == SIGNONSTATE_FULL)
	{
		if (I::ClientState->m_NetChannel->IsTimingOut() && !I::DemoPlayer->IsPlayingBack())
		{
			struct con_nprint_s
			{
				int		index;			// Row #
				float	time_to_live;	// # of seconds before it disappears. -1 means to display for 1 frame then go away.
				float	color[3];		// RGB colors ( 0.0 -> 1.0 scale )
				bool	fixed_width_font;
			} np;

			np.time_to_live = 1.f;
			np.index = 2;
			np.fixed_width_font = false;
			np.color[0] = 1.f;
			np.color[1] = 0.2f;
			np.color[2] = 0.2f;

			float flTimeOut = I::ClientState->m_NetChannel->GetTimeoutSeconds();
			float flRemainingTime = flTimeOut - I::ClientState->m_NetChannel->GetTimeSinceLastReceived();
			S::Con_NXPrintf.Call<void>(&np, "WARNING:  Connection Problem"); np.index = 3;
			S::Con_NXPrintf.Call<void>(&np, "Auto-disconnect in %.1f seconds", flRemainingTime);

			I::ClientState->ForceFullUpdate();
		}

		float host_frametime_unbounded = *reinterpret_cast<float*>(U::Memory.RelToAbs(S::host_frametime_unbounded(), 4));
		float host_frametime_stddeviation = *reinterpret_cast<float*>(U::Memory.RelToAbs(S::host_frametime_stddeviation(), 4));

		NET_Tick tickMsg(I::ClientState->m_nDeltaTick, host_frametime_unbounded, host_frametime_stddeviation);
		I::ClientState->m_NetChannel->SendNetMsg(tickMsg);
	}

	I::ClientState->lastoutgoingcommand = I::ClientState->m_NetChannel->SendDatagram(NULL);
	I::ClientState->chokedcommands = 0;

	static auto cl_cmdrate = H::ConVars.FindVar("cl_cmdrate");
	if (I::ClientState->m_nSignonState == SIGNONSTATE_FULL)
	{
		float flCommandInterval = 1.0f / cl_cmdrate->GetFloat();
		float flMaxDelta = std::min(TICK_INTERVAL, flCommandInterval);
		float flDelta = std::clamp((float)(net_time - I::ClientState->m_flNextCmdTime), 0.0f, flMaxDelta);
		I::ClientState->m_flNextCmdTime = net_time + flCommandInterval - flDelta;
	}
	else
		I::ClientState->m_flNextCmdTime = net_time + 0.2f;
}

void CTicks::Move(float accumulated_extra_samples, bool bFinalTick)
{
	MoveManage();

	while (m_iShiftedTicks > m_iMaxShift)
		MoveFunc(accumulated_extra_samples, false);
	m_iShiftedTicks = std::max(m_iShiftedTicks, 0) + 1;

	if (m_bSpeedhack)
	{
		m_iShiftedTicks = Vars::Speedhack::Scale.Value;
		m_iShiftedGoal = 0;
	}

	m_iShiftedGoal = std::clamp(m_iShiftedGoal, 0, m_iMaxShift);
	if (m_iShiftedTicks > m_iShiftedGoal) // normal use/doubletap/teleport
	{
		m_iShiftStart = m_iShiftedTicks - 1;
		m_bShifted = false;

		while (m_iShiftedTicks > m_iShiftedGoal)
		{
			m_bShifting = m_bShifted |= m_iShiftedTicks - 1 != m_iShiftedGoal;
			MoveFunc(accumulated_extra_samples, m_iShiftedTicks - 1 == m_iShiftedGoal);
		}

		m_bShifting = m_bAntiWarp = m_bTimingUnsure = false;
		if (m_bWarp)
			m_iDeficit = 0;

		m_bDoubletap = m_bWarp = false;
	}
	else // else recharge, run once if we have any choked ticks
	{
		if (I::ClientState->chokedcommands)
			MoveFunc(accumulated_extra_samples, bFinalTick);
	}
}

void CTicks::MoveManage()
{
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	Recharge(pLocal);
	Warp();
	Speedhack();

	if (!m_bRecharge)
		m_iWait = std::max(m_iWait, 0);
	if (auto pWeapon = H::Entities.GetWeapon())
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_PIPEBOMBLAUNCHER:
		case TF_WEAPON_CANNON:
			if (!G::CanSecondaryAttack)
				m_iWait = Vars::Doubletap::TickLimit.Value;
			break;
		default:
			if (!ValidWeapon(pWeapon))
				m_iWait = -1;
			else if (G::Attacking || !G::CanPrimaryAttack && !G::Reloading)
				m_iWait = Vars::Doubletap::TickLimit.Value;
		}
	}
	else
		m_iWait = -1;

	static auto sv_maxusrcmdprocessticks = H::ConVars.FindVar("sv_maxusrcmdprocessticks");
	m_iMaxUsrCmdProcessTicks = sv_maxusrcmdprocessticks->GetInt();
	if (F::AntiCheatCompatibility.Active())
		m_iMaxUsrCmdProcessTicks = std::min(m_iMaxUsrCmdProcessTicks, 8);
	m_iMaxShift = m_iMaxUsrCmdProcessTicks - std::max(m_iMaxUsrCmdProcessTicks - Vars::Doubletap::RechargeLimit.Value, 0) - (F::AntiAim.YawOn() ? F::AntiAim.AntiAimTicks() : 0);
	m_iMaxShift = std::max(m_iMaxShift, 1);
}

void CTicks::CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	Doubletap(pLocal, pCmd);
	AntiWarp(pLocal, pCmd);
	ManagePacket(pCmd);

	SaveShootPos(pLocal);
	SaveShootAngle(pCmd);

	if (m_bDoubletap && m_iShiftedTicks == m_iShiftStart && pWeapon && pWeapon->IsInReload())
		m_bTimingUnsure = true;
}

void CTicks::ManagePacket(CUserCmd* pCmd)
{
	if (!m_bDoubletap && !m_bWarp && !m_bSpeedhack)
	{
		static bool bWasSet = false;
		bool bCanChoke = CanChoke(true); // failsafe
		if (G::PSilentAngles && bCanChoke)
			G::SendPacket = false, bWasSet = true;
		else if (bWasSet || !bCanChoke)
			G::SendPacket = true, bWasSet = false;

		bool bShouldShift = m_iShiftedTicks && m_iShiftedTicks + I::ClientState->chokedcommands >= m_iMaxUsrCmdProcessTicks;
		if (!G::SendPacket && bShouldShift)
			m_iShiftedGoal = std::max(m_iShiftedGoal - 1, 0);
	}
	else
	{
		if ((m_bSpeedhack || m_bWarp) && G::Attacking == 1)
		{
			G::SendPacket = true;
			return;
		}

		G::SendPacket = m_iShiftedGoal == m_iShiftedTicks;
		if (I::ClientState->chokedcommands >= 21) // prevent overchoking
			G::SendPacket = true;
	}
}

void CTicks::Start(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	Vec2 vOriginalMove; int iOriginalButtons;
	if (m_bPredictAntiwarp = m_bAntiWarp || GetTicks(H::Entities.GetWeapon()) && Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity())
	{
		vOriginalMove = { pCmd->forwardmove, pCmd->sidemove };
		iOriginalButtons = pCmd->buttons;

		AntiWarp(pLocal, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
	}

	F::EnginePrediction.Start(pLocal, pCmd);

	if (m_bPredictAntiwarp)
	{
		pCmd->forwardmove = vOriginalMove.x, pCmd->sidemove = vOriginalMove.y;
		pCmd->buttons = iOriginalButtons;
	}
}

void CTicks::End(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bPredictAntiwarp && !m_bAntiWarp && !G::Attacking)
	{
		F::EnginePrediction.End(pLocal, pCmd);
		F::EnginePrediction.Start(pLocal, pCmd);
	}
}

bool CTicks::CanChoke(bool bCanShift, int iMaxTicks)
{
	bool bCanChoke = I::ClientState->chokedcommands < 21;
	if (bCanChoke && !bCanShift)
		bCanChoke = m_iShiftedTicks + I::ClientState->chokedcommands < iMaxTicks;
	return bCanChoke;
}
bool CTicks::CanChoke(bool bCanShift)
{
	return CanChoke(bCanShift, m_iMaxUsrCmdProcessTicks);
}

int CTicks::GetTicks(CTFWeaponBase* pWeapon)
{
	if (m_bDoubletap && m_iShiftedGoal < m_iShiftedTicks)
		return m_iShiftedTicks - m_iShiftedGoal;

	if (!Vars::Doubletap::Doubletap.Value
		|| m_iWait || m_bWarp || m_bRecharge || m_bSpeedhack || F::AutoRocketJump.IsRunning())
		return 0;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		return 0;
	
	return std::min(Vars::Doubletap::TickLimit.Value - 1, m_iMaxShift);
}

int CTicks::GetShotsWithinPacket(CTFWeaponBase* pWeapon, int iTicks)
{
	iTicks = std::min(m_iMaxShift + 1, iTicks);

	int iDelay = 1;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		iDelay = 2;
	}

	return 1 + (iTicks - iDelay) / std::ceilf(pWeapon->GetFireRate() / TICK_INTERVAL);
}

int CTicks::GetMinimumTicksNeeded(CTFWeaponBase* pWeapon)
{
	int iDelay = 1;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		iDelay = 2;
	}

	return (GetShotsWithinPacket(pWeapon) - 1) * std::ceilf(pWeapon->GetFireRate() / TICK_INTERVAL) + iDelay;
}

void CTicks::SaveShootPos(CTFPlayer* pLocal)
{
	if (m_iShiftedTicks == m_iShiftStart)
		m_vShootPos = pLocal->GetShootPos();
}
Vec3 CTicks::GetShootPos()
{
	return m_vShootPos;
}

void CTicks::SaveShootAngle(CUserCmd* pCmd)
{
	static auto sv_maxusrcmdprocessticks_holdaim = H::ConVars.FindVar("sv_maxusrcmdprocessticks_holdaim");

	if (G::SendPacket)
		m_vShootAngle = std::nullopt;
	else if (!m_vShootAngle && G::Attacking == 1 && sv_maxusrcmdprocessticks_holdaim->GetBool())
		m_vShootAngle = pCmd->viewangles;
}
Vec3* CTicks::GetShootAngle()
{
	if (m_vShootAngle && I::ClientState->chokedcommands)
		return &m_vShootAngle.value();
	return nullptr;
}

bool CTicks::IsTimingUnsure()
{	// actually knowing when we'll shoot would be better than this, but this is fine for now
	return m_bTimingUnsure || m_bSpeedhack /*|| m_bWarp*/;
}

void CTicks::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ticks) || !pLocal->IsAlive())
		return;

	const DragBox_t dtPos = Vars::Menu::TicksDisplay.Value;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);

	if (m_bSpeedhack)
		return H::Draw.StringOutlined(fFont, dtPos.x, dtPos.y + 2, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, ALIGN_TOP, std::format("Speedhack x{}", Vars::Speedhack::Scale.Value).c_str());;

	int iAntiAimTicks = F::AntiAim.YawOn() ? F::AntiAim.AntiAimTicks() : 0;
	int iTicks = std::clamp(m_iShiftedTicks + std::max(I::ClientState->chokedcommands - iAntiAimTicks, 0), 0, m_iMaxUsrCmdProcessTicks);
	int iMax = std::max(m_iMaxUsrCmdProcessTicks - iAntiAimTicks, 0);

	int boxWidth = 180;
	int boxHeight = 29;
	int barHeight = 3;
	int textBoxHeight = boxHeight - barHeight;

	int x = dtPos.x - boxWidth / 2;
	int y = dtPos.y;

	Color_t bgColor = { 0, 0, 0, 180 };
	H::Draw.GradientRect(x, y, boxWidth, textBoxHeight, bgColor, bgColor, true);
	H::Draw.GradientRect(x, y + textBoxHeight, boxWidth, barHeight, bgColor, bgColor, true);

	static float currentProgress = 0.0f;
	float targetProgress = float(iTicks) / iMax;
	currentProgress = std::lerp(currentProgress, targetProgress, I::GlobalVars->frametime * 10.0f);

	int barWidth = static_cast<int>(boxWidth * currentProgress);
	if (barWidth > 0)
	{
		Color_t barColor = m_iWait ? Color_t{ 255, 150, 0, 255 } : Color_t{ 0, 255, 100, 255 };
		H::Draw.GradientRect(x, y + textBoxHeight, barWidth, barHeight, barColor, barColor, true);
	}

	std::string leftText = "Ticks";
	std::string rightText = std::format("{} / {}", iTicks, iMax);
	Color_t textColor = Vars::Menu::Theme::Active.Value;

	H::Draw.String(fFont, x + 5, y + (textBoxHeight / 2), textColor, ALIGN_LEFT, leftText.c_str());
	H::Draw.String(fFont, x + boxWidth - 5, y + (textBoxHeight / 2), textColor, ALIGN_RIGHT, rightText.c_str());

	if (m_iWait)
		H::Draw.StringOutlined(fFont, dtPos.x, y + boxHeight + 2, textColor, Vars::Menu::Theme::Background.Value, ALIGN_TOP, "Not Ready");
}