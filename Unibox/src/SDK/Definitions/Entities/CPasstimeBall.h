#pragma once
#include "CBaseAnimating.h"

class CTFPlayer;

class CPasstimeBall : public CBaseAnimating
{
public:
	NETVAR(m_iCollisionCount, int, "CPasstimeBall", "m_iCollisionCount");
	NETVAR(m_hHomingTarget, EHANDLE, "CPasstimeBall", "m_hHomingTarget");
	NETVAR(m_hCarrier, EHANDLE, "CPasstimeBall", "m_hCarrier");
	NETVAR(m_hPrevCarrier, EHANDLE, "CPasstimeBall", "m_hPrevCarrier");

	inline CTFPlayer* GetCarrier()
	{
		return m_hCarrier().Get() ? m_hCarrier().Get()->As<CTFPlayer>() : nullptr;
	}

	inline CTFPlayer* GetPrevCarrier()
	{
		return m_hPrevCarrier().Get() ? m_hPrevCarrier().Get()->As<CTFPlayer>() : nullptr;
	}
};
