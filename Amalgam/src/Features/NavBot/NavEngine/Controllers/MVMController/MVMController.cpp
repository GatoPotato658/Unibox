#include "MVMController.h"
#include "../../NavEngine.h"
#include "../../../BotUtils.h"
#include "../../../NavBotJobs/NavBotJobs.h"

static bool IsMadMilk(CTFWeaponBase* pWeapon)
{
	return pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_JAR_MILK;
}

static bool SlotHasShot(int iSlot)
{
	const WeaponAmmoInfo_t& tAmmoInfo = G::AmmoInSlot[iSlot];
	if (!tAmmoInfo.m_bUsesAmmo)
		return true;

	if (tAmmoInfo.m_iMaxClip == WEAPON_NOCLIP)
		return tAmmoInfo.m_iReserve > 0;

	return tAmmoInfo.m_iClip > 0;
}

static float GetPrimaryRange(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!pLocal || !pWeapon)
		return 300.f;

	switch (pLocal->m_iClass())
	{
	case TF_CLASS_PYRO:
		return 285.f;
	case TF_CLASS_SCOUT:
		return 520.f;
	case TF_CLASS_SNIPER:
		return 1800.f;
	default:
		return std::max(300.f, pWeapon->GetRange());
	}
}

static bool IsVisibleToShoot(CTFPlayer* pLocal, CBaseEntity* pTarget)
{
	if (!pLocal || !pTarget)
		return false;

	return F::NavEngine.IsVectorVisibleNavigation(pLocal->GetEyePosition(), pTarget->GetCenter(), MASK_SHOT | CONTENTS_GRATE);
}

bool CMVMController::IsSupportedClass(CTFPlayer* pLocal) const
{
	if (!pLocal)
		return false;
	const int iClass = pLocal->m_iClass();
	return iClass >= TF_CLASS_SCOUT && iClass <= TF_CLASS_ENGINEER;
}

bool CMVMController::PrimaryHasAmmo() const
{
	return SlotHasShot(SLOT_PRIMARY);
}

bool CMVMController::DesiredCombatWeaponCanFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) const
{
	if (!pLocal || !pWeapon)
		return false;

	if (pLocal->m_iClass() == TF_CLASS_SCOUT)
	{
		auto pSecondary = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
		if (IsMadMilk(pSecondary) && SlotHasShot(SLOT_SECONDARY))
			return true;

		return SlotHasShot(SLOT_PRIMARY);
	}

	return SlotHasShot(SLOT_PRIMARY);
}

bool CMVMController::GetTankTarget(CBaseEntity*& pOut) const
{
	pOut = nullptr;
	float flBestDistance = FLT_MAX;
	CTFPlayer* pLocal = H::Entities.GetLocal();
	const Vector vLocalOrigin = pLocal ? pLocal->GetAbsOrigin() : Vector();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
	{
		if (!pEntity || pEntity->IsDormant() || pEntity->GetClassID() != ETFClassID::CTFTankBoss)
			continue;

		const float flDistance = pLocal ? vLocalOrigin.DistTo(pEntity->GetAbsOrigin()) : 0.f;
		if (flDistance >= flBestDistance)
			continue;

		flBestDistance = flDistance;
		pOut = pEntity;
	}

	return pOut != nullptr;
}

bool CMVMController::GetRobotTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity*& pOut) const
{
	pOut = nullptr;
	if (!pLocal)
		return false;

	float flBestDistance = FLT_MAX;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (!pEntity || !pEntity->IsPlayer())
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive() || pPlayer == pLocal || pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;

		Vector vOrigin;
		if (pEntity->IsDormant())
		{
			if (!F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
				continue;
		}
		else
		{
			vOrigin = pEntity->GetAbsOrigin();
		}

		const float flDistance = vLocalOrigin.DistTo(vOrigin);
		if (flDistance >= flBestDistance)
			continue;

		flBestDistance = flDistance;
		pOut = pEntity;
	}

	if (!pOut)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
		{
			if (!pEntity)
				continue;
			const ETFClassID iClass = pEntity->GetClassID();
			if (iClass != ETFClassID::CObjectSentrygun && iClass != ETFClassID::CObjectDispenser && iClass != ETFClassID::CObjectTeleporter)
				continue;
			if (iClass == ETFClassID::CObjectTeleporter)
			{
				auto pTele = pEntity->As<CObjectTeleporter>();
				if (pTele && pTele->m_iObjectMode() != 1)
					continue;
			}
			Vector vOrigin;
			if (pEntity->IsDormant())
			{
				if (!F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
					continue;
			}
			else
			{
				auto pObject = pEntity->As<CBaseObject>();
				if (!pObject || pObject->m_bCarried() || pObject->m_bBuilding() || pObject->m_bPlacing())
					continue;
				vOrigin = pEntity->GetAbsOrigin();
			}
			const float flDistance = vLocalOrigin.DistTo(vOrigin);
			if (flDistance >= flBestDistance)
				continue;
			flBestDistance = flDistance;
			pOut = pEntity;
		}
	}

	return pOut != nullptr;
}

bool CMVMController::GetMoneyTarget(CTFPlayer* pLocal, CBaseEntity*& pOut) const
{
	pOut = nullptr;
	if (!pLocal || pLocal->m_iClass() != TF_CLASS_SCOUT)
		return false;

	float flBestDistance = 1600.f;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PickupMoney))
	{
		if (!pEntity || pEntity->IsDormant())
			continue;

		auto pMoney = pEntity->As<CCurrencyPack>();
		if (pMoney->m_bDistributed())
			continue;

		const float flDistance = vLocalOrigin.DistTo(pEntity->GetAbsOrigin());
		if (flDistance >= flBestDistance)
			continue;

		flBestDistance = flDistance;
		pOut = pEntity;
	}

	return pOut != nullptr;
}

void CMVMController::RefreshSpawnAnchors(CTFPlayer* pLocal)
{
	if (!pLocal || !m_tAnchorRefresh.Run(2.f))
		return;

	m_vSpawnAnchors.clear();
	for (const auto& tRoom : F::NavEngine.GetRespawnRooms())
	{
		if (!tRoom.tData.m_vCenter.IsZero() && tRoom.m_iTeam != pLocal->m_iTeamNum())
			m_vSpawnAnchors.emplace_back(tRoom.tData.m_vCenter);
	}

	if (!F::NavEngine.IsNavMeshLoaded())
		return;

	const uint32_t uEnemySpawnFlag = pLocal->m_iTeamNum() == TF_TEAM_RED ? TF_NAV_SPAWN_ROOM_BLUE : TF_NAV_SPAWN_ROOM_RED;
	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		if (tArea.m_iTFAttributeFlags & uEnemySpawnFlag)
			m_vSpawnAnchors.emplace_back(tArea.m_vCenter);
	}

	if (m_vSpawnAnchors.empty())
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
		{
			if (pEntity && !pEntity->IsDormant())
				m_vSpawnAnchors.emplace_back(pEntity->GetCenter());
		}
	}
}

bool CMVMController::GetFrontlineTarget(CTFPlayer* pLocal, Vector& vOut)
{
	if (!pLocal)
		return false;

	RefreshSpawnAnchors(pLocal);

	CBaseEntity* pRobot = nullptr;
	if (GetRobotTarget(pLocal, H::Entities.GetWeapon(), pRobot) && pRobot)
	{
		vOut = pRobot->GetAbsOrigin();
		return true;
	}

	if (!m_vSpawnAnchors.empty())
	{
		const Vector vLocalOrigin = pLocal->GetAbsOrigin();
		float flBestDistance = FLT_MAX;
		for (const Vector& vAnchor : m_vSpawnAnchors)
		{
			const float flDistance = vLocalOrigin.DistTo(vAnchor);
			if (flDistance >= flBestDistance)
				continue;

			flBestDistance = flDistance;
			vOut = vAnchor;
		}
		return true;
	}

	return false;
}

bool CMVMController::RunTank(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTank)
{
	if (!pCmd || !pLocal || !pWeapon || !pTank)
		return false;

	m_eTask = MVMTaskEnum::Tank;
	if (F::BotUtils.m_iCurrentSlot != SLOT_PRIMARY)
		F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);

	const Vector vTarget = pTank->GetCenter();
	const float flDistance = pLocal->GetAbsOrigin().DistTo(vTarget);
	const float flRange = GetPrimaryRange(pLocal, pWeapon);
	pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), vTarget);

	if (flDistance > flRange * 0.85f)
		F::NavEngine.NavTo(pTank->GetAbsOrigin(), PriorityListEnum::MVMTank, true, flDistance > 260.f);
	else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMTank && F::NavEngine.IsPathing())
		F::NavEngine.CancelPath();

	if (F::BotUtils.m_iCurrentSlot == SLOT_PRIMARY && PrimaryHasAmmo() && flDistance <= flRange && IsVisibleToShoot(pLocal, pTank))
		pCmd->buttons |= IN_ATTACK;

	return true;
}

bool CMVMController::RunCombat(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget)
{
	if (!pCmd || !pLocal || !pWeapon || !pTarget)
		return false;

	m_eTask = MVMTaskEnum::Combat;
	const Vector vTarget = pTarget->GetCenter();
	const float flDistance = pLocal->GetAbsOrigin().DistTo(vTarget);
	const int iClass = pLocal->m_iClass();
	float flRange = GetPrimaryRange(pLocal, pWeapon);
	if (iClass == TF_CLASS_SCOUT || iClass == TF_CLASS_PYRO)
		flRange = 110.f;
	else if (iClass == TF_CLASS_SPY)
		flRange = 800.f;

	if (iClass == TF_CLASS_SCOUT)
	{
		auto pSecondary = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
		if (IsMadMilk(pSecondary) && SlotHasShot(SLOT_SECONDARY) && flDistance <= 620.f)
			F::BotUtils.SetSlot(pLocal, SLOT_SECONDARY);
		else if (iClass == TF_CLASS_SCOUT && flDistance <= 90.f)
			F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
		else
			F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
	}
	else if (iClass == TF_CLASS_SPY)
	{
		if (pTarget->IsBuilding())
		{
			auto pBuilding = pTarget->As<CBaseObject>();
			if (!pBuilding->m_bHasSapper() && flDistance < 130.f)
			{
				F::BotUtils.SetSlot(pLocal, SLOT_SECONDARY);
				pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), vTarget);
				if (flDistance < 90.f && IsVisibleToShoot(pLocal, pTarget))
					pCmd->buttons |= IN_ATTACK;
				if (flDistance > flRange * 0.8f)
					F::NavEngine.NavTo(pTarget->GetAbsOrigin(), PriorityListEnum::MVMCombat, true, flDistance > 220.f);
				return true;
			}
		}
		if (pTarget->IsPlayer())
		{
			auto pTargPlayer = pTarget->As<CTFPlayer>();
			Vec3 vFwd; Math::AngleVectors(pTargPlayer->GetEyeAngles(), &vFwd);
			vFwd.z = 0.f; Vec3 vToLocal = pLocal->GetAbsOrigin() - pTargPlayer->GetAbsOrigin(); vToLocal.z = 0.f;
			bool bBehind = vFwd.Normalize() > 0.01f && vToLocal.Normalize() > 0.01f && vFwd.Dot(vToLocal) < -0.3f;
			auto pKnife = pLocal->GetWeaponFromSlot(SLOT_MELEE);
			bool bReady = pKnife && pKnife->GetWeaponID() == TF_WEAPON_KNIFE && pKnife->As<CTFKnife>()->m_bReadyToBackstab();
			if ((bReady || bBehind) && flDistance < 200.f)
				F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
			else
				F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
		}
		else
			F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
	}
	else if (iClass == TF_CLASS_PYRO && flDistance <= 85.f)
	{
		F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
	}
	else
		F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);

	pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), vTarget);

	if (iClass == TF_CLASS_PYRO || iClass == TF_CLASS_SCOUT)
	{
		if (flDistance > 85.f)
			F::NavEngine.NavTo(pTarget->GetAbsOrigin(), PriorityListEnum::MVMCombat, true, flDistance > 150.f);
		else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMCombat && F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();
	}
	else
	{
		if (flDistance > flRange * 0.8f)
			F::NavEngine.NavTo(pTarget->GetAbsOrigin(), PriorityListEnum::MVMCombat, true, flDistance > 220.f);
		else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMCombat && F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();
	}

	if (F::BotUtils.m_iCurrentSlot == SLOT_SECONDARY && IsMadMilk(pWeapon) && G::CanPrimaryAttack && flDistance <= 620.f && IsVisibleToShoot(pLocal, pTarget))
		pCmd->buttons |= IN_ATTACK;
	else if (F::BotUtils.m_iCurrentSlot == SLOT_PRIMARY && PrimaryHasAmmo() && flDistance <= flRange && IsVisibleToShoot(pLocal, pTarget))
		pCmd->buttons |= IN_ATTACK;
	else if (F::BotUtils.m_iCurrentSlot == SLOT_MELEE && flDistance <= 90.f && IsVisibleToShoot(pLocal, pTarget))
		pCmd->buttons |= IN_ATTACK;

	return true;
}

bool CMVMController::RunMoney(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseEntity* pMoney)
{
	if (!pCmd || !pLocal || !pMoney)
		return false;

	m_eTask = MVMTaskEnum::Money;
	const Vector vOrigin = pMoney->GetAbsOrigin();
	const float flDistance = pLocal->GetAbsOrigin().DistTo(vOrigin);
	if (flDistance <= 65.f)
	{
		SDK::WalkTo(pCmd, pLocal, vOrigin);
		return true;
	}

	return F::NavEngine.NavTo(vOrigin, PriorityListEnum::MVMMoney, true, flDistance > 180.f);
}

bool CMVMController::RunFrontline(CTFPlayer* pLocal)
{
	if (!pLocal)
		return false;

	Vector vTarget = {};
	bool bHasTarget = GetFrontlineTarget(pLocal, vTarget) && !vTarget.IsZero();
	if (!bHasTarget)
	{
		RefreshSpawnAnchors(pLocal);
		if (!m_vSpawnAnchors.empty())
		{
			vTarget = m_vSpawnAnchors.front();
			bHasTarget = true;
		}
		else if (auto pArea = F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin()))
		{
			vTarget = pArea->m_vCenter;
			Vector vFwd; Math::AngleVectors(pLocal->GetEyeAngles(), &vFwd);
			vFwd.z = 0.f; vFwd.Normalize();
			vTarget += vFwd * 600.f;
			if (auto pNear = F::NavEngine.FindClosestNavArea(vTarget, false))
				vTarget = pNear->m_vCenter;
			bHasTarget = true;
		}
	}
	if (!bHasTarget || vTarget.IsZero())
		return false;

	m_eTask = MVMTaskEnum::Frontline;
	const float flDistance = pLocal->GetAbsOrigin().DistTo(vTarget);
	if (flDistance < 120.f)
		return true;

	return F::NavEngine.NavTo(vTarget, PriorityListEnum::MVMFrontline, true, flDistance > 180.f);
}

void CMVMController::Update()
{
	auto pGameRules = I::TFGameRules();
	m_bActive = pGameRules && pGameRules->m_bPlayingMannVsMachine();
	if (!m_bActive)
		m_eTask = MVMTaskEnum::None;
}

void CMVMController::Reset()
{
	m_bActive = false;
	m_eTask = MVMTaskEnum::None;
	m_vSpawnAnchors.clear();
}

bool CMVMController::WantsPrimary(CTFPlayer* pLocal) const
{
	if (!m_bActive || !IsSupportedClass(pLocal))
		return false;

	return m_eTask == MVMTaskEnum::Tank || pLocal->m_iClass() == TF_CLASS_PYRO || pLocal->m_iClass() == TF_CLASS_SNIPER;
}

bool CMVMController::WantsScoutSecondary(CTFPlayer* pLocal) const
{
	if (!m_bActive || !pLocal || pLocal->m_iClass() != TF_CLASS_SCOUT || m_eTask == MVMTaskEnum::Tank)
		return false;

	auto pSecondary = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
	return IsMadMilk(pSecondary) && SlotHasShot(SLOT_SECONDARY);
}

bool CMVMController::Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_eTask = MVMTaskEnum::None;
	if (!m_bActive || !IsSupportedClass(pLocal) || !pCmd || !pWeapon)
		return false;

	const float flHealth = static_cast<float>(pLocal->m_iHealth()) / std::max(1, pLocal->GetMaxHealth());
	const int iClass = pLocal->m_iClass();
	const bool bScoutPyro = iClass == TF_CLASS_SCOUT || iClass == TF_CLASS_PYRO;
	const bool bSpy = iClass == TF_CLASS_SPY;
	const bool bFrontline = iClass == TF_CLASS_SNIPER || iClass == TF_CLASS_HEAVYWEAPONS || iClass == TF_CLASS_DEMOMAN || iClass == TF_CLASS_SOLDIER;

	if (bScoutPyro || bSpy)
	{
		if (flHealth < 0.56f)
		{
			if (F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::Forced))
			{
				m_eTask = MVMTaskEnum::Health;
				return true;
			}
			if (F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced))
			{
				m_eTask = MVMTaskEnum::Ammo;
				return true;
			}
		}
	}

	CBaseEntity* pTank = nullptr;
	if (GetTankTarget(pTank))
		return RunTank(pCmd, pLocal, pWeapon, pTank);

	CBaseEntity* pRobot = nullptr;
	if (GetRobotTarget(pLocal, pWeapon, pRobot))
		return RunCombat(pCmd, pLocal, pWeapon, pRobot);

	CBaseEntity* pMoney = nullptr;
	if (GetMoneyTarget(pLocal, pMoney) && RunMoney(pCmd, pLocal, pMoney))
		return true;

	if (bFrontline)
	{
		if (flHealth < 0.78f && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::Forced))
		{
			m_eTask = MVMTaskEnum::Health;
			return true;
		}
		if (!DesiredCombatWeaponCanFire(pLocal, pWeapon) && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced))
		{
			m_eTask = MVMTaskEnum::Ammo;
			return true;
		}
	}
	else if (!bScoutPyro && !bSpy)
	{
		if (flHealth < 0.35f && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::Forced))
		{
			m_eTask = MVMTaskEnum::Health;
			return true;
		}
		if (!DesiredCombatWeaponCanFire(pLocal, pWeapon) && F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced))
		{
			m_eTask = MVMTaskEnum::Ammo;
			return true;
		}
	}

	return RunFrontline(pLocal);
}
