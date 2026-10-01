#pragma once
#include "../../../SDK/SDK.h"
#include "../../ImGui/IndicatorCache.h"

class CSpectatorList
{
private:
	struct Spectator_t
	{
		std::string m_sName;
		const char* m_sMode;
		float m_flRespawnIn;
		bool m_bRespawnTimeIncreased;
		bool m_bIsFriend;
		bool m_bInParty;
		int m_iIndex;
	};

	std::vector<Spectator_t> m_vSpectators = {};
	std::unordered_map<int, float> m_mRespawnCache = {};
	CIndicatorCache<std::vector<Spectator_t>> m_tDrawCache = {};

public:
	bool GetSpectators(CTFPlayer* pTarget);
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();
};

ADD_FEATURE(CSpectatorList, SpectatorList);