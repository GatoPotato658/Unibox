#include "NavBotJobs.h"

constexpr int kAnyObjectMode = -1;

static bool IsUsableObject(CBaseEntity* pEntity, ETFClassID eClassId, int iObjectMode)
{
	if (!pEntity || pEntity->GetClassID() != eClassId)
		return false;

	auto pObject = pEntity->As<CBaseObject>();
	return (iObjectMode == kAnyObjectMode || pObject->m_iObjectMode() == iObjectMode)
		&& !pObject->m_bPlacing() && !pObject->m_bCarried() && !pObject->m_bDisabled();
}

static CBaseObject* GetObject(int iIdx, ETFClassID eClassId, int iObjectMode)
{
	if (iIdx <= 0)
		return nullptr;

	auto pClientEntity = I::ClientEntityList->GetClientEntity(iIdx);
	auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
	return IsUsableObject(pEntity, eClassId, iObjectMode) ? pEntity->As<CBaseObject>() : nullptr;
}

static CBaseObject* FindClosestObject(CTFPlayer* pLocal, ETFClassID eClassId, int iObjectMode)
{
	CBaseObject* pBest = nullptr;
	float flBestDist = FLT_MAX;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	const auto Consider = [&](CBaseEntity* pEntity)
		{
			if (!IsUsableObject(pEntity, eClassId, iObjectMode))
				return;

			Vector vOrigin;
			if (pEntity->IsDormant())
			{
				if (!F::BotUtils.GetDormantOrigin(pEntity->entindex(), &vOrigin))
					return;
			}
			else
				vOrigin = pEntity->GetAbsOrigin();

			const float flDist = vLocalOrigin.DistToSqr(vOrigin);
			if (flDist >= flBestDist)
				return;

			flBestDist = flDist;
			pBest = pEntity->As<CBaseObject>();
		};

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
		Consider(pEntity);

	if (!pBest)
	{
		for (int i = 1; i <= I::ClientEntityList->GetHighestEntityIndex(); ++i)
		{
			auto pClientEntity = I::ClientEntityList->GetClientEntity(i);
			auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
			if (pEntity && pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
				Consider(pEntity);
		}
	}

	return pBest;
}

CBaseObject* CNavBotMVMSniper::FindClosestTeleporter(CTFPlayer* pLocal, int iObjectMode)
{
	return FindClosestObject(pLocal, ETFClassID::CObjectTeleporter, iObjectMode);
}

CBaseObject* CNavBotMVMSniper::FindClosestDispenser(CTFPlayer* pLocal)
{
	return FindClosestObject(pLocal, ETFClassID::CObjectDispenser, kAnyObjectMode);
}

bool CNavBotMVMSniper::CampAt(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseEntity* pAnchor)
{
	if (!pAnchor)
		return false;

	const Vector vAnchorOrigin = pAnchor->GetAbsOrigin();
	const float flDist = pLocal->GetAbsOrigin().DistTo(vAnchorOrigin);
	if (flDist > 150.f)
	{
		F::NavEngine.NavTo(vAnchorOrigin, PriorityListEnum::MVMSniper);
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
		auto pEntrance = GetObject(m_iEntranceIdx, ETFClassID::CObjectTeleporter, 0);
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
			F::NavEngine.NavTo(pEntrance->GetAbsOrigin(), PriorityListEnum::MVMSniper);
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
				if (pLocal->GetAbsOrigin().DistTo(pExit->GetAbsOrigin()) > 180.f)
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
			SDK::WalkTo(pCmd, pLocal, pEntrance->GetAbsOrigin());
		return true;
	}
	case EState::CampExit:
	{
		auto pExit = GetObject(m_iCampIdx, ETFClassID::CObjectTeleporter, 1);
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
		auto pDispenser = GetObject(m_iCampIdx, ETFClassID::CObjectDispenser, kAnyObjectMode);
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
			if (FindClosestTeleporter(pLocal, 0))
			{
				if (auto pExit = FindClosestTeleporter(pLocal, 1))
				{
					if (pLocal->GetAbsOrigin().DistTo(pExit->GetAbsOrigin()) < 220.f)
						return CampAt(pCmd, pLocal, pExit);

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
