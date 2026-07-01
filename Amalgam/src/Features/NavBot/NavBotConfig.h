#pragma once
#include "../../SDK/SDK.h"

struct NavBotClassConfig_t
{
	float m_flMinFullDanger = 0.f;
	float m_flMinSlightDanger = 0.f;
	float m_flMax = 0.f;
	bool m_bPreferFar = false;
};

namespace NavBotConfig
{
	inline constexpr NavBotClassConfig_t CONFIG_SHORT_RANGE = { 140.0f, 400.0f, 600.0f, false };
	inline constexpr NavBotClassConfig_t CONFIG_MID_RANGE = { 200.0f, 500.0f, 3000.0f, true };
	inline constexpr NavBotClassConfig_t CONFIG_LONG_RANGE = { 300.0f, 500.0f, 4000.0f, true };
	inline constexpr NavBotClassConfig_t CONFIG_ENGINEER = { 200.0f, 500.0f, 3000.0f, false };
	inline constexpr NavBotClassConfig_t CONFIG_GUNSLINGER_ENGINEER = { 50.0f, 300.0f, 2000.0f, false };

	NavBotClassConfig_t Select(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
}