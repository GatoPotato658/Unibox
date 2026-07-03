#pragma once
#include "../../SDK/SDK.h"

struct FollowTarget_t
{
	int m_iEntIndex = -1;
	int m_iUserID = -1;
	int m_iPriority = 0;
	int m_iPrefer = 0;
	int m_iClassPreference = 0;
	float m_flDistance = -1.f;

	bool m_bUnreachable = false;
	bool m_bNew = true;
	bool m_bDormant = false;
	Vec3 m_vLastKnownPos = {};

	uint32_t m_uNameHash = 0;
	CTFPlayer* m_pPlayer = nullptr;
};

struct FollowTargetPath_t
{
	Vec3 m_vOrigin = {};
	std::deque<Vec3> m_vAngles = {};
};

#define FB_RESET_NONE 0
#define FB_RESET_TARGETS 1 << 0
#define FB_RESET_NAV 1 << 1
class CFollowBot
{
private:
	void UpdateTargets(CTFPlayer* pLocal);
	void UpdateLockedTarget(CTFPlayer* pLocal);

	bool IsValidTarget(CTFPlayer* pLocal, CTFPlayer* pPlayer);
	bool IsAfkTarget(CTFPlayer* pPlayer);

	void LookAtPath(CTFPlayer* pLocal, CUserCmd* pCmd, std::deque<Vec3>* vIn, bool bSmooth);

	std::vector<FollowTarget_t> m_vTargets = {};

	std::array<float, MAX_PLAYERS> m_aLastActiveTimes = {};
	std::array<Vec3, MAX_PLAYERS> m_aLastOrigins = {};
	std::array<Vec3, MAX_PLAYERS> m_aLastAngles = {};

	std::deque<FollowTargetPath_t> m_vCurrentPath = {};
	std::deque<Vec3> m_vTempAngles = {};
	Vec3 m_vLastTargetAngles = {};
public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd);
	void Reset(int iFlags = FB_RESET_TARGETS);

	void Render();

	bool m_bActive = false;
	FollowTarget_t m_tLockedTarget = {};
};

ADD_FEATURE(CFollowBot, FollowBot);
