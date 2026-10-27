#pragma once
#include "../../SDK/SDK.h"

class CSpectate
{
private:
	int m_iTarget = -1;
	int m_iIntendedTarget = -1;

public:
	void NetUpdateEnd(CTFPlayer* pLocal);
	void NetUpdateStart(CTFPlayer* pLocal);
	void CreateMove(CUserCmd* pCmd);
	void Reset();

	void SetTarget(int iTarget);
	int GetTarget(bool bIntended = false);
	inline bool HasTarget(bool bIntended = false) { return (bIntended ? m_iIntendedTarget : m_iTarget) != -1; }

	EHANDLE m_hOriginalTarget;
	int m_iOriginalMode = OBS_MODE_NONE;

	Vec3 m_vOldView = {}; // don't let spectating change viewangles
};

ADD_FEATURE(CSpectate, Spectate);