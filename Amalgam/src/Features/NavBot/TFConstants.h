#pragma once

namespace TFGame
{
	inline constexpr float HULL_HALF_WIDTH = 24.f;
	inline constexpr float HULL_HEIGHT = 82.f;
	inline constexpr float HULL_DUCK_HEIGHT = 62.f;
	inline constexpr float VIEW_HEIGHT_DEFAULT = 72.f;

	inline constexpr float STEP_HEIGHT = 18.f;
	inline constexpr float GRAVITY = 800.f;

	inline constexpr float JUMP_SPEED = 289.f;
	inline constexpr float JUMP_APEX = JUMP_SPEED * JUMP_SPEED / (2.f * GRAVITY);
	inline constexpr float CROUCH_JUMP_REACH = JUMP_APEX + (HULL_HEIGHT - HULL_DUCK_HEIGHT);

	inline constexpr float MAX_SAFE_FALL_SPEED = 650.f;
	inline constexpr float MAX_SAFE_DROP_HEIGHT = MAX_SAFE_FALL_SPEED * MAX_SAFE_FALL_SPEED / (2.f * GRAVITY);

	inline constexpr float SENTRY_MAX_RANGE = 1100.f;
	inline constexpr float SENTRY_EYE_OFFSET_L1 = 32.f;
	inline constexpr float SENTRY_EYE_OFFSET_L2 = 40.f;
	inline constexpr float SENTRY_EYE_OFFSET_L3 = 46.f;
	inline constexpr float SENTRY_IGNORE_INVIS = 0.5f;

	constexpr float SentryEyeOffset(int iUpgradeLevel)
	{
		return iUpgradeLevel >= 3 ? SENTRY_EYE_OFFSET_L3 : iUpgradeLevel == 2 ? SENTRY_EYE_OFFSET_L2 : SENTRY_EYE_OFFSET_L1;
	}

	inline constexpr float STICKY_ARM_TIME = 0.8f;
}
