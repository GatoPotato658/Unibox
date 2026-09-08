#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"

MAKE_SIGNATURE(CTFPlayer_ShouldDraw, "client.dll", "48 89 74 24 ? 57 48 83 EC ? 48 8D 71", 0x0);
MAKE_SIGNATURE(CBasePlayer_ShouldDrawThisPlayer, "client.dll", "48 83 EC ? E8 ? ? ? ? 84 C0 74 ? 48 8B 0D ? ? ? ? 48 85 C9", 0x0);
MAKE_SIGNATURE(CBasePlayer_ShouldDrawLocalPlayer, "client.dll", "48 83 EC ? 48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 50 ? 84 C0 74 ? 8B 05", 0x0);
MAKE_SIGNATURE(CBaseCombatWeapon_ShouldDraw, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 83 B9 ? ? ? ? ? 48 8B D9 74 ? 8B 81", 0x0);
//MAKE_SIGNATURE(CTFWeaponBase_PostDataUpdate_ShouldDrawThisPlayer_Call, "client.dll", "84 C0 75 ? 8B BE", 0x0);
MAKE_SIGNATURE(CBasePlayer_BuildFirstPersonMeathookTransformations_ShouldDrawThisPlayer_Call, "client.dll", "84 C0 0F 84 ? ? ? ? 48 8B 94 24 ? ? ? ? 48 8B CE", 0x0);
//MAKE_SIGNATURE(CBaseCombatWeapon_CalcOverrideModelIndex_ShouldDrawLocalPlayer_Call, "client.dll", "84 C0 75 ? B8 ? ? ? ? 48 8B 5C 24", 0x0);

MAKE_HOOK(CTFPlayer_ShouldDraw, S::CTFPlayer_ShouldDraw(), bool,
	void* rcx)
{
	DEBUG_RETURN(CTFPlayer_ShouldDraw, rcx);

#ifdef TEXTMODE
	return false;
#else
	if (F::Spectate.HasTarget() && !I::EngineClient->IsHLTV())
	{
		auto pLocal = H::Entities.GetLocal();
		auto pBaseTarget = I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(F::Spectate.GetTarget()));
		auto pTarget = pBaseTarget ? pBaseTarget->As<CTFPlayer>() : nullptr;
		if (pLocal && pLocal->IsAlive() && rcx == pLocal->GetClientRenderable())
			return true;
		else if (pTarget && pTarget->IsPlayer() && pTarget->IsAlive() && rcx == pTarget->GetClientRenderable())
			return Vars::Visuals::Thirdperson::Enabled.Value;
	}

	return CALL_ORIGINAL(rcx);
#endif
}

MAKE_HOOK(CBasePlayer_ShouldDrawThisPlayer, S::CBasePlayer_ShouldDrawThisPlayer(), bool,
	void* rcx)
{
	DEBUG_RETURN(CBasePlayer_ShouldDrawThisPlayer, rcx);

#ifdef TEXTMODE
	return false;
#else
	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	//const auto dwDesired = S::CTFWeaponBase_PostDataUpdate_ShouldDrawThisPlayer_Call();
	const auto dwUndesired = S::CBasePlayer_BuildFirstPersonMeathookTransformations_ShouldDrawThisPlayer_Call();

	//if (dwRetAddr == dwDesired)
	//	return false; // breaks thirdperson jigglebones?

	if (F::Spectate.HasTarget() && !I::EngineClient->IsHLTV())
	{
		if (dwRetAddr == dwUndesired)
			return false;

		auto pLocal = H::Entities.GetLocal();
		auto pBaseTarget = I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(F::Spectate.GetTarget()));
		auto pTarget = pBaseTarget ? pBaseTarget->As<CTFPlayer>() : nullptr;
		if (pLocal && pLocal->IsAlive() && rcx == pLocal)
			return true;
		else if (pTarget && pTarget->IsPlayer() && pTarget->IsAlive() && rcx == pTarget)
			return Vars::Visuals::Thirdperson::Enabled.Value;
	}

	return CALL_ORIGINAL(rcx);
#endif
}

MAKE_HOOK(CBasePlayer_ShouldDrawLocalPlayer, S::CBasePlayer_ShouldDrawLocalPlayer(), bool,
	void* rcx)
{
	DEBUG_RETURN(CBasePlayer_ShouldDrawLocalPlayer, rcx);

#ifdef TEXTMODE
	return false;
#else
	//const auto dwRetAddr = uintptr_t(_ReturnAddress());
	//const auto dwDesired = S::CBaseCombatWeapon_CalcOverrideModelIndex_ShouldDrawLocalPlayer_Call();

	//if (dwRetAddr == dwDesired)
	//	return false;

	if (F::Spectate.HasTarget() && !I::EngineClient->IsHLTV())
	{
		auto pLocal = H::Entities.GetLocal();
		if (pLocal && pLocal->IsAlive())
			return true;
	}

	return CALL_ORIGINAL(rcx);
#endif
}

MAKE_HOOK(CBaseCombatWeapon_ShouldDraw, S::CBaseCombatWeapon_ShouldDraw(), bool,
	void* rcx)
{
	DEBUG_RETURN(CBaseCombatWeapon_ShouldDraw, rcx);

#ifdef TEXTMODE
	return false;
#else
	if (F::Spectate.HasTarget() && !I::EngineClient->IsHLTV())
	{
		auto pWeapon = H::Entities.GetWeapon();
		if (pWeapon && rcx == pWeapon->GetClientRenderable())
			return true;
	}

	return CALL_ORIGINAL(rcx);
#endif
}
