#ifndef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CMatchInviteNotification_OnTick, "client.dll", "40 53 48 83 EC ? 48 8B D9 E8 ? ? ? ? F7 83", 0x0);
MAKE_SIGNATURE(CMatchInviteNotification_AutoJoinTimeOffset, "client.dll", "0F 57 FF 66 0F 2E BB ? ? ? ? 74 ? FF 15 ? ? ? ? 66 0F 2F 83", 0x0);

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
