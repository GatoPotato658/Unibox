#include "Objectives.h"
#include "NavEngine.h"

using namespace ObjectiveUtils;

CCaptureFlag* CDoomsdayController::GetFlag()
{
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		return pEntity->As<CCaptureFlag>();
	}

	return nullptr;
}

static bool GetDoomsdayCapturePos(Vector& vOut)
{
	for (int n = I::EngineClient->GetMaxClients() + 1; n <= I::ClientEntityList->GetHighestEntityIndex(); n++)
	{
		auto pClientEntity = I::ClientEntityList->GetClientEntity(n);
		auto pEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
		if (!pEntity || pEntity->IsDormant() || pEntity->GetClassID() != ETFClassID::CDynamicProp)
			continue;

		auto pModel = pEntity->GetModel();
		if (!pModel)
			continue;

		const char* pszModelName = I::ModelInfoClient->GetModelName(pModel);
		if (pszModelName && std::string_view(pszModelName).find("rocket_lid") != std::string_view::npos)
		{
			Vector vPos = pEntity->GetAbsOrigin();
			if (vPos.IsZero())
				vPos = pEntity->GetCenter();
			if (vPos.IsZero())
				continue;

			vOut = AdjustPosToNav(vPos);
			if (Vars::Debug::Logging.Value)
				SDK::Output("DoomsdayController", std::format("GetDoomsdayCapturePos: found rocket via rocket_lid_model ({})", pszModelName).c_str(), { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return true;
		}
	}

	return false;
}

bool CDoomsdayController::GetCapturePos(Vector& vOut)
{
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	Vector vCapturePos = {};
	if (GetDoomsdayCapturePos(vCapturePos))
	{
		m_vCachedCapturePos = vCapturePos;
		m_bHasCachedCapturePos = true;
	}
	else if (m_bHasCachedCapturePos)
	{
		vCapturePos = m_vCachedCapturePos;
	}

	if (vCapturePos.IsZero())
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("DoomsdayController", "GetCapturePos: failed to find rocket position", { 255, 100, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return false;
	}

	vOut = vCapturePos;
	return true;
}

bool CDoomsdayController::GetGoal(Vector& vOut)
{
	m_sDoomsdayStatus = L"";
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	auto pFlag = GetFlag();
	if (!pFlag)
		return false;

	int iLocalTeam = pLocal->m_iTeamNum();
	int iFlagTeam = pFlag->m_iTeamNum();

	bool bIsCarrier = F::FlagController.GetCarrier(pFlag) == pLocal->entindex();
	if (!bIsCarrier)
	{
		auto pCarried = pLocal->m_hCarriedObject().Get();
		if (pCarried == pFlag)
			bIsCarrier = true;
	}

	if (bIsCarrier)
	{
		if (GetCapturePos(vOut))
		{
			m_sDoomsdayStatus = L"Rocket";

			float flClosestDist = 1000.0f;
			CBaseEntity* pClosestTrain = nullptr;
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
			{
				if (!pEntity || pEntity->GetClassID() != ETFClassID::CFuncTrackTrain)
					continue;

				float flDist = pLocal->GetAbsOrigin().DistTo(pEntity->GetCenter());
				if (flDist < flClosestDist)
				{
					flClosestDist = flDist;
					pClosestTrain = pEntity;
				}
			}

			if (pClosestTrain)
			{
				vOut = pClosestTrain->GetCenter();
			}
			else
			{
				Vector vDir = vOut - pLocal->GetAbsOrigin();
				float len = vDir.Length2D();
				if (len > 0.001f)
				{
					vDir /= len;
					vOut -= (vDir * 40.0f);
				}
			}

			return true;
		}

		return false;
	}

	if (iFlagTeam != 0 && iFlagTeam != iLocalTeam)
		return false;

	int iCarrierIdx = F::FlagController.GetCarrier(pFlag);
	if (iCarrierIdx != -1)
	{
		m_sDoomsdayStatus = L"Assist";
		vOut = pFlag->GetAbsOrigin();
		return true;
	}

	m_sDoomsdayStatus = L"Australium";
	vOut = pFlag->GetAbsOrigin();
	return true;
}

void CDoomsdayController::Init()
{
	m_vCachedCapturePos = {};
	m_bHasCachedCapturePos = false;
	m_sDoomsdayStatus = L"";
}

static CCaptureFlag* GetHaarpFlag(const Vector& vRelativePos = Vector())
{
	CCaptureFlag* pBestFlag = nullptr;
	float flBestDist = FLT_MAX;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureFlag)
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

static bool GetHaarpCapturePos(int iLocalTeam, Vector& vOut)
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

		vOut = AdjustPosToNav(tTrigger.m_vCenter);

		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(255, 0, 255, 10), Color_t(255, 0, 255, 100));

		return true;
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CCaptureZone)
			continue;

		if (pEntity->As<CCaptureZone>()->m_bDisabled())
			continue;

		int iTeam = pEntity->m_iTeamNum();
		if (iTeam != 0 && iTeam != iLocalTeam)
			continue;

		vOut = AdjustPosToNav(GetObjectiveOrigin(pEntity));

		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(255, 128, 0, 10), Color_t(255, 128, 0, 100));

		return true;
	}

	int iFallbackIdx = -1;
	const int iControlPointCount = std::clamp(pResource->m_iNumControlPoints(), 0, MAX_CONTROL_POINTS);
	for (int i = 0; i < iControlPointCount; i++)
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

		vOut = AdjustPosToNav(vCPPos);
		if (Vars::Debug::Info.Value)
			G::SphereStorage.emplace_back(vOut, 40.f, 10, 10, I::GlobalVars->curtime + 2.2f, Color_t(0, 255, 0, 10), Color_t(0, 255, 0, 100));

		return true;
	}

	if (iFallbackIdx != -1)
	{
		vOut = AdjustPosToNav(pResource->m_vCPPositions(iFallbackIdx));
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

	auto pFlag = GetHaarpFlag(vCapturePos);

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

void CHaarpController::Init()
{
	m_vCachedCapturePos = {};
	m_bHasCachedCapturePos = false;
	m_vCachedBluCapturePos = {};
	m_bHasCachedBluCapturePos = false;
	m_sHaarpStatus = L"";
}
