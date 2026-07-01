#include "DoomsdayController.h"
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

CCaptureFlag* CDoomsdayController::GetFlag()
{
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		return pEntity->As<CCaptureFlag>();
	}

	return nullptr;
}

bool GetDoomsdayCapturePos(int iLocalTeam, Vector& vOut)
{
	/*
	// capture area
	for (auto& tTrigger : G::TriggerStorage)
	{
		if (tTrigger.m_eType != TriggerTypeEnum::CaptureArea)
			continue;

		Vector vPos = AdjustToNav(tTrigger.m_vCenter);
		if (!vPos.IsZero())
		{
			if (Vars::Debug::Logging.Value)
				SDK::Output("DoomsdayController", "GetDoomsdayCapturePos: found rocket via trigger", { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			vOut = vPos;
			return true;
		}
	}

	// sd_ doesnt really use control points actually
	auto pResource = H::Entities.GetObjectiveResource();
	if (pResource)
	{
		int iNumCPs = pResource->m_iNumControlPoints();
		for (int i = 0; i < iNumCPs; i++)
		{
			Vector vCPPos = pResource->m_vCPPositions(i);
			if (vCPPos.IsZero())
				continue;

			// The rocket is usually the only control point in Doomsday
			vOut = AdjustToNav(vCPPos);
			if (Vars::Debug::Logging.Value)
				SDK::Output("DoomsdayController", "GetDoomsdayCapturePos: found rocket via objective resource", { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return true;
		}
	}
	*/

	// Try to find the rocket lid prop specifically (prop_dynamic)
	for (int n = I::EngineClient->GetMaxClients() + 1; n <= I::ClientEntityList->GetHighestEntityIndex(); n++)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(n)->As<CBaseEntity>();
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

			vOut = AdjustObjectivePosToNav(vPos);
			if (Vars::Debug::Logging.Value)
				SDK::Output("DoomsdayController", std::format("GetDoomsdayCapturePos: found rocket via rocket_lid_model ({})", pszModelName).c_str(), { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return true;
		}
	}

	/*
	// rocket lid already works but if by some reason we would not be able to find it, then this works too. just we'd have to slighly move its pos
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (!pEntity || pEntity->IsDormant())
			continue;

		bool bIsRocket = pEntity->GetClassID() == ETFClassID::CTeamControlPoint || pEntity->GetClassID() == ETFClassID::CFuncTrackTrain;
		if (!bIsRocket)
		{
			if (auto pClientClass = pEntity->GetClientClass())
			{
				uint32_t uHash = FNV1A::Hash32(pClientClass->m_pNetworkName);
				bIsRocket = uHash == FNV1A::Hash32Const("CTeamControlPoint") || uHash == FNV1A::Hash32Const("CFuncTrackTrain");
			}
		}

		if (!bIsRocket)
			continue;

		Vector vCPPos = pEntity->GetAbsOrigin();
		if (vCPPos.IsZero())
			vCPPos = pEntity->GetCenter();
		if (vCPPos.IsZero())
			continue;

		vOut = AdjustToNav(vCPPos);
		if (Vars::Debug::Logging.Value)
			SDK::Output("DoomsdayController", std::format("GetDoomsdayCapturePos: found rocket via WorldObjective entity ({})", pEntity->GetClientClass()->m_pNetworkName).c_str(), { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return true;
	}
	*/

	return false;
}

bool CDoomsdayController::GetCapturePos(Vector& vOut)
{
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	int iLocalTeam = pLocal->m_iTeamNum();
	Vector vCapturePos = {};
	if (GetDoomsdayCapturePos(iLocalTeam, vCapturePos))
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

	// Check if we are carrying the flag
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
				if (pEntity->GetClassID() != ETFClassID::CFuncTrackTrain)
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

void CDoomsdayController::Update()
{
	static std::string sLastMap = "";
	const char* szLevelName = I::EngineClient->GetLevelName();
	std::string sCurrentMap = szLevelName ? szLevelName : "";
	if (sCurrentMap != sLastMap || sCurrentMap.empty())
	{
		m_vCachedCapturePos = {};
		m_bHasCachedCapturePos = false;
		sLastMap = sCurrentMap;
	}
}
