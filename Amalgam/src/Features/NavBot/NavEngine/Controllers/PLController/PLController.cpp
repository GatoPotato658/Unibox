#include "PLController.h"

inline int GetPayloadTeamIndex(int iTeam)
{
	return iTeam - TF_TEAM_RED;
}

void CPLController::Init()
{
	// Reset entries
	for (auto& vEntities : m_aPayloads)
		vEntities.clear();
}

void CPLController::Update()
{
	// We should update the payload list
	{
		// Reset entries
		for (auto& vEntities : m_aPayloads)
			vEntities.clear();

		for (auto pPayload : H::Entities.GetGroup(EntityEnum::WorldObjective))
		{
			if (pPayload->GetClassID() != ETFClassID::CObjectCartDispenser)
				continue;

			int iTeam = pPayload->m_iTeamNum();

			// Not the object we need
			if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
				continue;

			// Add new entry for the team
			m_aPayloads.at(GetPayloadTeamIndex(iTeam)).push_back(pPayload->As<CObjectCartDispenser>());
		}
	}
}

CObjectCartDispenser* CPLController::GetClosestPayload(Vector vPos, int iTeam)
{
	// Invalid team
	if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
		return nullptr;

	float flMinDist = FLT_MAX;
	CObjectCartDispenser* pBestEnt = nullptr;

	// Find best payload
	for (auto pEntity : m_aPayloads[GetPayloadTeamIndex(iTeam)])
	{
		if (pEntity->GetClassID() != ETFClassID::CObjectCartDispenser || pEntity->IsDormant())
			continue;

		const auto vOrigin = pEntity->GetAbsOrigin();
		const auto flDist = vOrigin.DistToSqr(vPos);
		if (flDist < flMinDist)
		{
			pBestEnt = pEntity;
			flMinDist = flDist;
		}
	}

	return pBestEnt;
}
