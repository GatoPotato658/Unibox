#pragma once
#include "CBaseEntity.h"
#include "CPasstimeBall.h"

class CTFPasstimeLogic : public CBaseEntity
{
public:
	NETVAR(m_hBall, EHANDLE, "CTFPasstimeLogic", "m_hBall");
	NETVAR(m_iNumSections, int, "CTFPasstimeLogic", "m_iNumSections");
	NETVAR(m_iCurrentSection, int, "CTFPasstimeLogic", "m_iCurrentSection");
	NETVAR(m_flMaxPassRange, float, "CTFPasstimeLogic", "m_flMaxPassRange");
	NETVAR(m_iBallPower, int, "CTFPasstimeLogic", "m_iBallPower");
	NETVAR(m_flPackSpeed, float, "CTFPasstimeLogic", "m_flPackSpeed");

	inline CPasstimeBall* GetBall()
	{
		return m_hBall().Get() ? m_hBall().Get()->As<CPasstimeBall>() : nullptr;
	}
};
