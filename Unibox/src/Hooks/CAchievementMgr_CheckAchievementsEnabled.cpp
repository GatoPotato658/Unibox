#include "../SDK/SDK.h"


MAKE_HOOK(CAchievementMgr_CheckAchievementsEnabled, S::CAchievementMgr_CheckAchievementsEnabled(), bool,
	void* rcx)
{
	DEBUG_RETURN(CAchievementMgr_CheckAchievementsEnabled, rcx);

	return !I::EngineClient->IsPlayingDemo();
}