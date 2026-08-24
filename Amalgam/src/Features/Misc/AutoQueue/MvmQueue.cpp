#include "MvmQueue.h"

bool CMvmQueue::EnsureTours()
{
	const auto pSchema = CEconItemSchema::GetInstance();
	if (!pSchema || pSchema->m_iMvMTourCount() <= 0 || pSchema->m_iMvMMissionCount() <= 0)
	{
		if (m_pLastSchema)
		{
			m_pLastSchema = nullptr;
			m_iLastTourCount = -1;
			m_vTours.clear();
			m_vBootcampMissions.clear();
			RebuildDropdowns();
		}
		return false;
	}

	const int iTourCount = pSchema->m_iMvMTourCount();
	if (m_pLastSchema == pSchema && m_iLastTourCount == iTourCount && !m_vTours.empty())
		return true;

	m_pLastSchema = pSchema;
	m_iLastTourCount = iTourCount;
	m_vTours.clear();
	m_vBootcampMissions.clear();

	const auto uToursBase = reinterpret_cast<uintptr_t>(pSchema->m_pMvMTours());
	const auto uMissionsBase = reinterpret_cast<uintptr_t>(pSchema->m_pMvMMissions());
	const int iMissionCount = pSchema->m_iMvMMissionCount();

	for (int i = 0; i < iTourCount; i++)
	{
		const uintptr_t uTour = uToursBase + 88 * i;
		Tour_t tTour;
		if (const auto szInternalName = *reinterpret_cast<const char**>(uTour))
			tTour.m_sInternalName = szInternalName;
		if (tTour.m_sInternalName.empty())
			continue;

		const auto pPairs = *reinterpret_cast<TourPair_t**>(uTour + 40);
		const int iPairCount = *reinterpret_cast<int*>(uTour + 56);
		for (int j = 0; pPairs && j < iPairCount; j++)
		{
			const int iMissionIndex = pPairs[j].m_iMissionIndex;
			if (iMissionIndex < 0 || iMissionIndex >= iMissionCount)
				continue;

			const auto szPopfile = *reinterpret_cast<const char**>(uMissionsBase + 48 * iMissionIndex + 8);
			if (!szPopfile || !*szPopfile)
				continue;

			tTour.m_vMissions.push_back({ szPopfile, pPairs[j].m_iBadgeSlot });

			if (pPairs[j].m_iBadgeSlot < 0 && m_vBootcampMissions.size() < 31
				&& std::find_if(m_vBootcampMissions.begin(), m_vBootcampMissions.end(), [&](const Mission_t& tMission) { return tMission.m_sPopfile == szPopfile; }) == m_vBootcampMissions.end())
				m_vBootcampMissions.push_back({ szPopfile, -1 });
		}

		m_vTours.push_back(std::move(tTour));
	}

	RebuildDropdowns();
	return true;
}

void CMvmQueue::Refresh()
{
	EnsureTours();
}

void CMvmQueue::RebuildDropdowns()
{
	auto fnRebuild = [](MvMDropdown_t& tDropdown)
		{
			tDropdown.m_vEntries.clear();
			tDropdown.m_vEntries.reserve(tDropdown.m_vNames.size());
			for (const auto& sName : tDropdown.m_vNames)
				tDropdown.m_vEntries.push_back(sName.c_str());
		};

	m_tTourDropdown.m_vNames.clear();
	m_tTourDropdown.m_vNames.push_back("Any");
	for (auto& tTour : m_vTours)
		m_tTourDropdown.m_vNames.push_back(tTour.m_sInternalName);

	m_tBootcampDropdown.m_vNames.clear();
	for (auto& tMission : m_vBootcampMissions)
		m_tBootcampDropdown.m_vNames.push_back(tMission.m_sPopfile);

	fnRebuild(m_tTourDropdown);
	fnRebuild(m_tBootcampDropdown);
}

bool CMvmQueue::SetCriteria(const int eMatchGroup, const char* sTourInternalName, const std::vector<std::string>& vMissions)
{
	if (!S::Protobuf_RepeatedStrings_Reserve() || !S::Protobuf_RepeatedStrings_NewElement() || !S::Protobuf_String_Assign())
		return false;

	auto pCriteria = I::TFPartyClient->m_localGroupCriteria();
	if (!pCriteria)
		return false;

	auto pProto = pCriteria->Proto();
	if (!pProto)
		return false;

	auto pField = eMatchGroup == k_eTFMatchGroup_MvM_Practice ? pProto->GetBootcampMissions() : pProto->GetMannUpMissions();
	if (!pField)
		return false;

	if (eMatchGroup == k_eTFMatchGroup_MvM_MannUp)
		pProto->SetMannUpTour(sTourInternalName);

	if (pField->Matches(vMissions))
		return true;

	pField->Clear();
	for (auto& sMission : vMissions)
	{
		if (!pField->Add(sMission.c_str()))
			return false;
	}
	return true;
}

bool CMvmQueue::ApplyMannUpCriteria()
{
	if (!S::Protobuf_String_Assign())
		return false;

	if (!EnsureTours())
		return SetCriteria(k_eTFMatchGroup_MvM_MannUp, "", {});

	const int iSelected = Vars::Misc::Queueing::MannUpTourIndex.Value;
	if (iSelected <= 0 || iSelected > static_cast<int>(m_vTours.size()))
		return SetCriteria(k_eTFMatchGroup_MvM_MannUp, "", {});

	auto& tTour = m_vTours[iSelected - 1];
	std::vector<std::string> vMissions;
	for (auto& tMission : tTour.m_vMissions)
	{
		if (tMission.m_iBadgeSlot >= 0)
			vMissions.push_back(tMission.m_sPopfile);
	}

	if (!vMissions.empty() && Vars::Misc::Queueing::MannUpUncompleted.Value)
	{
		std::vector<std::string> vUncompleted;
		const uint32_t uMask = CEconItemSchema::GetTourCompletedMask(iSelected - 1);
		for (auto& tMission : tTour.m_vMissions)
		{
			if (tMission.m_iBadgeSlot < 0 || !(uMask & (1u << tMission.m_iBadgeSlot)))
				vUncompleted.push_back(tMission.m_sPopfile);
		}
		if (!vUncompleted.empty())
			vMissions = std::move(vUncompleted);
	}

	if (vMissions.empty())
		return SetCriteria(k_eTFMatchGroup_MvM_MannUp, "", {});

	return SetCriteria(k_eTFMatchGroup_MvM_MannUp, tTour.m_sInternalName.c_str(), vMissions);
}

bool CMvmQueue::ApplyBootcampCriteria()
{
	if (!S::Protobuf_String_Assign())
		return false;

	EnsureTours();

	std::vector<std::string> vMissions;
	const uint32_t uBits = Vars::Misc::Queueing::BootcampMissionBits.Value;
	for (int i = 0; i < static_cast<int>(m_vBootcampMissions.size()); i++)
	{
		if (uBits & (1u << i))
			vMissions.push_back(m_vBootcampMissions[i].m_sPopfile);
	}

	return SetCriteria(k_eTFMatchGroup_MvM_Practice, nullptr, vMissions);
}

void CMvmQueue::ClearMannUpCriteria()
{
	SetCriteria(k_eTFMatchGroup_MvM_MannUp, "", {});
}

void CMvmQueue::ClearBootcampCriteria()
{
	SetCriteria(k_eTFMatchGroup_MvM_Practice, nullptr, {});
}
