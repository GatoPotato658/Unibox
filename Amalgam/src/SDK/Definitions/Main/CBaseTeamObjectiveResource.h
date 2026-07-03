#pragma once
#include "CBaseEntity.h"

class CBaseTeamObjectiveResource : public CBaseEntity
{
public:
	NETVAR(m_iNumControlPoints, int, "CBaseTeamObjectiveResource", "m_iNumControlPoints");
	NETVAR(m_bPlayingMiniRounds, bool, "CBaseTeamObjectiveResource", "m_bPlayingMiniRounds");
	NETVAR(m_nMannVsMachineMaxWaveCount, int, "CTFObjectiveResource", "m_nMannVsMachineMaxWaveCount");
	NETVAR(m_nMannVsMachineWaveCount, int, "CTFObjectiveResource", "m_nMannVsMachineWaveCount");
	NETVAR(m_bMannVsMachineBetweenWaves, bool, "CTFObjectiveResource", "m_bMannVsMachineBetweenWaves");

	NETVAR_ARRAY(m_vCPPositions, Vector, "CBaseTeamObjectiveResource", "m_vCPPositions[0]");
	NETVAR_ARRAY(m_iBaseControlPoints, int, "CBaseTeamObjectiveResource", "m_iBaseControlPoints");
	NETVAR_ARRAY(m_iPreviousPoints, int, "CBaseTeamObjectiveResource", "m_iPreviousPoints");
	NETVAR_ARRAY(m_iOwner, int, "CBaseTeamObjectiveResource", "m_iOwner");
	NETVAR_ARRAY(m_bCPLocked, bool, "CBaseTeamObjectiveResource", "m_bCPLocked");
	NETVAR_ARRAY(m_bTeamCanCap, bool, "CBaseTeamObjectiveResource", "m_bTeamCanCap");
	NETVAR_ARRAY(m_bInMiniRound, bool, "CBaseTeamObjectiveResource", "m_bInMiniRound");
};
