#pragma once
#include "../../../SDK/SDK.h"
#include "../../ImGui/IndicatorCache.h"

class CPlayerConditions
{
private:
	CIndicatorCache<std::vector<std::string>> m_tDrawCache = {};

public:
	std::vector<std::string> Get(CTFPlayer* pEntity);
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();
};

ADD_FEATURE(CPlayerConditions, PlayerConditions);
