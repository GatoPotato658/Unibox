#pragma once
#include "../../../../../SDK/SDK.h"

struct FlagInfo
{
	CCaptureFlag* m_pFlag = nullptr;
	int m_iTeam = TEAM_UNASSIGNED;
	FlagInfo() = default;
	FlagInfo(CCaptureFlag* pFlag, int iTeam)
	{
		m_pFlag = pFlag;
		m_iTeam = iTeam;
	}
};

class CFlagController
{
private:
	std::vector<FlagInfo> m_vFlags;
	std::unordered_map<int, Vector> m_mSpawnPositions;
public:
	// Use incase you don't get the needed information from the functions below
	FlagInfo GetFlag(int team);

	Vector GetPosition(CCaptureFlag* pFlag);
	bool GetPosition(int iTeam, Vector& vOut);
	bool GetSpawnPosition(int iTeam, Vector& vOut);
	int GetCarrier(CCaptureFlag* pFlag);
	int GetCarrier(int iTeam);
	int GetStatus(CCaptureFlag* pFlag);
	int GetStatus(int iTeam);

	void Init();
	void Update();
};

ADD_FEATURE(CFlagController, FlagController);