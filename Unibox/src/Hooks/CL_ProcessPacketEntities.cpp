#include "../SDK/SDK.h"

MAKE_SIGNATURE(CL_ProcessPacketEntities, "engine.dll", "48 89 7C 24 ? 4C 89 64 24 ? 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 4C 8B F9", 0x0);

struct CriticalStorage_t
{
	CHandle<CTFWeaponBase> m_hWeapon;
	float m_flCritTokenBucket;
	int m_nCritChecks;
	int m_nCritSeedRequests;
	float m_flLastRapidFireCritCheckTime;
	float m_flCritTime;
};

MAKE_HOOK(CL_ProcessPacketEntities, S::CL_ProcessPacketEntities(), bool,
	SVC_PacketEntities* entmsg)
{
	DEBUG_RETURN(CL_ProcessPacketEntities, entmsg);

	if (entmsg->m_bIsDelta || I::EngineClient->IsPlayingDemo() || G::Unload) // we won't need to restore
		return CALL_ORIGINAL(entmsg);

	auto pLocalEnt = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	auto pLocal = pLocalEnt ? pLocalEnt->As<CTFPlayer>() : nullptr;
	if (!pLocal || !pLocal->IsPlayer())
		return CALL_ORIGINAL(entmsg);

	std::array<CriticalStorage_t, MAX_WEAPONS> aCriticalStorage = {};

	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		auto hWeapon = pLocal->m_hMyWeapons()[i];
		auto pWeapon = hWeapon.Get();
		if (!pWeapon || pWeapon->IsPlayer() || I::ClientEntityList->GetClientEntity(pWeapon->entindex()) != pWeapon)
			continue;

		aCriticalStorage[i].m_hWeapon = hWeapon;
		aCriticalStorage[i].m_flCritTokenBucket = pWeapon->m_flCritTokenBucket();
		aCriticalStorage[i].m_nCritChecks = pWeapon->m_nCritChecks();
		aCriticalStorage[i].m_nCritSeedRequests = pWeapon->m_nCritSeedRequests();
		aCriticalStorage[i].m_flLastRapidFireCritCheckTime = pWeapon->m_flLastRapidFireCritCheckTime();
		aCriticalStorage[i].m_flCritTime = pWeapon->m_flCritTime();

		if (Vars::Debug::Logging.Value)
		{
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): aCriticalStorage[i].m_flCritTokenBucket = {}", i, uintptr_t(pWeapon), pWeapon->m_flCritTokenBucket()).c_str(), { 100, 150, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): aCriticalStorage[i].m_nCritChecks = {}", i, uintptr_t(pWeapon), pWeapon->m_nCritChecks()).c_str(), { 100, 150, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): aCriticalStorage[i].m_nCritSeedRequests = {}", i, uintptr_t(pWeapon), pWeapon->m_nCritSeedRequests()).c_str(), { 100, 150, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): aCriticalStorage[i].m_flLastRapidFireCritCheckTime = {}", i, uintptr_t(pWeapon), pWeapon->m_flLastRapidFireCritCheckTime()).c_str(), { 100, 150, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): aCriticalStorage[i].m_flCritTime = {}", i, uintptr_t(pWeapon), pWeapon->m_flCritTime()).c_str(), { 100, 150, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}
	}

	bool bReturn = CALL_ORIGINAL(entmsg);

	pLocalEnt = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	if (!pLocalEnt)
		return bReturn;

	pLocal = pLocalEnt->As<CTFPlayer>();
	if (!pLocal || !pLocal->IsPlayer())
		return bReturn;

	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		auto& tStorage = aCriticalStorage[i];
		auto pWeapon = tStorage.m_hWeapon.Get();
		if (!pWeapon || pWeapon->IsPlayer() || pLocal->GetWeaponFromSlot(i) != pWeapon
			|| I::ClientEntityList->GetClientEntity(pWeapon->entindex()) != pWeapon)
			continue;

		pWeapon->m_flCritTokenBucket() = tStorage.m_flCritTokenBucket;
		pWeapon->m_nCritChecks() = tStorage.m_nCritChecks;
		pWeapon->m_nCritSeedRequests() = tStorage.m_nCritSeedRequests;
		pWeapon->m_flLastRapidFireCritCheckTime() = tStorage.m_flLastRapidFireCritCheckTime;
		pWeapon->m_flCritTime() = tStorage.m_flCritTime;

		if (Vars::Debug::Logging.Value)
		{
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): pWeapon->m_flCritTokenBucket() = {}", i, uintptr_t(pWeapon), pWeapon->m_flCritTokenBucket()).c_str(), { 100, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): pWeapon->m_nCritChecks() = {}", i, uintptr_t(pWeapon), pWeapon->m_nCritChecks()).c_str(), { 100, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): pWeapon->m_nCritSeedRequests() = {}", i, uintptr_t(pWeapon), pWeapon->m_nCritSeedRequests()).c_str(), { 100, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): pWeapon->m_flLastRapidFireCritCheckTime() = {}", i, uintptr_t(pWeapon), pWeapon->m_flLastRapidFireCritCheckTime()).c_str(), { 100, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): pWeapon->m_flCritTime() = {}", i, uintptr_t(pWeapon), pWeapon->m_flCritTime()).c_str(), { 100, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}
	}

	return bReturn;
}
