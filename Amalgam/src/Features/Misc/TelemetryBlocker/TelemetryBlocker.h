#pragma once
#include "../../../SDK/SDK.h"

class CTelemetryBlocker
{
public:
	void Initialize();
	void Unload();

private:
	uintptr_t GetUploaderAddress();
	void ApplyConVarToBeDisabled(const char* sName, int& iOriginal, bool& bFound);
	void RestoreConVar(const char* sName, int iOriginal, bool bFound);
	void PatchGateByte();
	void RestoreGateByte();
	void PatchVTableSlots();
	void RestoreVTableSlots();

	int m_iTfStatsTrackOrig = 1;
	int m_iClSaveScreenshotsOrig = 0;
	int m_iClSteamScreenshotsOrig = 0;
	bool m_bTfStatsTrackFound = false;
	bool m_bClSaveScreenshotsFound = false;
	bool m_bClSteamScreenshotsFound = false;

	uint8_t m_uGateByteOrig = 0;
	uintptr_t m_uGateByteAddr = 0;
	std::vector<std::pair<uintptr_t, uintptr_t>> m_vVTablePatches;
};

ADD_FEATURE(CTelemetryBlocker, TelemetryBlocker);