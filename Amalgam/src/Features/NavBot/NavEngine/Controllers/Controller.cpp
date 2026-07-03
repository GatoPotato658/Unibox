#include "Controller.h"
#include "CPController/CPController.h"
#include "FlagController/FlagController.h"
#include "PLController/PLController.h"
#include "HaarpController/HaarpController.h"
#include "DoomsdayController/DoomsdayController.h"
#include "PasstimeController/PasstimeController.h"
#include "MVMController/MVMController.h"

static std::string GetNormalizedLevelName()
{
	auto sMapName = std::string(I::EngineClient->GetLevelName());
	size_t nLastSlash = sMapName.find_last_of("/\\");
	if (nLastSlash != std::string::npos)
		sMapName = sMapName.substr(nLastSlash + 1);
	return sMapName;
}

static bool MapStartsWith(const std::string& sMapName, std::string_view sPrefix)
{
	return sMapName.find(sPrefix) == 0;
}

ETFGameType GetGameType()
{
	// Check if we're on doomsday
	auto sMapName = GetNormalizedLevelName();
	F::GameObjectiveController.m_bDoomsday = sMapName.find("sd_doomsday") != std::string::npos;
	F::GameObjectiveController.m_bHaarp = sMapName.find("ctf_haarp") != std::string::npos;

	int iType = TF_GAMETYPE_UNDEFINED;
	if (auto pGameRules = I::TFGameRules())
		iType = pGameRules->m_nGameType();

	return static_cast<ETFGameType>(iType);
}

void CGameObjectiveController::Update()
{
	static float flNextGameTypeRefresh = 0.0f;
	if (m_eGameMode == TF_GAMETYPE_UNDEFINED || I::GlobalVars->curtime >= flNextGameTypeRefresh)
	{
		m_eGameMode = GetGameType();
		flNextGameTypeRefresh = I::GlobalVars->curtime + 1.0f;
	}

	const auto sMapName = GetNormalizedLevelName();

	F::MVMController.Update();
	if (F::MVMController.IsActive())
		return;

	if (MapStartsWith(sMapName, "cppl_"))
	{
		F::CPController.Update();
		F::PLController.Update();
		return;
	}
	if (MapStartsWith(sMapName, "vsh_") || MapStartsWith(sMapName, "2koth_") || MapStartsWith(sMapName, "koth_") || MapStartsWith(sMapName, "cp_") || MapStartsWith(sMapName, "tc_"))
	{
		F::CPController.Update();
		return;
	}
	if (MapStartsWith(sMapName, "pl_") || MapStartsWith(sMapName, "plr_"))
	{
		F::PLController.Update();
		return;
	}
	if (MapStartsWith(sMapName, "pass_"))
	{
		F::PasstimeController.Update();
		return;
	}
	if (MapStartsWith(sMapName, "ctf_") || MapStartsWith(sMapName, "sd_") || MapStartsWith(sMapName, "rd_") || MapStartsWith(sMapName, "pd_"))
	{
		F::FlagController.Update();
		if (MapStartsWith(sMapName, "sd_doomsday"))
		{
			F::CPController.Update();
			F::DoomsdayController.Update();
		}
		if (MapStartsWith(sMapName, "ctf_haarp"))
		{
			F::CPController.Update();
			F::HaarpController.Update();
		}
		return;
	}

	switch (m_eGameMode)
	{
	case TF_GAMETYPE_CTF:
		F::FlagController.Update();
		if (m_bDoomsday)
		{
			F::CPController.Update();
			F::DoomsdayController.Update();
		}
		if (m_bHaarp)
		{
			F::CPController.Update();
			F::HaarpController.Update();
		}
		break;
	case TF_GAMETYPE_CP:
		F::CPController.Update();
		break;
	case TF_GAMETYPE_ESCORT:
		F::PLController.Update();
		break;
	case TF_GAMETYPE_PASSTIME:
		F::PasstimeController.Update();
		break;
	default:
		if (m_bDoomsday)
		{
			F::FlagController.Update();
			F::CPController.Update();
			F::DoomsdayController.Update();
		}
		if (m_bHaarp)
		{
			F::FlagController.Update();
			F::CPController.Update();
			F::HaarpController.Update();
		}
		break;
	}
}

void CGameObjectiveController::Reset()
{
	m_eGameMode = TF_GAMETYPE_UNDEFINED;
	m_bDoomsday = false;
	m_bHaarp = false;
	F::FlagController.Init();
	F::PLController.Init();
	F::CPController.Init();
	F::PasstimeController.Init();
	F::MVMController.Reset();
}
