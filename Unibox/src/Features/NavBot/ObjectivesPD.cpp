#include "Objectives.h"
#include "NavEngine.h"
#include "BotUtils.h"

using namespace ObjectiveUtils;

namespace
{
	constexpr float kMaxCollectDistance = 3500.f;
	constexpr float kMaxCollectPathCost = 9000.f;

	int GetPlayerDestructionPoints(CBaseEntity* pItem)
	{
		if (!pItem || pItem->GetClassID() != ETFClassID::CCaptureFlag)
			return 0;

		auto pFlag = pItem->As<CCaptureFlag>();
		return pFlag->m_nType() == TF_FLAGTYPE_PLAYER_DESTRUCTION ? pFlag->m_nPointValue() : 0;
	}

	CTFPlayerDestructionLogic* FindLogic(int& iCachedIdx)
	{
		if (iCachedIdx > 0)
		{
			if (auto pCached = I::ClientEntityList->GetClientEntity(iCachedIdx))
			{
				auto pEntity = pCached->As<CBaseEntity>();
				if (pEntity->GetClassID() == ETFClassID::CTFPlayerDestructionLogic)
					return pEntity->As<CTFPlayerDestructionLogic>();
			}
		}

		iCachedIdx = -1;
		return nullptr;
	}
}

void CPDController::Init()
{
	m_vPickups.clear();
	m_vZones.clear();
	m_iLogicIdx = -1;
	m_tLogicScan -= 10.f;
}

void CPDController::Update()
{
	CollectCaptureZones(m_vZones);

	m_vPickups.clear();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		if (pFlag->m_nType() != TF_FLAGTYPE_PLAYER_DESTRUCTION || pFlag->m_bDisabled() || pFlag->m_nFlagStatus() != TF_FLAGINFO_DROPPED)
			continue;

		const int iPoints = pFlag->m_nPointValue();
		if (iPoints > 0)
			m_vPickups.push_back({ pFlag, pFlag->GetAbsOrigin(), iPoints });
	}

	if (FindLogic(m_iLogicIdx) || !m_tLogicScan.Run(1.f))
		return;

	const int iMaxClients = I::EngineClient->GetMaxClients();
	const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int n = iMaxClients + 1; n <= iHighest; n++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
		if (pClientEntity && pClientEntity->As<CBaseEntity>()->GetClassID() == ETFClassID::CTFPlayerDestructionLogic)
		{
			m_iLogicIdx = n;
			return;
		}
	}
}

int CPDController::GetCarriedPoints(CTFPlayer* pPlayer) const
{
	return pPlayer ? GetPlayerDestructionPoints(pPlayer->m_hItem().Get()) : 0;
}

CTFPlayer* CPDController::GetTeamLeader(int iTeam) const
{
	int iIdx = m_iLogicIdx;
	auto pLogic = FindLogic(iIdx);
	return pLogic ? pLogic->GetTeamLeader(iTeam) : nullptr;
}

const CaptureZone_t* CPDController::GetZoneAt(const Vector& vPos, int iTeam) const
{
	for (const auto& tZone : m_vZones)
	{
		if ((tZone.m_iTeam == TEAM_UNASSIGNED || tZone.m_iTeam == iTeam) && tZone.Contains(vPos, 8.f))
			return &tZone;
	}
	return nullptr;
}

bool CPDController::GetDeliveryGoal(CTFPlayer* pLocal, bool bPathCosts, PDGoal_t& tOut) const
{
	const int iCarried = GetCarriedPoints(pLocal);
	if (iCarried <= 0)
		return false;

	const int iTeam = pLocal->m_iTeamNum();
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	std::vector<Vector> vStandPositions;
	for (const auto& tZone : m_vZones)
	{
		Vector vStand;
		if ((tZone.m_iTeam == TEAM_UNASSIGNED || tZone.m_iTeam == iTeam) && FindZoneStandPos(tZone, vLocalOrigin, vStand))
			vStandPositions.push_back(vStand);
	}
	if (vStandPositions.empty())
		return false;

	const int iBest = PickNearest(vStandPositions, vLocalOrigin, bPathCosts);
	if (iBest == -1)
		return false;

	tOut.m_eKind = PDGoalEnum::Deliver;
	tOut.m_vPos = vStandPositions[iBest];
	tOut.m_iCarried = iCarried;
	return true;
}

bool CPDController::GetCollectGoal(CTFPlayer* pLocal, bool bPathCosts, PDGoal_t& tOut) const
{
	if (m_vPickups.empty())
		return false;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	std::vector<Vector> vPositions;
	for (const auto& tPickup : m_vPickups)
	{
		if (tPickup.m_vPos.DistTo(vLocalOrigin) <= kMaxCollectDistance)
			vPositions.push_back(tPickup.m_vPos);
	}
	if (vPositions.empty())
		return false;

	const int iBest = PickNearest(vPositions, vLocalOrigin, bPathCosts, kMaxCollectPathCost);
	if (iBest == -1)
		return false;

	tOut.m_eKind = PDGoalEnum::Collect;
	tOut.m_vPos = vPositions[iBest];
	return true;
}

bool CPDController::GetGoal(CTFPlayer* pLocal, bool bPathCosts, PDGoal_t& tOut)
{
	tOut = {};
	if (!pLocal)
		return false;

	if (GetDeliveryGoal(pLocal, bPathCosts, tOut) || GetCollectGoal(pLocal, bPathCosts, tOut))
		return true;

	const int iTeam = pLocal->m_iTeamNum();
	const int iEnemyTeam = iTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
	Vector vPos = {};

	if (auto pEnemyLeader = GetTeamLeader(iEnemyTeam))
	{
		if (pEnemyLeader->IsAlive() && F::BotUtils.GetDormantOrigin(pEnemyLeader->entindex(), &vPos))
		{
			tOut.m_eKind = PDGoalEnum::Hunt;
			tOut.m_vPos = vPos;
			tOut.m_iTargetIdx = pEnemyLeader->entindex();
			return true;
		}
	}

	auto pLeader = GetTeamLeader(iTeam);
	if (pLeader && pLeader != pLocal && pLeader->IsAlive() && F::BotUtils.ShouldAssist(pLocal, pLeader->entindex())
		&& F::BotUtils.GetDormantOrigin(pLeader->entindex(), &vPos))
	{
		tOut.m_eKind = PDGoalEnum::Escort;
		tOut.m_vPos = vPos;
		tOut.m_iTargetIdx = pLeader->entindex();
		return true;
	}

	return false;
}
