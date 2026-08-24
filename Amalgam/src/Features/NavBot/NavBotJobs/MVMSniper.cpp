#include "NavBotJobs.h"

static CBaseObject* GetTeleporter(int iIdx, int iObjectMode)
{
	if (iIdx <= 0)
		return nullptr;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIdx);
	if (!pEntity || pEntity->GetClassID() != ETFClassID::CObjectTeleporter)
		return nullptr;

	auto pTeleporter = pEntity->As<CObjectTeleporter>();
	if (pTeleporter->m_iObjectMode() != iObjectMode || pTeleporter->m_bPlacing() || pTeleporter->m_bCarried() || pTeleporter->m_bDisabled())
		return nullptr;

	return pTeleporter;
}

static CBaseObject* GetDispenser(int iIdx)
{
	if (iIdx <= 0)
		return nullptr;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIdx);
	if (!pEntity || pEntity->GetClassID() != ETFClassID::CObjectDispenser)
		return nullptr;

	auto pDispenser = pEntity->As<CObjectDispenser>();
	if (pDispenser->m_bPlacing() || pDispenser->m_bCarried() || pDispenser->m_bDisabled())
		return nullptr;

	return pDispenser;
}

CBaseObject* CNavBotMVMSniper::FindClosestTeleporter(CTFPlayer* pLocal, int iObjectMode)
{
	CBaseObject* pBest = nullptr;
	float flBestDist = FLT_MAX;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CObjectTeleporter)
			continue;

		auto pTeleporter = pEntity->As<CObjectTeleporter>();
		if (pTeleporter->m_iObjectMode() != iObjectMode || pTeleporter->m_bPlacing() || pTeleporter->m_bCarried() || pTeleporter->m_bDisabled())
			continue;

		Vector vOrigin;
		if (pEntity->IsDormant())
		{
			if (!F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
				continue;
		}
		else
			vOrigin = pTeleporter->GetAbsOrigin();

		const float flDist = vLocalOrigin.DistToSqr(vOrigin);
		if (flDist >= flBestDist)
			continue;

		flBestDist = flDist;
		pBest = pTeleporter;
	}

	if (!pBest)
	{
		for (int i = 1; i <= I::ClientEntityList->GetHighestEntityIndex(); ++i)
		{
			auto pEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pEntity || pEntity->GetClassID() != ETFClassID::CObjectTeleporter)
				continue;
			auto pTeleporter = pEntity->As<CObjectTeleporter>();
			if (pTeleporter->m_iObjectMode() != iObjectMode || pTeleporter->m_bPlacing() || pTeleporter->m_bCarried() || pTeleporter->m_bDisabled())
				continue;
			if (pTeleporter->m_iTeamNum() != pLocal->m_iTeamNum())
				continue;
			Vector vOrigin;
			if (pEntity->IsDormant())
			{
				if (!F::BotUtils.GetDormantOrigin(i, &vOrigin))
					continue;
			}
			else
				vOrigin = pTeleporter->GetAbsOrigin();
			const float flDist = vLocalOrigin.DistToSqr(vOrigin);
			if (flDist >= flBestDist)
				continue;
			flBestDist = flDist;
			pBest = pTeleporter;
		}
	}

	return pBest;
}

CBaseObject* CNavBotMVMSniper::FindClosestDispenser(CTFPlayer* pLocal)
{
	CBaseObject* pBest = nullptr;
	float flBestDist = FLT_MAX;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CObjectDispenser)
			continue;

		auto pDispenser = pEntity->As<CObjectDispenser>();
		if (pDispenser->m_bPlacing() || pDispenser->m_bCarried() || pDispenser->m_bDisabled())
			continue;

		Vector vOrigin;
		if (pEntity->IsDormant())
		{
			if (!F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
				continue;
		}
		else
			vOrigin = pDispenser->GetAbsOrigin();

		const float flDist = vLocalOrigin.DistToSqr(vOrigin);
		if (flDist >= flBestDist)
			continue;

		flBestDist = flDist;
		pBest = pDispenser;
	}

	if (!pBest)
	{
		for (int i = 1; i <= I::ClientEntityList->GetHighestEntityIndex(); ++i)
		{
			auto pEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pEntity || pEntity->GetClassID() != ETFClassID::CObjectDispenser)
				continue;
			auto pDispenser = pEntity->As<CObjectDispenser>();
			if (pDispenser->m_bPlacing() || pDispenser->m_bCarried() || pDispenser->m_bDisabled())
				continue;
			if (pDispenser->m_iTeamNum() != pLocal->m_iTeamNum())
				continue;
			Vector vOrigin;
			if (pEntity->IsDormant())
			{
				if (!F::BotUtils.GetDormantOrigin(i, &vOrigin))
					continue;
			}
			else
				vOrigin = pDispenser->GetAbsOrigin();
			const float flDist = vLocalOrigin.DistToSqr(vOrigin);
			if (flDist >= flBestDist)
				continue;
			flBestDist = flDist;
			pBest = pDispenser;
		}
	}

	return pBest;
}

bool CNavBotMVMSniper::CampAt(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseEntity* pAnchor)
{
	if (!pAnchor)
		return false;

	const Vector vAnchorOrigin = pAnchor->GetAbsOrigin();
	const float flDist = pLocal->GetAbsOrigin().DistTo(vAnchorOrigin);
	if (flDist > 150.f)
	{
		F::NavEngine.NavTo(vAnchorOrigin, PriorityListEnum::MVMSniper, true, !F::NavEngine.IsPathing());
		return true;
	}

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMSniper && F::NavEngine.IsPathing())
		F::NavEngine.CancelPath();

	if (flDist > 40.f)
		SDK::WalkTo(pCmd, pLocal, vAnchorOrigin);

	return true;
}

bool CNavBotMVMSniper::Run(CUserCmd* pCmd, CTFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive())
	{
		Reset();
		return false;
	}

	const float flCurTime = I::GlobalVars->curtime;

	switch (m_eState)
	{
	case EState::ToEntrance:
	{
		auto pEntrance = GetTeleporter(m_iEntranceIdx, 0);
		if (!pEntrance)
		{
			if (flCurTime >= m_flScanClock)
			{
				pEntrance = FindClosestTeleporter(pLocal, 0);
				m_iEntranceIdx = pEntrance ? pEntrance->entindex() : -1;
				m_flLastEntranceDist = FLT_MAX;
				m_flOnEntranceSince = 0.f;
				m_flScanClock = flCurTime + 2.f;
			}

			if (!pEntrance)
			{
				// PLEASE STOP GOING TO DISPENCER WHEN TELE IS UP PUSSY
				if (auto pExit = FindClosestTeleporter(pLocal, 1))
				{
					m_iCampIdx = pExit->entindex();
					m_eState = EState::CampExit;
					m_flScanClock = 0.f;
					return CampAt(pCmd, pLocal, pExit);
				}

				if (auto pDispenser = FindClosestDispenser(pLocal))
				{
					m_iCampIdx = pDispenser->entindex();
					m_eState = EState::CampDispenser;
					m_flScanClock = 0.f;
					return CampAt(pCmd, pLocal, pDispenser);
				}
				return false;
			}
		}

		const float flDist = pLocal->GetAbsOrigin().DistTo(pEntrance->GetAbsOrigin());

		if (m_flLastEntranceDist < 200.f && flDist - m_flLastEntranceDist > 400.f)
		{
			m_eState = EState::CampExit;
			m_iCampIdx = -1;
			m_flScanClock = 0.f;
			return true;
		}
		m_flLastEntranceDist = flDist;

		if (flDist > 250.f)
		{
			m_flOnEntranceSince = 0.f;
			F::NavEngine.NavTo(pEntrance->GetAbsOrigin(), PriorityListEnum::MVMSniper, true, !F::NavEngine.IsPathing());
			return true;
		}

		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MVMSniper && F::NavEngine.IsPathing())
			F::NavEngine.CancelPath();

		if (!m_flOnEntranceSince && flDist <= 100.f)
			m_flOnEntranceSince = flCurTime;
		else if (flDist > 100.f)
			m_flOnEntranceSince = 0.f;

		if (m_flOnEntranceSince && flCurTime - m_flOnEntranceSince > 4.f)
		{
			if (auto pExit = FindClosestTeleporter(pLocal, 1))
			{
				const float flDistToExit = pLocal->GetAbsOrigin().DistTo(pExit->GetAbsOrigin());
				if (flDistToExit > 180.f)
				{
					m_eState = EState::CampExit;
					m_iCampIdx = pExit->entindex();
					m_flScanClock = 0.f;
					return CampAt(pCmd, pLocal, pExit);
				}
				m_flOnEntranceSince = flCurTime;
			}
			else
			{
				m_eState = EState::CampDispenser;
				m_iCampIdx = -1;
				m_flScanClock = 0.f;
				return true;
			}
		}

		if (flDist > 55.f)
		{
			Vector vTarget = pEntrance->GetAbsOrigin();
			bool bExpandedClose = pEntrance->IsDormant() || F::NavEngine.FindClosestNavArea(vTarget, true) != nullptr;
			if (flDist <= 120.f && bExpandedClose)
			{
				SDK::WalkTo(pCmd, pLocal, vTarget);
				return true;
			}
			SDK::WalkTo(pCmd, pLocal, vTarget);
		}
		return true;
	}
	case EState::CampExit:
	{
		auto pExit = GetTeleporter(m_iCampIdx, 1);
		if (!pExit)
		{
			if (flCurTime >= m_flScanClock)
			{
				pExit = FindClosestTeleporter(pLocal, 1);
				m_iCampIdx = pExit ? pExit->entindex() : -1;
				m_flScanClock = flCurTime + 2.f;
			}

			if (!pExit)
			{
				m_eState = EState::CampDispenser;
				m_iCampIdx = -1;
				m_flScanClock = 0.f;
				return true;
			}
		}

		return CampAt(pCmd, pLocal, pExit);
	}
	case EState::CampDispenser:
	{
		auto pDispenser = GetDispenser(m_iCampIdx);
		if (!pDispenser)
		{
			if (flCurTime >= m_flScanClock)
			{
				pDispenser = FindClosestDispenser(pLocal);
				m_iCampIdx = pDispenser ? pDispenser->entindex() : -1;
				m_flScanClock = flCurTime + 2.f;
			}

			if (!pDispenser)
			{
				m_eState = EState::ToEntrance;
				m_iEntranceIdx = -1;
				m_flScanClock = 0.f;
				return true;
			}
		}

		if (flCurTime >= m_flPairClock)
		{
			m_flPairClock = flCurTime + 5.f;
			if (auto pEntrance = FindClosestTeleporter(pLocal, 0))
			{
				if (auto pExit = FindClosestTeleporter(pLocal, 1))
				{
					float flDistExit = pLocal->GetAbsOrigin().DistTo(pExit->GetAbsOrigin());
					if (flDistExit < 220.f)
					{
						return CampAt(pCmd, pLocal, pExit);
					}
					m_eState = EState::CampExit;
					m_iCampIdx = pExit->entindex();
					m_flScanClock = 0.f;
					return CampAt(pCmd, pLocal, pExit);
				}
			}
		}

		return CampAt(pCmd, pLocal, pDispenser);
	}
	}

	Reset();
	return false;
}

void CNavBotMVMSniper::Reset()
{
	m_eState = EState::ToEntrance;
	m_iEntranceIdx = -1;
	m_iCampIdx = -1;
	m_flLastEntranceDist = FLT_MAX;
	m_flOnEntranceSince = 0.f;
	m_flScanClock = 0.f;
	m_flPairClock = 0.f;
}
