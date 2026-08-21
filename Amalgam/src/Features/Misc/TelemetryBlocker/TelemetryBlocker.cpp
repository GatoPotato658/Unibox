#include "TelemetryBlocker.h"

MAKE_SIGNATURE(CSteamWorksGameStatsUploader_GetInterface, "client.dll", "48 89 5C 24 08 48 89 74 24 18 57 48 83 EC ? 48 8B 05 ? ? ? ? 33 DB 48 8B F1 8B FB 48 85 C0 74 ? 48 8B 48 08 48 85 C9 74 ? 48 39 58 18", 0x0);
MAKE_SIGNATURE(CSteamWorksGameStatsUploader_WritePerfData, "client.dll", "40 55 53 57 48 8B EC 48 83 EC ? 48 8B FA 48 8B D9 E8 ? ? ? ? 48 89 43 78 48 85 C0 0F 84 ? ? ? ? 80 BB C2 ? ? ? ? 0F 84 ? ? ? ? 48 8B 0D ? ? ? ? 4C 89 74 24 38 48 89 74 24 68 48 85 C9 74 ? 48 8B 49 18 48 85 C9 74 ? 48 8B 01 FF 50 18 8B F0", 0x0);
MAKE_SIGNATURE(CSteamWorksGameStatsUploader_SubmitRow, "client.dll", "40 53 57 41 55 48 83 EC ? 41 0F B6 D8 4C 8B EA 48 8B F9 48 85 D2 0F 84 ? ? ? ? 80 B9 C2 ? ? ? ? 0F 84 ? ? ? ? E8 ? ? ? ? 48 89 47 78 48 85 C0", 0x0);
MAKE_SIGNATURE(CSteamWorksGameStatsUploader_DrainRows, "client.dll", "40 53 57 41 54 48 83 EC 40 33 DB 48 8B F9 44 8B E3 39 99 ? ? ? ? 0F 8E ? ? ? ? 48 89 6C 24 68 48 89 74 24 70 4C 89 6C 24 78 4C 89 74 24 38 4C 89 7C 24 30 0F 29 74 24 20 0F 1F 44 00 00 48 8B 87 ? ? ? ? 41 8B F4 4C 8B 2C F0 4D 85 ED 0F 84 ? ? ? ? 80 BF C2 ? ? ? ? 0F 84 ? ? ? ? 48 8B CF E8 ? ? ? ? 48 89 47 78 48 85 C0 75 ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? E9 ? ? ? ? 49 8B CD E8 ? ? ? ? 48 8B 4F 78 48 8D 54 24 60 4C 8B 87 ? ? ? ? 4C 8B C8 48 89 5C 24 60 4C 8B 11 41 FF 52 28 4C 8B 44 24 60", 0x0);
MAKE_SIGNATURE(CSteamWorksGameStatsUploader_EndSession, "client.dll", "40 53 48 83 EC 20 48 8B D9 48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 49 18 48 85 C9 74 ? 48 8B 01 FF 50 18 8B C0 EB ? 48 8D 4C 24 30 E8 ? ? ? ? 48 8B 44 24 30 48 83 BB B8 ? ? ? ? 89 83 ? ? ? ? 0F 84 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 89 43 78 48 85 C0 0F 84 ? ? ? ? 48 8B 93 ? ? ? ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 89 43 78 48 85 C0 0F 84 ? ? ? ? 4C 8B 10 4C 8D 05 ? ? ? ? 44 8B 8B ? ? ? ? 48 8B C8 48 8B 93 ? ? ? ? 41 FF 52 10 48 8B 4B 78", 0x0);
MAKE_SIGNATURE(CSteamWorksGameStatsUploader_ResetSession, "client.dll", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B D9 33 F6 48 89 B1 ? ? ? ? 33 D2 48 8D 0D ? ? ? ? E8 ? ? ? ? 33 D2 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 8B ? ? ? ? 48 89 B3 ? ? ? ? 33 D2 40 88 B3 ? ? ? ? 41 B8 04 01 00 00 89 B3 ? ? ? ? 89 B3 ? ? ? ? 40 88 B3 ? ? ? ? 48 89 B3 ? ? ? ? 48 89 B3 ? ? ? ? C7 83 90 00 00 00 ? ? ? ? E8 ? ? ? ? 48 8D 8B ? ? ? ? 33 D2 41 B8 04 01 00 00 E8 ? ? ? ? 48 8D 8B ? ? ? ? 33 D2 41 B8 04 01 00 00 E8 ? ? ? ? 48 89 B3 ? ? ? ?", 0x0);
MAKE_SIGNATURE(CGameStats_ResetData, "client.dll", "48 89 5C 24 18 55 56 57 48 81 EC 70 03 00 00 48 8B 99 ?? ?? ?? ?? 33 ED 48 8B F1 48 85 DB 74 ?? 48 8B 0B 48 85 C9 74 ?? E8 ?? ?? ?? ?? 48 89 2B BA 10 00 00 00 48 8B CB E8 ?? ?? ?? ?? 48 89 AE ?? ?? ?? ?? B9 10 00 00 00 40 88 AE ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B F8 48 85 C0 74 ?? B9 48 00 00 00 48 89 28 40 88 68 08 E8 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8D 15 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 48 89 07 EB ?? 48 8B C5 48 89 07 EB ?? 48 8B FD 48 89 BE ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8B 3F 41 B8 01 00 00 00 48 8B CF E8 ?? ?? ?? ?? 41 B8 01 00 00 00 48 8D 15 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 48 8B CF 48 8D 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? FF 15 ?? ?? ?? ?", 0x0);
MAKE_SIGNATURE(CSteamScreenshots_WriteScreenshot, "client.dll", "40 53 48 83 EC ? 48 8B 05 ? ? ? ? 0F B6 D9 48 85 C0 74 ? 48 83 78 58 ? 74 ? 48 8D 15 ? ? ? ? 48 8D 4C 24 20 E8 ? ? ? ? 48 8D 4C 24 20 E8 ? ? ? ? 84 C0 74 ? 48 8B 4C 24 20", 0x0);

MAKE_SIGNATURE(TelemetrySetLevel, "tier0.dll", "40 53 48 83 EC 20 8B 15 ? ? ? ? 8B D9 3B CA 74 ? 44 8B C1 48 8D 0D ? ? ? ? E8 ? ? ? ? 8B 05 ? ? ? ? 89 05 ? ? ? ? 89 1D ? ? ? ? C6 05 ? ? ? ? 01 48 83 C4 20 5B C3", 0x0);
MAKE_SIGNATURE(TelemetryTick, "tier0.dll", "40 57 41 54 48 83 EC 58 8B 05 ? ? ? ? A8 01 75 ? 83 C8 01 89 05 ? ? ? ? E8 ? ? ? ? 8B 05 ? ? ? ? F2 0F 11 05 ? ? ? ? A8 02 75 ? 83 C8 02 89 05 ? ? ? ? 0F 31 48 C1 E2 20 48 0B C2 48 89 05 ? ? ? ? 8B 3D ? ? ? ? 45 33 E4 85 FF 75 ? 8B 05 ? ? ? ?", 0x0);

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

	auto* pStatsHolder = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(rcx) + 5000);
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
		auto* pUploadGate = reinterpret_cast<uint8_t*>(rcx) + 962;
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
		auto* pSessionId = reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(rcx) + 952);
		*pSessionId = 0;

		const auto iResult = CALL_ORIGINAL(rcx);
		S::CSteamWorksGameStatsUploader_ResetSession.Call<void>(rcx);
		return iResult;
	}

	return CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CSteamScreenshots_WriteScreenshot, S::CSteamScreenshots_WriteScreenshot(), char,
	bool bScoreboard)
{
	DEBUG_RETURN(CSteamScreenshots_WriteScreenshot, bScoreboard);

	if (Vars::Misc::TelemetryBlocker::Mode.Value >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced)
		return 0;

	return CALL_ORIGINAL(bScoreboard);
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
	ApplyConVarToBeDisabled("cl_savescreenshotstosteam", m_iClSaveScreenshotsOrig, m_bClSaveScreenshotsFound);
	ApplyConVarToBeDisabled("cl_steamscreenshots", m_iClSteamScreenshotsOrig, m_bClSteamScreenshotsFound);
	ApplyConVarToBeDisabled("replay_enable", m_iReplayEnableOrig, m_bReplayEnableFound);
	ApplyFloatConVarToBeDisabled("tf_matchmaking_ogs_odds", m_flTfMatchmakingOgsOddsOrig, m_bTfMatchmakingOgsOddsFound);
}

void CTelemetryBlocker::Unload()
{
	RestoreConVar("tf_stats_track", m_iTfStatsTrackOrig, m_bTfStatsTrackFound);
	RestoreConVar("steamworks_stats_disable", m_iSteamworksStatsDisableOrig, m_bSteamworksStatsDisableFound);
	RestoreConVar("steamworks_sessionid_client", m_iSteamworksSessionIdClientOrig, m_bSteamworksSessionIdClientFound);
	RestoreConVar("steamworks_sessionid_server", m_iSteamworksSessionIdServerOrig, m_bSteamworksSessionIdServerFound);
	RestoreConVar("cl_savescreenshotstosteam", m_iClSaveScreenshotsOrig, m_bClSaveScreenshotsFound);
	RestoreConVar("cl_steamscreenshots", m_iClSteamScreenshotsOrig, m_bClSteamScreenshotsFound);
	RestoreConVar("replay_enable", m_iReplayEnableOrig, m_bReplayEnableFound);
	RestoreFloatConVar("tf_matchmaking_ogs_odds", m_flTfMatchmakingOgsOddsOrig, m_bTfMatchmakingOgsOddsFound);
}
