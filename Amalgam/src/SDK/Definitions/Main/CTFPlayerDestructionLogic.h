#pragma once
#include "CBaseEntity.h"

class CTFPlayer;

class CTFPlayerDestructionLogic : public CBaseEntity
{
public:
	NETVAR(m_hRedTeamLeader, EHANDLE, "CTFPlayerDestructionLogic", "m_hRedTeamLeader");
	NETVAR(m_hBlueTeamLeader, EHANDLE, "CTFPlayerDestructionLogic", "m_hBlueTeamLeader");

	inline CTFPlayer* GetTeamLeader(int iTeam)
	{
		auto pLeader = (iTeam == TF_TEAM_RED ? m_hRedTeamLeader() : m_hBlueTeamLeader()).Get();
		return pLeader ? pLeader->As<CTFPlayer>() : nullptr;
	}
};
