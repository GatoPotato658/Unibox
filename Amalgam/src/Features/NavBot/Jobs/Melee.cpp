#include "NavBotJobs.h"
#include "../Objectives.h"

static bool ApproachMeleeTarget(CUserCmd* pCmd, CTFPlayer* pLocal, const Vector& vTargetOrigin)
{
	auto pGroundEntity = pLocal->m_hGroundEntity().Get();
	if (pGroundEntity && pGroundEntity->IsPlayer())
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

bool CNavBotMelee::Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, const ClosestEnemy_t& tClosestEnemy)
{
	const auto Release = []() -> bool
		{
			if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack)
				F::NavEngine.CancelPath();
			return false;
		};

	if (iSlot != SLOT_MELEE || F::NavBotReload.m_iLastReloadSlot != -1)
		return Release();

	auto pEntity = I::ClientEntityList->GetClientEntity(tClosestEnemy.m_iEntIdx);
	if (!pEntity || pEntity->IsDormant())
		return Release();

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch)
		return Release();

	if (!F::VSHController.IsBossLocal() && tClosestEnemy.m_flDist > Vars::Misc::Movement::NavBot::MeleeTargetRange.Value)
		return Release();

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::MeleeAttack)
		return false;

	if (m_iVisibilityTarget != tClosestEnemy.m_iEntIdx || m_tVisibility.Run(0.2f))
	{
		m_iVisibilityTarget = tClosestEnemy.m_iEntIdx;
		m_tVisibility.Update();

		CGameTrace trace;
		CTraceFilterHitscan filter(pLocal);
		SDK::TraceHull(pLocal->GetShootPos(), pPlayer->GetAbsOrigin(), pLocal->m_vecMins() * 0.3f, pLocal->m_vecMaxs() * 0.3f, MASK_PLAYERSOLID, &filter, &trace);
		m_bTargetVisible = trace.DidHit() ? trace.m_pEnt && trace.m_pEnt == pPlayer : true;
	}

	const Vector vTargetOrigin = pPlayer->GetAbsOrigin();
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	if (pLocal->m_iClass() == TF_CLASS_SPY)
	{
		auto pKnife = pLocal->GetWeaponFromSlot(SLOT_MELEE);
		const bool bReadyToBackstab = pKnife && pKnife->GetWeaponID() == TF_WEAPON_KNIFE && pKnife->As<CTFKnife>()->m_bReadyToBackstab();
		const bool bKnifeLethal = pKnife && pPlayer->m_iHealth() <= pKnife->GetDamage(false);
		const bool bCanSwing = bReadyToBackstab || bKnifeLethal;
		if (!bCanSwing)
			pCmd->buttons &= ~IN_ATTACK;

		const Vector vBackstabSpot = GetSpyBackstabSpot(pLocal, pPlayer);
		const float flDistToSpot = vLocalOrigin.DistTo(vBackstabSpot);
		if (flDistToSpot < 100.f && m_bTargetVisible)
			return ApproachMeleeTarget(pCmd, pLocal, bCanSwing ? vTargetOrigin : vBackstabSpot);

		static Timer tSpyMeleeCooldown{};
		if (!tSpyMeleeCooldown.Run(flDistToSpot < 200.f ? 0.1f : flDistToSpot < 1000.f ? 0.3f : 1.f) && F::NavEngine.IsPathing())
			return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

		return F::NavEngine.NavTo(vBackstabSpot, PriorityListEnum::MeleeAttack);
	}

	if (tClosestEnemy.m_flDist < 100.f && m_bTargetVisible)
		return ApproachMeleeTarget(pCmd, pLocal, vTargetOrigin);

	static Timer tMeleeCooldown{};
	if (!tMeleeCooldown.Run(tClosestEnemy.m_flDist < 100.f ? 0.2f : tClosestEnemy.m_flDist < 1000.f ? 0.5f : 2.f) && F::NavEngine.IsPathing())
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

	return F::NavEngine.NavTo(vTargetOrigin, PriorityListEnum::MeleeAttack);
}

void CNavBotMelee::Reset()
{
	m_iVisibilityTarget = -1;
	m_bTargetVisible = false;
}
