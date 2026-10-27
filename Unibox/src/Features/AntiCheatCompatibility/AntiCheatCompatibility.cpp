#include "AntiCheatCompatibility.h"

#include "../Backtrack/Backtrack.h"
#include "../Misc/Misc.h"

void CAntiCheatCompatibility::SanitizeString(std::string& sText)
{
	std::erase(sText, '\n');
	std::erase(sText, '\r');

	static const std::string sRLM = "\xE2\x80\x8F";
	static const std::string sLRM = "\xE2\x80\x8E";
	size_t pos = 0;
	while ((pos = sText.find(sRLM, pos)) != std::string::npos)
		sText.erase(pos, sRLM.length());
	pos = 0;
	while ((pos = sText.find(sLRM, pos)) != std::string::npos)
		sText.erase(pos, sLRM.length());
}

static inline float CalcStacAngleDiff(const Vec3& v1, const Vec3& v2)
{
	float flPitch = v1.x - v2.x;
	float flYaw = v1.y - v2.y;
	float flDist = std::sqrt(flPitch * flPitch + flYaw * flYaw);
	if (flDist > 180.f)
		flDist = std::abs(flDist - 360.f);
	return flDist;
}

void CAntiCheatCompatibility::EnforceSettings()
{
	if (!Active())
		return;

	if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent
		|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Locking)
		Vars::Aimbot::General::AimType.Value = Vars::Aimbot::General::AimTypeEnum::Plain;

	Vars::Aimbot::General::NoSpread.Value = false;
	Vars::CritHack::ForceCrits.Value = false;
	Vars::CritHack::AvoidRandomCrits.Value = false;
	Vars::Misc::Movement::FastAccelerate.Value = false;
	Vars::Misc::Movement::DuckSpeed.Value = false;
	Vars::Doubletap::Doubletap.Value = false;
	Vars::Doubletap::Warp.Value = false;
	Vars::Doubletap::RechargeTicks.Value = false;
	Vars::Doubletap::PassiveRecharge.Value = 0;
	Vars::Fakelag::Fakelag.Value = Vars::Fakelag::FakelagEnum::Off;
	Vars::Misc::Automation::KartControl.Value = false;

	const auto pLocal = H::Entities.GetLocal();
	if (!Vars::AntiAim::TauntSpin.Value || !pLocal || !pLocal->IsTaunting())
		Vars::AntiAim::Enabled.Value = false;

	static auto fov_desired = H::ConVars.FindVar("fov_desired");
	if (fov_desired && (fov_desired->GetInt() < 20 || fov_desired->GetInt() > 90))
		fov_desired->SetValue(90);

	static auto cl_cmdrate = H::ConVars.FindVar("cl_cmdrate");
	if (cl_cmdrate && cl_cmdrate->GetInt() < 10)
		cl_cmdrate->SetValue(66);

	static auto cl_interpolate = H::ConVars.FindVar("cl_interpolate");
	if (cl_interpolate && cl_interpolate->GetInt() != 1)
		cl_interpolate->SetValue(1);
}

void CAntiCheatCompatibility::CreateMove(CUserCmd* pCmd)
{
	EnforceSettings();

	if (!Active())
		return;

	pCmd->tick_count = G::OriginalCmd.tick_count;
	pCmd->command_number = G::OriginalCmd.command_number;
	if (pCmd->tick_count < 0)
		pCmd->tick_count = 0;
	if (pCmd->command_number < 0)
		pCmd->command_number = 0;

	pCmd->buttons &= ((1 << 26) - 1);
	pCmd->buttons &= ~(IN_LEFT | IN_RIGHT);

	Math::ClampAngles(pCmd->viewangles);

	m_vHistory.emplace_front(pCmd->viewangles, pCmd->buttons & IN_ATTACK, pCmd->buttons & IN_ATTACK2, G::SendPacket);
	if (m_vHistory.size() > 5)
		m_vHistory.pop_back();
	if (m_vHistory.size() < 3)
		return;

	// prevent trigger checks, though this shouldn't happen ordinarily
	if (!m_vHistory[0].m_bAttack1 && m_vHistory[1].m_bAttack1 && !m_vHistory[2].m_bAttack1)
	{
		pCmd->buttons |= IN_ATTACK;
		m_vHistory[0].m_bAttack1 = true;
	}
	if (!m_vHistory[0].m_bAttack2 && m_vHistory[1].m_bAttack2 && !m_vHistory[2].m_bAttack2)
	{
		pCmd->buttons |= IN_ATTACK2;
		m_vHistory[0].m_bAttack2 = true;
	}

	// don't care if we are actually attacking or not, a miss is less important than a detection
	if (m_vHistory[0].m_bAttack1 || m_vHistory[1].m_bAttack1 || m_vHistory[2].m_bAttack1)
	{
		// prevent silent aim checks
		if (CalcStacAngleDiff(m_vHistory[0].m_vAngle, m_vHistory[1].m_vAngle) > PSILENT_EPSILON
			&& (std::abs(m_vHistory[0].m_vAngle.x - m_vHistory[2].m_vAngle.x) < REAL_EPSILON
				&& std::abs(m_vHistory[0].m_vAngle.y - m_vHistory[2].m_vAngle.y) < REAL_EPSILON))
		{
			pCmd->viewangles = m_vHistory[1].m_vAngle.LerpAngle(m_vHistory[0].m_vAngle, 0.5f);
			if (std::abs(pCmd->viewangles.x - m_vHistory[2].m_vAngle.x) < REAL_EPSILON
				&& std::abs(pCmd->viewangles.y - m_vHistory[2].m_vAngle.y) < REAL_EPSILON)
				pCmd->viewangles = m_vHistory[0].m_vAngle + Vec3(0.f, REAL_EPSILON * 2);
			Math::ClampAngles(pCmd->viewangles);
			m_vHistory[0].m_vAngle = pCmd->viewangles;
			m_vHistory[0].m_bSendingPacket = G::SendPacket = m_vHistory[1].m_bSendingPacket;
		}

		// prevent aim snap checks
		if (m_vHistory.size() == 5)
		{
			float flDelta01 = CalcStacAngleDiff(m_vHistory[0].m_vAngle, m_vHistory[1].m_vAngle);
			float flDelta12 = CalcStacAngleDiff(m_vHistory[1].m_vAngle, m_vHistory[2].m_vAngle);
			float flDelta23 = CalcStacAngleDiff(m_vHistory[2].m_vAngle, m_vHistory[3].m_vAngle);
			float flDelta34 = CalcStacAngleDiff(m_vHistory[3].m_vAngle, m_vHistory[4].m_vAngle);

			if ((flDelta12 > SNAP_SIZE_EPSILON && flDelta23 < SNAP_NOISE_EPSILON && m_vHistory[2].m_vAngle != m_vHistory[3].m_vAngle
				|| flDelta23 > SNAP_SIZE_EPSILON && flDelta12 < SNAP_NOISE_EPSILON && m_vHistory[1].m_vAngle != m_vHistory[2].m_vAngle)
				&& flDelta01 < SNAP_NOISE_EPSILON && m_vHistory[0].m_vAngle != m_vHistory[1].m_vAngle
				&& flDelta34 < SNAP_NOISE_EPSILON && m_vHistory[3].m_vAngle != m_vHistory[4].m_vAngle)
			{
				pCmd->viewangles.y += SNAP_NOISE_EPSILON * 2;
				Math::ClampAngles(pCmd->viewangles);
				m_vHistory[0].m_vAngle = pCmd->viewangles;
				m_vHistory[0].m_bSendingPacket = G::SendPacket = m_vHistory[1].m_bSendingPacket;
			}
		}
	}
}

void CAntiCheatCompatibility::RespondCvarValue(INetMessage& msg)
{
	if (!Active())
		return;

	auto pMsg = reinterpret_cast<CLC_RespondCvarValue*>(&msg);
	if (!pMsg->m_szCvarName)
		return;

	const uint32_t uNameHash = FNV1A::Hash32(pMsg->m_szCvarName);

	static const std::unordered_set<uint32_t> s_sCheatVars = {
		FNV1A::Hash32Const("cat_load"),
		FNV1A::Hash32Const("setcvar"),
		FNV1A::Hash32Const("getcvar"),
		FNV1A::Hash32Const("crash"),
		FNV1A::Hash32Const("rijin_load"),
		FNV1A::Hash32Const("rijin_save"),
		FNV1A::Hash32Const("windows_speaker_config"),
		FNV1A::Hash32Const("lithium_disable_party_bypass"),
		FNV1A::Hash32Const("caramelldansen"),
		FNV1A::Hash32Const("melancholy"),
		FNV1A::Hash32Const("SetCursor"),
		FNV1A::Hash32Const("hook")
	};
	if (s_sCheatVars.contains(uNameHash))
	{
		pMsg->m_eStatusCode = 1;
		m_sValue = "";
		pMsg->m_szCvarValue = m_sValue.c_str();
		return;
	}

	auto pConVar = H::ConVars.FindVar(pMsg->m_szCvarName);

	switch (uNameHash)
	{
	case FNV1A::Hash32Const("cl_interp"):
	{
		float flInterp = 0.030303f;
		if (F::Backtrack.m_flSentInterp != -1.f)
			flInterp = F::Backtrack.m_flSentInterp;
		else if (pConVar)
			flInterp = pConVar->GetFloat();
		flInterp = std::clamp(flInterp, 0.015152f, 0.1f);
		flInterp += SDK::StdRandomFloat(-0.001f, 0.001f);
		flInterp = std::clamp(flInterp, 0.015152f, 0.1f);
		m_sValue = std::format("{:.6f}", flInterp);
		pMsg->m_eStatusCode = 0;
		break;
	}
	case FNV1A::Hash32Const("cl_interp_ratio"):
		m_sValue = SDK::StdRandomInt(0, 3) == 0 ? "2" : "1";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("cl_interpolate"):
		m_sValue = "1";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("cl_cmdrate"):
	{
		int iCmdrate = 66;
		if (F::Misc.m_iWishCmdrate != -1)
			iCmdrate = F::Misc.m_iWishCmdrate;
		else if (pConVar)
			iCmdrate = pConVar->GetInt();
		iCmdrate = std::clamp(iCmdrate, 10, 66);
		m_sValue = std::to_string(iCmdrate);
		pMsg->m_eStatusCode = 0;
		break;
	}
	case FNV1A::Hash32Const("cl_updaterate"):
	{
		int iUpdaterate = pConVar ? pConVar->GetInt() : 66;
		iUpdaterate = std::clamp(iUpdaterate, 10, 66);
		m_sValue = std::to_string(iUpdaterate);
		pMsg->m_eStatusCode = 0;
		break;
	}
	case FNV1A::Hash32Const("fov_desired"):
	{
		int iFov = pConVar ? pConVar->GetInt() : 90;
		iFov = std::clamp(iFov, 20, 90);
		m_sValue = std::to_string(iFov);
		pMsg->m_eStatusCode = 0;
		break;
	}
	case FNV1A::Hash32Const("cl_thirdperson"):
		m_sValue = "0";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("host_timescale"):
		m_sValue = "1.0";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("sv_cheats"):
		m_sValue = "0";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("r_drawothermodels"):
		m_sValue = "1";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("snd_show"):
	case FNV1A::Hash32Const("snd_visualize"):
	case FNV1A::Hash32Const("r_portalsopenall"):
	case FNV1A::Hash32Const("mat_wireframe"):
	case FNV1A::Hash32Const("mat_fillrate"):
		m_sValue = "0";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("r_drawparticles"):
	case FNV1A::Hash32Const("r_skybox"):
	case FNV1A::Hash32Const("r_drawskybox"):
		m_sValue = "1";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("cl_pitchup"):
	case FNV1A::Hash32Const("cl_pitchdown"):
		m_sValue = "89";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("mat_dxlevel"):
		m_sValue = pConVar ? pConVar->GetString() : "90";
		pMsg->m_eStatusCode = 0;
		break;
	case FNV1A::Hash32Const("name"):
		m_sValue = pConVar ? pConVar->GetString() : "";
		SanitizeString(m_sValue);
		pMsg->m_eStatusCode = 0;
		break;
	default:
	{
		if (std::string_view(pMsg->m_szCvarName).starts_with("net_"))
		{
			m_sValue = "0";
			pMsg->m_eStatusCode = 0;
			break;
		}

		if (pConVar)
		{
			int nOrigFlags = H::ConVars.GetOriginalFlags(reinterpret_cast<ConCommandBase*>(pConVar->m_pParent));
			if (nOrigFlags & FCVAR_CHEAT)
				m_sValue = pConVar->m_pParent->m_pszDefaultValue ? pConVar->m_pParent->m_pszDefaultValue : "0";
			else
				m_sValue = pConVar->GetString();
			pMsg->m_eStatusCode = 0;
		}
		else
		{
			pMsg->m_eStatusCode = 1;
			m_sValue = "";
		}
		break;
	}
	}
	pMsg->m_szCvarValue = m_sValue.c_str();

	SDK::Output("Convar spoof", std::format("{}: {} ({})", pMsg->m_szCvarName, pMsg->m_szCvarValue, msg.ToString()).c_str(), Vars::Menu::Theme::Accent.Value, Vars::Debug::Logging.Value);
}

void CAntiCheatCompatibility::BunnyHop(CUserCmd* pCmd, bool bCurrValid, bool bLastValid)
{
	if (!Active())
		return;

	if (bCurrValid)
	{
		if (!bLastValid && (pCmd->buttons & IN_JUMP) && !(G::LastUserCmd->buttons & IN_JUMP))
			m_iJumps++;
		else
			m_iJumps = 0;

		if (m_iJumps > 4)
		{
			pCmd->buttons &= ~IN_JUMP;
			m_iJumps = 0;
		}
	}
}
