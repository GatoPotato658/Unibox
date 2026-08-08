#include "TelemetryBlocker.h"

#include <Psapi.h>

MAKE_SIGNATURE(Telemetry_GetSteamGameStats, "client.dll", "48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 33 DB 48 8B F1 8B FB 48 85 C0 74 ?? 48 8B 48 08 48 85 C9 74 ?? 48 39 58 18", 0x0);
MAKE_HOOK(Telemetry_GetSteamGameStats, S::Telemetry_GetSteamGameStats(), void*,
	void* rcx)
{
	DEBUG_RETURN(Telemetry_GetSteamGameStats, rcx);

	if (Vars::Misc::TelemetryBlocker::Mode.Value == Vars::Misc::TelemetryBlocker::ModeEnum::Aggressive)
		return nullptr;

	return CALL_ORIGINAL(rcx);
}

MAKE_SIGNATURE(Telemetry_SubmitRow, "client.dll", "48 89 74 24 18 57 48 83 EC 20 48 8B F2 48 8B F9 48 85 D2 0F 84 ?? ?? ?? ?? 45 84 C0 74 ?? 41 B0 01 E8 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? B8 01 00 00 00", 0x0);
MAKE_HOOK(Telemetry_SubmitRow, S::Telemetry_SubmitRow(), int,
	void* rcx, void* rdx, bool r8)
{
	DEBUG_RETURN(Telemetry_SubmitRow, rcx, rdx, r8);

	if (Vars::Misc::TelemetryBlocker::Mode.Value >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced)
		return 2;

	return CALL_ORIGINAL(rcx, rdx, r8);
}

uintptr_t CTelemetryBlocker::GetUploaderAddress()
{
	if (const auto hClient = GetModuleHandleA("client.dll"))
		return reinterpret_cast<uintptr_t>(hClient) + 0x1135150;
	return 0;
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

void CTelemetryBlocker::RestoreConVar(const char* sName, int iOriginal, bool bFound)
{
	if (!bFound)
		return;

	auto pConVar = H::ConVars.FindVar(sName);
	if (!pConVar)
		return;

	pConVar->SetValue(iOriginal);
}

void CTelemetryBlocker::PatchGateByte()
{
	const uintptr_t uObject = GetUploaderAddress();
	if (!uObject)
		return;

	m_uGateByteAddr = uObject + 0x3C2;
	m_uGateByteOrig = *reinterpret_cast<uint8_t*>(m_uGateByteAddr);

	DWORD dwOldProtect;
	VirtualProtect(reinterpret_cast<void*>(m_uGateByteAddr), 1, PAGE_EXECUTE_READWRITE, &dwOldProtect);
	*reinterpret_cast<uint8_t*>(m_uGateByteAddr) = 0;
	VirtualProtect(reinterpret_cast<void*>(m_uGateByteAddr), 1, dwOldProtect, &dwOldProtect);
}

void CTelemetryBlocker::RestoreGateByte()
{
	if (!m_uGateByteAddr)
		return;

	DWORD dwOldProtect;
	VirtualProtect(reinterpret_cast<void*>(m_uGateByteAddr), 1, PAGE_EXECUTE_READWRITE, &dwOldProtect);
	*reinterpret_cast<uint8_t*>(m_uGateByteAddr) = m_uGateByteOrig;
	VirtualProtect(reinterpret_cast<void*>(m_uGateByteAddr), 1, dwOldProtect, &dwOldProtect);

	m_uGateByteAddr = 0;
}

uint64_t __fastcall hkUploaderVTableStub()
{
	return 0;
}

void CTelemetryBlocker::PatchVTableSlots()
{
	const uintptr_t uObject = GetUploaderAddress();
	if (!uObject)
		return;

	const uintptr_t uVTable = *reinterpret_cast<uintptr_t*>(uObject);
	if (!uVTable)
		return;

	MODULEINFO tModuleInfo;
	if (!GetModuleInformation(GetCurrentProcess(), GetModuleHandleA("client.dll"), &tModuleInfo, sizeof(MODULEINFO)))
		return;

	const uintptr_t uImageBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
	const uintptr_t uImageEnd = uImageBase + tModuleInfo.SizeOfImage;

	constexpr int vSlots[] = { 1, 13, 18, 22, 23 };
	for (const int iSlot : vSlots)
	{
		const uintptr_t uSlotAddr = uVTable + static_cast<uintptr_t>(iSlot) * sizeof(uintptr_t);
		if (uSlotAddr < uImageBase || uSlotAddr >= uImageEnd)
			continue;

		const uintptr_t uOriginal = *reinterpret_cast<uintptr_t*>(uSlotAddr);
		if (!uOriginal)
			continue;

		m_vVTablePatches.emplace_back(uSlotAddr, uOriginal);

		DWORD dwOldProtect;
		VirtualProtect(reinterpret_cast<void*>(uSlotAddr), sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &dwOldProtect);
		*reinterpret_cast<uintptr_t*>(uSlotAddr) = reinterpret_cast<uintptr_t>(hkUploaderVTableStub);
		VirtualProtect(reinterpret_cast<void*>(uSlotAddr), sizeof(uintptr_t), dwOldProtect, &dwOldProtect);
	}
}

void CTelemetryBlocker::RestoreVTableSlots()
{
	for (auto& tPatch : m_vVTablePatches)
	{
		DWORD dwOldProtect;
		VirtualProtect(reinterpret_cast<void*>(tPatch.first), sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &dwOldProtect);
		*reinterpret_cast<uintptr_t*>(tPatch.first) = tPatch.second;
		VirtualProtect(reinterpret_cast<void*>(tPatch.first), sizeof(uintptr_t), dwOldProtect, &dwOldProtect);
	}
	m_vVTablePatches.clear();
}

void CTelemetryBlocker::Initialize()
{
	ApplyConVarToBeDisabled("tf_stats_track", m_iTfStatsTrackOrig, m_bTfStatsTrackFound);
	ApplyConVarToBeDisabled("cl_savescreenshotstosteam", m_iClSaveScreenshotsOrig, m_bClSaveScreenshotsFound);
	ApplyConVarToBeDisabled("cl_steamscreenshots", m_iClSteamScreenshotsOrig, m_bClSteamScreenshotsFound);

	const int iMode = Vars::Misc::TelemetryBlocker::Mode.Value;

	if (iMode >= Vars::Misc::TelemetryBlocker::ModeEnum::Balanced)
	{
		PatchGateByte();

		if (iMode == Vars::Misc::TelemetryBlocker::ModeEnum::Aggressive)
			PatchVTableSlots();
	}
}

void CTelemetryBlocker::Unload()
{
	RestoreGateByte();
	RestoreVTableSlots();

	RestoreConVar("tf_stats_track", m_iTfStatsTrackOrig, m_bTfStatsTrackFound);
	RestoreConVar("cl_savescreenshotstosteam", m_iClSaveScreenshotsOrig, m_bClSaveScreenshotsFound);
	RestoreConVar("cl_steamscreenshots", m_iClSteamScreenshotsOrig, m_bClSteamScreenshotsFound);
}