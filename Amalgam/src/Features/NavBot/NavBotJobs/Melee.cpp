#include "NavBotJobs.h"

static bool ApproachMeleeTarget(CUserCmd* pCmd, CTFPlayer* pLocal, const Vector& vTargetOrigin)
{
	// Crouch if we are standing on someone
	if (pLocal->m_hGroundEntity().Get() && pLocal->m_hGroundEntity().Get()->IsPlayer())
		pCmd->buttons |= IN_DUCK;

	SDK::WalkTo(pCmd, pLocal, vTargetOrigin);
	F::NavEngine.CancelPath();
	F::NavEngine.m_eCurrentPriority = PriorityListEnum::MeleeAttack;
	return true;
}

static Vector GetSpyBackstabSpot(CTFPlayer* pLocal, CTFPlayer* pPlayer)
{
	Vec3 vForward;
	Math::AngleVectors(pPlayer->GetEyeAngles(), &vForward);
	vForward.z = 0.f;
	if (vForward.Normalize() <= 0.01f)
		return pPlayer->GetAbsOrigin();

	Vector vSide(-vForward.y, vForward.x, 0.f);
	const float flSideSign = (pLocal->entindex() + pPlayer->entindex()) % 2 ? 1.f : -1.f;
	Vector vBackstabSpot = pPlayer->GetAbsOrigin() - vForward * 68.f + vSide * (flSideSign * 28.f);

	CNavArea* pBackstabArea = F::NavEngine.FindClosestNavArea(vBackstabSpot);
	if (!pBackstabArea || pBackstabArea->IsBlocked(pLocal->m_iTeamNum()))
		vBackstabSpot = pPlayer->GetAbsOrigin() - vForward * 54.f;

	return vBackstabSpot;
}

bool CNavBotMelee::Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, ClosestEnemy_t tClosestEnemy)
{
	if (iSlot != SLOT_MELEE || F::NavBotReload.m_iLastReloadSlot != -1)
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack)
			F::NavEngine.CancelPath();
		return false;
	}

	auto pEntity = I::ClientEntityList->GetClientEntity(tClosestEnemy.m_iEntIdx);
	if (!pEntity || pEntity->IsDormant())
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch)
		return false;

	// Too far away
	if (tClosestEnemy.m_flDist > Vars::Misc::Movement::NavBot::MeleeTargetRange.Value)
		return false;

	// Too high priority, so don't try
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::MeleeAttack)
		return false;

	static bool bIsVisible = false;
	static Timer tVischeckCooldown{};
	if (tVischeckCooldown.Run(0.2f))
	{
		CGameTrace trace;
		CTraceFilterHitscan filter(pLocal);
		SDK::TraceHull(pLocal->GetShootPos(), pPlayer->GetAbsOrigin(), pLocal->m_vecMins() * 0.3f, pLocal->m_vecMaxs() * 0.3f, MASK_PLAYERSOLID, &filter, &trace);
		bIsVisible = trace.DidHit() ? trace.m_pEnt && trace.m_pEnt == pPlayer : true;
	}

	Vector vTargetOrigin = pPlayer->GetAbsOrigin();
	Vector vLocalOrigin = pLocal->GetAbsOrigin();

	if (pLocal->m_iClass() == TF_CLASS_SPY)
	{
		auto pKnife = pLocal->GetWeaponFromSlot(SLOT_MELEE);
		const bool bReadyToBackstab = pKnife && pKnife->GetWeaponID() == TF_WEAPON_KNIFE && pKnife->As<CTFKnife>()->m_bReadyToBackstab();
		const bool bKnifeLethal = pKnife && pPlayer->m_iHealth() <= pKnife->GetDamage(false);
		const bool bCanSwing = bReadyToBackstab || bKnifeLethal;
		if (!bCanSwing)
			pCmd->buttons &= ~IN_ATTACK;

		Vector vBackstabSpot = GetSpyBackstabSpot(pLocal, pPlayer);
		if (vLocalOrigin.DistTo(vBackstabSpot) < 100.0f && bIsVisible)
			return ApproachMeleeTarget(pCmd, pLocal, bCanSwing ? vTargetOrigin : vBackstabSpot);

		static Timer tSpyMeleeCooldown{};
		float flDistToSpot = vLocalOrigin.DistTo(vBackstabSpot);
		if (!tSpyMeleeCooldown.Run(flDistToSpot < 200.f ? 0.1f : flDistToSpot < 1000.f ? 0.3f : 1.f) && F::NavEngine.IsPathing())
			return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

		if (F::NavEngine.NavTo(vBackstabSpot, PriorityListEnum::MeleeAttack, true, !F::NavEngine.IsPathing()))
			return true;

		return false;
	}

	// If we are close enough, don't even bother with using the navparser to get there
	if (tClosestEnemy.m_flDist < 100.0f && bIsVisible)
		return ApproachMeleeTarget(pCmd, pLocal, vTargetOrigin);

	// Don't constantly path, it's slow.
	// The closer we are, the more we should try to path
	static Timer tMeleeCooldown{};
	if (!tMeleeCooldown.Run(tClosestEnemy.m_flDist < 100.f ? 0.2f : tClosestEnemy.m_flDist < 1000.f ? 0.5f : 2.f) && F::NavEngine.IsPathing())
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

	// Just walk at the enemy l0l
	if (F::NavEngine.NavTo(vTargetOrigin, PriorityListEnum::MeleeAttack, true, !F::NavEngine.IsPathing()))
		return true;
	return false;
}
