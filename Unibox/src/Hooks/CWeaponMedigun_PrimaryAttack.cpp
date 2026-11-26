#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"


struct Restore_t
{
	Vec3 m_vOrigin;
	Vec3 m_vMins;
	Vec3 m_vMaxs;
};

MAKE_HOOK(CWeaponMedigun_PrimaryAttack, S::CWeaponMedigun_PrimaryAttack(), void,
	void* rcx)
{
	DEBUG_RETURN(CWeaponMedigun_PrimaryAttack, rcx);

	auto pWeapon = reinterpret_cast<CWeaponMedigun*>(rcx);
	auto pOwner = pWeapon->m_hOwner()->As<CTFPlayer>();
	if (!pOwner || !pOwner->m_pCurrentCommand())
		return CALL_ORIGINAL(rcx);

	// do pseudo lagcomp for the client
	std::unordered_map<CBaseEntity*, Restore_t> mRestore = {};

	auto pCmd = pOwner->m_pCurrentCommand();
	float flTargetTime = TICKS_TO_TIME(pCmd->tick_count - TIME_TO_TICKS(F::Backtrack.GetFakeInterp()));
	static thread_local std::vector<TickRecord*> vRecords = {};
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer == pOwner || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
			continue;

		F::Backtrack.GetValidRecords(pEntity, vRecords);
		if (vRecords.empty())
			continue;

		for (auto pRecord : vRecords)
		{
			if (pRecord->m_flSimTime <= flTargetTime)
			{
				mRestore[pEntity] = { pEntity->GetAbsOrigin(), pEntity->m_vecMins(), pEntity->m_vecMaxs() };
				pEntity->SetAbsOrigin(pRecord->m_vOrigin);
				pEntity->m_vecMins() = pRecord->m_vMins;
				pEntity->m_vecMaxs() = pRecord->m_vMaxs;
				break;
			}
		}
	}

	CALL_ORIGINAL(rcx);

	for (auto& [pEntity, tRestore] : mRestore)
	{
		pEntity->SetAbsOrigin(tRestore.m_vOrigin);
		pEntity->m_vecMins() = tRestore.m_vMins;
		pEntity->m_vecMaxs() = tRestore.m_vMaxs;
	}
	mRestore.clear();
}