#pragma once
#include "../../../SDK/SDK.h"

struct MvMDropdown_t
{
	std::vector<std::string> m_vNames;
	std::vector<const char*> m_vEntries;
};

class CMvmQueue
{
private:
	struct Mission_t
	{
		std::string m_sPopfile;
		int m_iBadgeSlot = -1;
	};

	struct Tour_t
	{
		std::string m_sInternalName;
		std::vector<Mission_t> m_vMissions;
	};

	struct TourPair_t
	{
		int m_iMissionIndex;
		int m_iBadgeSlot;
	};

	bool EnsureTours();
	bool SetCriteria(int eMatchGroup, const char* sTourInternalName, const std::vector<std::string>& vMissions);
	void RebuildDropdowns();

	std::vector<Tour_t> m_vTours;
	std::vector<Mission_t> m_vBootcampMissions;
	const void* m_pLastSchema = nullptr;
	int m_iLastTourCount = -1;

public:
	MvMDropdown_t m_tTourDropdown = { { "Any" }, {} };
	MvMDropdown_t m_tBootcampDropdown;

	void Refresh();
	bool ApplyMannUpCriteria();
	bool ApplyBootcampCriteria();
	void ClearMannUpCriteria();
	void ClearBootcampCriteria();
};

ADD_FEATURE(CMvmQueue, MvmQueue);
