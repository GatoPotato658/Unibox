#include "PLController.h"

inline int GetPayloadTeamIndex(int iTeam)
{
	return iTeam - TF_TEAM_RED;
}

void CPLController::Init()
{
	m_aPayloadCounts = {};
}

void CPLController::Update()
{
	m_aPayloadCounts = {};

	{
		for (auto pPayload : H::Entities.GetGroup(EntityEnum::WorldObjective))
		{
			if (!pPayload || pPayload->GetClassID() != ETFClassID::CObjectCartDispenser)
				continue;

			int iTeam = pPayload->m_iTeamNum();

			if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
				continue;

		const auto iTeamIndex = GetPayloadTeamIndex(iTeam);
		auto& aPayloads = m_aPayloads[iTeamIndex];
		auto& nPayloadCount = m_aPayloadCounts[iTeamIndex];
		if (nPayloadCount < aPayloads.size())
			aPayloads[nPayloadCount++] = pPayload->As<CObjectCartDispenser>();
		}
	}
}

CObjectCartDispenser* CPLController::GetClosestPayload(Vector vPos, int iTeam)
{
	if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
		return nullptr;

	float flMinDist = FLT_MAX;
	CObjectCartDispenser* pBestEnt = nullptr;

	const auto iTeamIndex = GetPayloadTeamIndex(iTeam);
	for (size_t nPayloadIndex = 0; nPayloadIndex < m_aPayloadCounts[iTeamIndex]; nPayloadIndex++)
	{
		auto pEntity = m_aPayloads[iTeamIndex][nPayloadIndex];
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CObjectCartDispenser || pEntity->IsDormant())
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
