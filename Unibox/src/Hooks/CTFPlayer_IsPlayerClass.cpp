#ifndef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CTFPlayer_IsPlayerClass, "client.dll", "48 81 C1 ? ? ? ? 75 ? 32 C0", 0x0);

MAKE_HOOK(CTFPlayer_IsPlayerClass, S::CTFPlayer_IsPlayerClass(), bool,
	void* rcx, int iClass)
{
	DEBUG_RETURN(CTFPlayer_IsPlayerClass, rcx, iClass);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	static const auto dwDesired = []()
	{
		const auto dwAddress = U::Memory.FindOptionalSignature("client.dll", "BA 08 00 00 00 49 8B CE E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 48 8B 06 48 8B CE FF 90 ?? ?? ?? ?? 49 8B 16 49 8B CE 8B D8 FF 92 ?? ?? ?? ?? 3B C3 74 ?? BA 39 00 00 00 49 8B CE");
		return dwAddress ? dwAddress + 0xD : 0x0;
	}();

	if (dwRetAddr == dwDesired && Vars::Misc::Sound::HitsoundAlways.Value)
		return false;

	return CALL_ORIGINAL(rcx, iClass);
}
#endif
