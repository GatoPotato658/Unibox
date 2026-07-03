#pragma once
#include "BotUtils.h"
#include "NavBotJobs/NavBotJobs.h"
#include "NavBotConfig.h"

class CNavArea;
class CNavBotCore
{
public:
	NavBotClassConfig_t m_tSelectedConfig = NavBotConfig::CONFIG_MID_RANGE;
private:
	void UpdateSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy);
	void UpdateRunReloadInput(CUserCmd* pCmd, bool bShouldHold);
	void ResetRuntimeState(CUserCmd* pCmd);
public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
	void Draw(CTFPlayer* pLocal);
	void DrawDangerOverlay(CTFPlayer* pLocal);

private:

	Timer m_tIdleTimer = {};
	Timer m_tAntiStuckTimer = {};
	float m_flNextStuckAngleChange = 0.f;
	float m_flNextIdleTime = 0.f;
	Vec3 m_vStuckAngles = {};
	bool m_bHoldingRunReload = false;
	CNavBotJobSystem m_tJobSystem = {};
};

ADD_FEATURE(CNavBotCore, NavBotCore);
