#pragma once
#include "BotUtils.h"
#include "Jobs/NavBotJobs.h"
#include "NavBotConfig.h"
#include "../ImGui/IndicatorCache.h"

class CNavArea;
class CNavBotCore
{
public:
	NavBotClassConfig_t m_tSelectedConfig = NavBotConfig::CONFIG_MID_RANGE;
private:
	void UpdateSlot(CTFPlayer* pLocal, const ClosestEnemy_t& tClosestEnemy);
	void UpdateRunReloadInput(CUserCmd* pCmd, bool bShouldHold);
	void ResetRuntimeState(CUserCmd* pCmd);
	void ResetBusy(CUserCmd* pCmd);
public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();
	void DrawDangerOverlay(CTFPlayer* pLocal);

private:
	struct NavIndicatorLine_t
	{
		std::string m_sText = {};
		Color_t m_tColor = {};
	};
	CIndicatorCache<std::vector<NavIndicatorLine_t>> m_tDrawCache = {};

	Timer m_tIdleTimer = {};
	Timer m_tAntiStuckTimer = {};
	float m_flNextStuckAngleChange = 0.f;
	float m_flNextIdleTime = 0.f;
	Vec3 m_vStuckAngles = {};
	bool m_bHoldingRunReload = false;
	CNavBotJobSystem m_tJobSystem = {};
};

ADD_FEATURE(CNavBotCore, NavBotCore);
