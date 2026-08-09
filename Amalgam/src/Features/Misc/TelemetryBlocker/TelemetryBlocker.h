#pragma once
#include "../../../SDK/SDK.h"

class CTelemetryBlocker
{
public:
	void Initialize();
	void Unload();

private:
	void ApplyConVarToBeDisabled(const char* sName, int& iOriginal, bool& bFound);
	void ApplyFloatConVarToBeDisabled(const char* sName, float& flOriginal, bool& bFound);
	void RestoreConVar(const char* sName, int iOriginal, bool bFound);
	void RestoreFloatConVar(const char* sName, float flOriginal, bool bFound);

	int m_iTfStatsTrackOrig = 1;
	int m_iSteamworksStatsDisableOrig = 0;
	int m_iSteamworksSessionIdClientOrig = 0;
	int m_iSteamworksSessionIdServerOrig = 0;
	int m_iClSaveScreenshotsOrig = 0;
	int m_iClSteamScreenshotsOrig = 0;
	int m_iReplayEnableOrig = 0;
	float m_flTfMatchmakingOgsOddsOrig = 0.05f;
	bool m_bTfStatsTrackFound = false;
	bool m_bSteamworksStatsDisableFound = false;
	bool m_bSteamworksSessionIdClientFound = false;
	bool m_bSteamworksSessionIdServerFound = false;
	bool m_bClSaveScreenshotsFound = false;
	bool m_bClSteamScreenshotsFound = false;
	bool m_bReplayEnableFound = false;
	bool m_bTfMatchmakingOgsOddsFound = false;
};

ADD_FEATURE(CTelemetryBlocker, TelemetryBlocker);
