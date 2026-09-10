#pragma once
#include "../../../../../SDK/SDK.h"

#define MAX_CONTROL_POINTS 8
#define MAX_PREVIOUS_POINTS 3

struct CPInfo
{
	// Index in the ObjectiveResource
	int m_iIdx = -1;
	Vector m_vPos = {};
	bool m_bGotPos = false;
	std::array<bool, 2> m_bCanCap = { false, false };	// For BLU and RED to show who can and cannot cap
	CPInfo() = default;
};

class CCPController
{
private:
	std::array<CPInfo, MAX_CONTROL_POINTS> m_aControlPointData;
	CBaseTeamObjectiveResource* m_pObjectiveResource = nullptr;

	//Update
	void UpdateObjectiveResource();
	void UpdateControlPoints();

	struct PointIgnore
	{
		std::string m_sMapName;
		int m_iPointIdx;
		PointIgnore(std::string sName, int iIndex) : m_sMapName{ std::move(sName) }, m_iPointIdx{ iIndex } {};
	};

	// TODO: Find a way to fix these edge-cases.
	// clang-format off
	std::array<PointIgnore, 1> m_aIgnorePoints{ PointIgnore("cp_steel", 4) };
	// clang-format on

	bool TeamCanCapPoint(int iIndex, int iTeam);
	int GetPreviousPointForPoint(int iIndex, int iTeam, int iPrevIdx);
	int GetFarthestOwnedControlPoint(int iTeam);

public:
	// Can we cap this point?
	bool IsPointUseable(int iIndex, int iTeam);

	// Get the closest Control Point we can cap
	bool GetClosestControlPoint(Vector vPos, int iTeam, Vector& vOut);
	bool GetClosestControlPointInfo(Vector vPos, int iTeam, std::pair<int, Vector>& tOut);

	void Init();
	void Update();
};

ADD_FEATURE(CCPController, CPController);
