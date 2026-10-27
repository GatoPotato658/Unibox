#pragma once
#include <cmath>

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
	inline constexpr float DUCK_LIFT = HULL_HEIGHT - HULL_DUCK_HEIGHT;
	inline constexpr float CROUCH_JUMP_REACH = JUMP_APEX + DUCK_LIFT;
	inline constexpr float AIR_SPEED_CAP = 30.f;
	inline constexpr float TAKEOFF_SPEED_SCALE = 0.9f;
	inline constexpr float TAKEOFF_EARLY_MARGIN = 12.f;

	inline bool CanJumpAcross(float flGap2D, float flRise, float flRunSpeed)
	{
		if (flRise <= STEP_HEIGHT && flGap2D <= 2.f * STEP_HEIGHT)
			return true;
		if (flRise > CROUCH_JUMP_REACH)
			return false;

		if (flGap2D <= HULL_HALF_WIDTH)
			return true;

		const float flSpeed = flRunSpeed * TAKEOFF_SPEED_SCALE;
		if (flSpeed <= 0.f)
			return false;

		const float flTravel = flGap2D - HULL_HALF_WIDTH + TAKEOFF_EARLY_MARGIN;
		const float flDiscriminant = JUMP_SPEED * JUMP_SPEED - 2.f * GRAVITY * (flRise - DUCK_LIFT);
		if (flDiscriminant < 0.f)
			return false;

		const float flLatestTime = (JUMP_SPEED + std::sqrt(flDiscriminant)) / GRAVITY;
		return flTravel / flSpeed <= flLatestTime;
	}

	inline bool CanWalkOffAcross(float flGap2D, float flDrop, float flRunSpeed)
	{
		const float flTravel = flGap2D > HULL_HALF_WIDTH ? flGap2D - HULL_HALF_WIDTH : 0.f;
		if (flTravel <= 0.f)
			return true;

		const float flSpeed = flRunSpeed * TAKEOFF_SPEED_SCALE;
		if (flSpeed <= 0.f || flDrop <= 0.f)
			return false;

		return flTravel / flSpeed <= std::sqrt(2.f * flDrop / GRAVITY);
	}

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
