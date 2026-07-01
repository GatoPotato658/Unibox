#include "HaarpController.h"
#include "../FlagController/FlagController.h"
#include "../CPController/CPController.h"
#include "../../NavEngine.h"

static Vector AdjustObjectivePosToNav(Vector vPos)
{
	if (!F::NavEngine.IsNavMeshLoaded())
		return vPos;

	CNavArea* pArea = F::NavEngine.FindClosestNavArea(vPos, false);
	if (!pArea)
		return vPos;

	Vector vCorrected = pArea->GetNearestPoint(vPos.Get2D());
	vCorrected.z = pArea->GetZ(vCorrected.x, vCorrected.y);
	return vCorrected;
}

CCaptureFlag* GetHaarpFlag(int iTeam, const Vector& vRelativePos = Vector())
{
	CCaptureFlag* pBestFlag = nullptr;
	float flBestDist = FLT_MAX;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		if (vRelativePos.IsZero())
			return pFlag;

		float flDist = vRelativePos.DistTo(pFlag->GetAbsOrigin());
		if (flDist < flBestDist)
		{
			flBestDist = flDist;
			pBestFlag = pFlag;
		}
	}

	return pBestFlag;
}

bool GetHaarpCapturePos(int iLocalTeam, Vector& vOut)
{
	auto pResource = H::Entities.GetObjectiveResource();
	if (!pResource)
		return false;

	for (auto& tTrigger : G::TriggerStorage)
	{
		if (tTrigger.m_eType != TriggerTypeEnum::CaptureArea)
			continue;

		if (tTrigger.m_iTeam != 0 && tTrigger.m_iTeam != TF_TEAM_RED)
			continue;

		vOut = AdjustObjectivePosToNav(tTrigger.m_vCenter);

		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(255, 0, 255, 10), Color_t(255, 0, 255, 100));

		return true;
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (pEntity->GetClassID() != ETFClassID::CCaptureZone)
			continue;

		static int nDisabledOffset = U::NetVars.GetNetVar("CBaseEntity", "m_bDisabled");
		if (*reinterpret_cast<bool*>(uintptr_t(pEntity) + nDisabledOffset))
			continue;

		int iTeam = pEntity->m_iTeamNum();
		if (iTeam != 0 && iTeam != iLocalTeam)
			continue;

		vOut = AdjustObjectivePosToNav(pEntity->GetAbsOrigin());

		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(255, 128, 0, 10), Color_t(255, 128, 0, 100));

		return true;
	}

	int iFallbackIdx = -1;
	for (int i = 0; i < pResource->m_iNumControlPoints(); i++)
	{
		if (!F::CPController.IsPointUseable(i, iLocalTeam))
			continue;

		Vector vCPPos = pResource->m_vCPPositions(i);

		bool bIsFlagSpot = false;
		for (int iTeam = 0; iTeam < 4; iTeam++)
		{
			Vector vSpawnPos;
			if (F::FlagController.GetSpawnPosition(iTeam, vSpawnPos))
			{
				if (vCPPos.DistTo(vSpawnPos) < 100.f)
				{
					bIsFlagSpot = true;
					break;
				}
			}
		}

		if (bIsFlagSpot)
		{
			if (iFallbackIdx == -1) iFallbackIdx = i;
			continue;
		}

		vOut = AdjustObjectivePosToNav(vCPPos);
		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(0, 255, 0, 10), Color_t(0, 255, 0, 100));

		return true;
	}

	if (iFallbackIdx != -1)
	{
		vOut = AdjustObjectivePosToNav(pResource->m_vCPPositions(iFallbackIdx));
		return true;
	}

	return false;
}

bool CHaarpController::GetCapturePos(Vector& vOut)
{
	m_sHaarpStatus = L"";
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	int iLocalTeam = pLocal->m_iTeamNum();
	if (iLocalTeam != TF_TEAM_BLUE)
		return false;

	Vector vCapturePos = {};
	if (GetHaarpCapturePos(iLocalTeam, vCapturePos))
	{
		m_vCachedCapturePos = vCapturePos;
		m_bHasCachedCapturePos = true;
	}
	else if (m_bHasCachedCapturePos)
		vCapturePos = m_vCachedCapturePos;

	if (vCapturePos.IsZero())
		return false;

	int iLocalIndex = pLocal->entindex();
	bool bIsCarryingFlag = false;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		int iCarrierIdx = F::FlagController.GetCarrier(pFlag);
		if (iCarrierIdx == iLocalIndex)
		{
			bIsCarryingFlag = true;
			break;
		}
	}

	if (!bIsCarryingFlag)
		return false;

	Vector vGoalPos = vCapturePos;
	if (F::NavEngine.IsNavMeshLoaded())
	{
		CNavArea* pArea = F::NavEngine.FindClosestNavArea(vCapturePos, false);
		if (pArea)
		{
			Vector vCenter = pArea->m_vCenter;
			vCenter.z = pArea->GetZ(vCenter.x, vCenter.y);
			vGoalPos = vCenter;
		}
	}

	m_sHaarpStatus = L"CP";
	vOut = vGoalPos;
	if (Vars::Debug::Info.Value)
		G::SphereStorage.emplace_back(vGoalPos, 30.f, 20, 20, I::GlobalVars->curtime + 2.2f, Color_t(0, 255, 0, 10), Color_t(0, 255, 0, 100));

	return true;
}

bool CHaarpController::GetDefensePos(Vector& vOut)
{
	m_sHaarpStatus = L"";
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	int iLocalTeam = pLocal->m_iTeamNum();
	if (iLocalTeam != TF_TEAM_RED)
		return false;

	Vector vCapturePos = {};
	if (GetHaarpCapturePos(TF_TEAM_BLUE, vCapturePos))
	{
		m_vCachedBluCapturePos = vCapturePos;
		m_bHasCachedBluCapturePos = true;
	}
	else if (m_bHasCachedBluCapturePos)
		vCapturePos = m_vCachedBluCapturePos;

	if (vCapturePos.IsZero())
		return false;

	auto pFlag = GetHaarpFlag(-1, vCapturePos);

	if (!pFlag)
		return false;

	if (Vars::Debug::Info.Value)
		G::SphereStorage.emplace_back(vCapturePos, 30.f, 20, 20, I::GlobalVars->curtime + 2.2f, Color_t(0, 255, 255, 10), Color_t(0, 255, 255, 100));

	int iStatus = F::FlagController.GetStatus(pFlag);
	if (iStatus == TF_FLAGINFO_STOLEN)
	{
		m_sHaarpStatus = L"CP";
		vOut = vCapturePos;
		return true;
	}

	m_sHaarpStatus = L"Flag";
	vOut = pFlag->GetAbsOrigin();
	return true;
}

void CHaarpController::Update()
{
	static std::string sLastMap = "";
	const char* szLevelName = I::EngineClient->GetLevelName();
	std::string sCurrentMap = szLevelName ? szLevelName : "";
	if (sCurrentMap != sLastMap || sCurrentMap.empty())
	{
		m_vCachedCapturePos = {};
		m_bHasCachedCapturePos = false;
		m_vCachedBluCapturePos = {};
		m_bHasCachedBluCapturePos = false;
		sLastMap = sCurrentMap;
	}
}
