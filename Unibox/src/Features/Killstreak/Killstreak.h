#pragma once
#include "../../SDK/SDK.h"

class CKillstreak
{
private:
	std::unordered_map<int, int> m_mKillstreakMap;
	int m_iCurrentKillstreak;

private:
	int	 GetCurrentStreak();
	void ApplyKillstreak(int iLocalIdx);

public:
	void PlayerDeath(IGameEvent* pEvent);
	void PlayerSpawn(IGameEvent* pEvent);
	void Reset();
};

ADD_FEATURE(CKillstreak, Killstreak);