#include "TelemetryBlocker.h"


MAKE_HOOK(CSteamWorksGameStatsUploader_GetInterface, S::CSteamWorksGameStatsUploader_GetInterface(), void*,
	void* rcx)
{
	DEBUG_RETURN(CSteamWorksGameStatsUploader_GetInterface, rcx);

	if (Vars::Misc::TelemetryBlocker::Mode.Value == Vars::Misc::TelemetryBlocker::ModeEnum::Aggressive)
		return nullptr;

	return CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CSteamWorksGameStatsUploader_WritePerfData, S::CSteamWorksGameStatsUploader_WritePerfData(), int64_t,
	void* rcx, void* rdx)
{
	DEBUG_RETURN(CSteamWorksGameStatsUploader_WritePerfData, rcx, rdx);

	if (Vars::Misc::TelemetryBlocker::Mode.Value >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced)
		return 0;

	return CALL_ORIGINAL(rcx, rdx);
}

MAKE_HOOK(CGameStats_ResetData, S::CGameStats_ResetData(), int64_t,
	void* rcx)
{
	DEBUG_RETURN(CGameStats_ResetData, rcx);

	const auto iResult = CALL_ORIGINAL(rcx);
	if (Vars::Misc::TelemetryBlocker::Mode.Value < Vars::Misc::TelemetryBlocker::ModeEnum::Balanced || !rcx)
		return iResult;

	static const int iStatsHolderOffset = S::CGameStats_ResetData_StatsHolderOffset.ReadAt<int>(18);
	auto* pStatsHolder = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(rcx) + iStatsHolderOffset);
	if (!pStatsHolder)
		return iResult;

	auto* pData = *reinterpret_cast<KeyValues**>(pStatsHolder);
	if (!pData)
		return iResult;

	pData->SetString("srcid", "");
	pData->SetString("CPUID", "");
	pData->SetFloat("CPUGhz", 0.f);
	pData->SetUint64("CPUModel", 0);
	pData->SetUint64("CPUFeatures0", 0);
	pData->SetUint64("CPUFeatures1", 0);
	pData->SetUint64("CPUFeatures2", 0);
	pData->SetInt("NumCores", 0);
	pData->SetInt("PhysicalRamMbTotal", 0);
	pData->SetInt("PhysicalRamMbAvailable", 0);
	pData->SetInt("VirtualRamMbTotal", 0);
	pData->SetInt("VirtualRamMbAvailable", 0);
	pData->SetString("GPUDrv", "");
	pData->SetInt("GPUVendor", 0);
	pData->SetInt("GPUDeviceID", 0);
	pData->SetString("GPUDriverVersion", "");
	pData->SetInt("DxLvl", 0);
	pData->SetInt("Width", 0);
	pData->SetInt("Height", 0);
	pData->SetBool("Windowed", false);
	pData->SetInt("MaxDxLevel", 0);
	return iResult;
}

MAKE_HOOK(CSteamWorksGameStatsUploader_SubmitRow, S::CSteamWorksGameStatsUploader_SubmitRow(), int,
	void* rcx, void* rdx, bool r8)
{
	DEBUG_RETURN(CSteamWorksGameStatsUploader_SubmitRow, rcx, rdx, r8);

	if (Vars::Misc::TelemetryBlocker::Mode.Value >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced)
		return 2;

	return CALL_ORIGINAL(rcx, rdx, r8);
}

MAKE_HOOK(CSteamWorksGameStatsUploader_DrainRows, S::CSteamWorksGameStatsUploader_DrainRows(), void,
	void* rcx)
{
	DEBUG_RETURN(CSteamWorksGameStatsUploader_DrainRows, rcx);

	if (Vars::Misc::TelemetryBlocker::Mode.Value >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced && rcx)
	{
		static const int iUploadGateOffset = S::CSteamWorksGameStatsUploader_SubmitRow_UploadGateOffset.ReadAt<int>(30);
		auto* pUploadGate = reinterpret_cast<uint8_t*>(rcx) + iUploadGateOffset;
		const auto bOriginalGate = *pUploadGate;
		*pUploadGate = 0;
		CALL_ORIGINAL(rcx);
		*pUploadGate = bOriginalGate;
		return;
	}

	CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CSteamWorksGameStatsUploader_EndSession, S::CSteamWorksGameStatsUploader_EndSession(), int64_t,
	void* rcx)
{
	DEBUG_RETURN(CSteamWorksGameStatsUploader_EndSession, rcx);

	if (Vars::Misc::TelemetryBlocker::Mode.Value >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced && rcx)
	{
		static const int iSessionIdOffset = S::CSteamWorksGameStatsUploader_EndSession_SessionIdOffset.ReadAt<int>(58);
		auto* pSessionId = reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(rcx) + iSessionIdOffset);
		*pSessionId = 0;

		const auto iResult = CALL_ORIGINAL(rcx);
		S::CSteamWorksGameStatsUploader_ResetSession.Call<void>(rcx);
		return iResult;
	}

	return CALL_ORIGINAL(rcx);
}

MAKE_HOOK(TelemetrySetLevel, S::TelemetrySetLevel(), int64_t,
	int iLevel)
{
	DEBUG_RETURN(TelemetrySetLevel, iLevel);

	if (Vars::Misc::TelemetryBlocker::Mode.Value >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced && iLevel != 0)
		return 0;

	return CALL_ORIGINAL(iLevel);
}

MAKE_HOOK(TelemetryTick, S::TelemetryTick(), uint64_t)
{
	DEBUG_RETURN(TelemetryTick);

	if (Vars::Misc::TelemetryBlocker::Mode.Value >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced)
		return 0;

	return CALL_ORIGINAL();
}

void CTelemetryBlocker::ApplyConVarToBeDisabled(const char* sName, int& iOriginal, bool& bFound)
{
	auto pConVar = H::ConVars.FindVar(sName);
	if (!pConVar)
		return;

	iOriginal = pConVar->GetInt();
	pConVar->SetValue(0);
	bFound = true;
}

void CTelemetryBlocker::ApplyFloatConVarToBeDisabled(const char* sName, float& flOriginal, bool& bFound)
{
	auto pConVar = H::ConVars.FindVar(sName);
	if (!pConVar)
		return;

	flOriginal = pConVar->GetFloat();
	pConVar->SetValue(0.f);
	bFound = true;
}

void CTelemetryBlocker::RestoreConVar(const char* sName, int iOriginal, bool bFound)
{
	if (!bFound)
		return;

	auto pConVar = H::ConVars.FindVar(sName);
	if (!pConVar)
		return;

	pConVar->SetValue(iOriginal);
}

void CTelemetryBlocker::RestoreFloatConVar(const char* sName, float flOriginal, bool bFound)
{
	if (!bFound)
		return;

	auto pConVar = H::ConVars.FindVar(sName);
	if (!pConVar)
		return;

	pConVar->SetValue(flOriginal);
}

void CTelemetryBlocker::Initialize()
{
	if (Vars::Misc::TelemetryBlocker::Mode.Value <= Vars::Misc::TelemetryBlocker::ModeEnum::Off)
		return;

	ApplyConVarToBeDisabled("tf_stats_track", m_iTfStatsTrackOrig, m_bTfStatsTrackFound);
	ApplyConVarToBeDisabled("steamworks_stats_disable", m_iSteamworksStatsDisableOrig, m_bSteamworksStatsDisableFound);
	ApplyConVarToBeDisabled("steamworks_sessionid_client", m_iSteamworksSessionIdClientOrig, m_bSteamworksSessionIdClientFound);
	ApplyConVarToBeDisabled("steamworks_sessionid_server", m_iSteamworksSessionIdServerOrig, m_bSteamworksSessionIdServerFound);
	ApplyConVarToBeDisabled("replay_enable", m_iReplayEnableOrig, m_bReplayEnableFound);
	ApplyFloatConVarToBeDisabled("tf_matchmaking_ogs_odds", m_flTfMatchmakingOgsOddsOrig, m_bTfMatchmakingOgsOddsFound);
}

void CTelemetryBlocker::Unload()
{
	RestoreConVar("tf_stats_track", m_iTfStatsTrackOrig, m_bTfStatsTrackFound);
	RestoreConVar("steamworks_stats_disable", m_iSteamworksStatsDisableOrig, m_bSteamworksStatsDisableFound);
	RestoreConVar("steamworks_sessionid_client", m_iSteamworksSessionIdClientOrig, m_bSteamworksSessionIdClientFound);
	RestoreConVar("steamworks_sessionid_server", m_iSteamworksSessionIdServerOrig, m_bSteamworksSessionIdServerFound);
	RestoreConVar("replay_enable", m_iReplayEnableOrig, m_bReplayEnableFound);
	RestoreFloatConVar("tf_matchmaking_ogs_odds", m_flTfMatchmakingOgsOddsOrig, m_bTfMatchmakingOgsOddsFound);
}
