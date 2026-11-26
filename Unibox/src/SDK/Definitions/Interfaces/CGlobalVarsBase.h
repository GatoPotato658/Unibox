#pragma once
#include "Interface.h"

class CSaveRestoreData;

class CGlobalVarsBase
{
public:
	float realtime;
	int framecount;
	float absoluteframetime;
	float curtime;
	float frametime;
	int maxClients;
	int tickcount;
	float interval_per_tick;
	float interpolation_amount;
	int simTicksThisFrame;
	int network_protocol;
	CSaveRestoreData* pSaveData;
	bool m_bClient;
	int nTimestampNetworkingBase;
	int nTimestampRandomizeWindow;
};
