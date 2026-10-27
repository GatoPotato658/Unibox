#include "Objectives.h"
#include "NavEngine.h"

using namespace ObjectiveUtils;

namespace
{
	constexpr float kMaxCollectPathCost = 9000.f;

	CCaptureFlag* GetCarriedRobotDestructionFlag(CTFPlayer* pPlayer)
	{
		auto pItem = pPlayer ? pPlayer->m_hItem().Get() : nullptr;
		if (!pItem || pItem->GetClassID() != ETFClassID::CCaptureFlag)
			return nullptr;

		auto pFlag = pItem->As<CCaptureFlag>();
		return pFlag->m_nType() == TF_FLAGTYPE_ROBOT_DESTRUCTION ? pFlag : nullptr;
	}
}

void CRDController::Init()
{
	m_vPickups.clear();
	m_vPacks.clear();
	m_vRobots.clear();
	m_vZones.clear();
	m_tScan -= 10.f;
}

void CRDController::ScanEntities()
{
	m_vPacks.clear();
	m_vRobots.clear();

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	const int iTeam = pLocal->m_iTeamNum();
	const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int n = I::EngineClient->GetMaxClients() + 1; n <= iHighest; n++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
		auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
		if (!pEntity || pEntity->IsDormant())
			continue;

		switch (pEntity->GetClassID())
		{
		case ETFClassID::CBonusPack:
		{
			const int iPackTeam = pEntity->m_iTeamNum();
			if (iPackTeam < TF_TEAM_RED || iPackTeam == iTeam)
				m_vPacks.push_back(pEntity->GetAbsOrigin());
			break;
		}
		case ETFClassID::CTFRobotDestruction_Robot:
			if (pEntity->m_iTeamNum() != iTeam && pEntity->As<CTFRobotDestruction_Robot>()->m_iHealth() > 0)
				m_vRobots.push_back(pEntity->GetAbsOrigin());
			break;
		default:
			break;
		}
	}
}

void CRDController::Update()
{
	CollectCaptureZones(m_vZones);

	if (m_tScan.Run(0.25f))
		ScanEntities();

	m_vPickups = m_vPacks;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		if (pFlag->m_nType() == TF_FLAGTYPE_ROBOT_DESTRUCTION && !pFlag->m_bDisabled()
			&& pFlag->m_nFlagStatus() == TF_FLAGINFO_DROPPED && pFlag->m_nPointValue() > 0)
			m_vPickups.push_back(pFlag->GetAbsOrigin());
	}
}

const CaptureZone_t* CRDController::GetZoneAt(const Vector& vPos, int iTeam) const
{
	for (const auto& tZone : m_vZones)
	{
		if ((tZone.m_iTeam == TEAM_UNASSIGNED || tZone.m_iTeam == iTeam) && tZone.Contains(vPos, 8.f))
			return &tZone;
	}
	return nullptr;
}

bool CRDController::GetGoal(CTFPlayer* pLocal, bool bPathCosts, RDGoal_t& tOut)
{
	tOut = {};
	if (!pLocal)
		return false;

	const int iTeam = pLocal->m_iTeamNum();
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	if (GetCarriedRobotDestructionFlag(pLocal))
	{
		std::vector<Vector> vStandPositions;
		for (const auto& tZone : m_vZones)
		{
			Vector vStand;
			if ((tZone.m_iTeam == TEAM_UNASSIGNED || tZone.m_iTeam == iTeam) && FindZoneStandPos(tZone, vLocalOrigin, vStand))
				vStandPositions.push_back(vStand);
		}

		const int iZone = PickNearest(vStandPositions, vLocalOrigin, bPathCosts);
		if (iZone != -1)
		{
			tOut.m_eKind = RDGoalEnum::Deliver;
			tOut.m_vPos = vStandPositions[iZone];
			return true;
		}
	}

	if (const int iPickup = PickNearest(m_vPickups, vLocalOrigin, bPathCosts, kMaxCollectPathCost); iPickup != -1)
	{
		tOut.m_eKind = RDGoalEnum::Collect;
		tOut.m_vPos = m_vPickups[iPickup];
		return true;
	}

	if (const int iRobot = PickNearest(m_vRobots, vLocalOrigin, false); iRobot != -1)
	{
		tOut.m_eKind = RDGoalEnum::Attack;
		tOut.m_vPos = m_vRobots[iRobot];
		return true;
	}

	return false;
}
