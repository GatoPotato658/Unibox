#ifndef TEXTMODE
#include "../SDK/SDK.h"


#define JOIN_TIME (10.0)
#define FULL_TIME (180.0)
#define EXTEND_TIME (FULL_TIME - JOIN_TIME)

static std::unordered_map<void*, double> s_mLastAutoJoinTime = {};

MAKE_HOOK(CMatchInviteNotification_OnTick, S::CMatchInviteNotification_OnTick(), void,
	void* rcx)
{
	DEBUG_RETURN(CMatchInviteNotification_OnTick, rcx);

	static const int iAutoJoinTimeOffset = S::CMatchInviteNotification_AutoJoinTimeOffset.ReadAt<int>(7);
	if (Vars::Misc::Queueing::ExtendQueue.Value)
	{
		auto& dAutoJoinTime = *reinterpret_cast<double*>(uintptr_t(rcx) + iAutoJoinTimeOffset);
		auto& dLastAutoJoinTime = s_mLastAutoJoinTime[rcx];

		if (dAutoJoinTime != 0.0 && dAutoJoinTime != dLastAutoJoinTime)
		{
			const double dExtend = Vars::Misc::Queueing::ExtendQueue.Value == Vars::Misc::Queueing::ExtendQueueEnum::Join ? EXTEND_TIME : FULL_TIME;
			dLastAutoJoinTime = dAutoJoinTime += dExtend;
		}
	}

	CALL_ORIGINAL(rcx);
}
#endif
