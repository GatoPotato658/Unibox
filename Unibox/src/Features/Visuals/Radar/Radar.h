#pragma once
#ifndef TEXTMODE

#include "../../../SDK/SDK.h"
#include "../../ImGui/IndicatorCache.h"

class CRadar
{
private:
	struct FloorArea_t
	{
		Vec3 m_vMin, m_vMax;
	};

	struct Player_t
	{
		Vec3 m_vOrigin;
		float m_flYaw;
		int m_iClass;
		bool m_bTeammate;
	};

	struct Snapshot_t
	{
		bool m_bValid = false;
		Vec3 m_vOrigin = {};
		float m_flYaw = 0.f;
		std::shared_ptr<const std::vector<FloorArea_t>> m_pFloor;
		std::vector<Player_t> m_vPlayers;
	};

	std::string m_sLevel;
	std::shared_ptr<const std::vector<FloorArea_t>> m_pFloor;
	CIndicatorCache<Snapshot_t> m_tCache;
	WindowBox_t m_tLastWindow;

	void LoadFloor();

public:
	void Store(CTFPlayer* pLocal);
	void Reset();
	void Draw(bool bInGame);
};

ADD_FEATURE(CRadar, Radar);

#endif
