#include "../SDK/SDK.h"

MAKE_SIGNATURE(IsFreeTrialAccount, "client.dll", "48 83 EC 28 E8 ?? ?? ?? ?? 48 85 C0 74 ?? E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 48 85 C0 74 ?? E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 48 83 B8 98 ?? ?? ?? ?? 74 ?? E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? BA 07 00 00 00 48 8B 88 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0 74 ?? 83 78 28 01 75 ?? 48 8B 40 08 48 8B 00 48 85 C0 74 ?? 0F B6 40 24 48 83 C4 28 C3 32 C0 48 83 C4 28 C3 CC", 0x0);

MAKE_HOOK(IsFreeTrialAccount, S::IsFreeTrialAccount(), bool)
{
	DEBUG_RETURN(IsFreeTrialAccount);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	static const auto dwDesired = U::Memory.FindOptionalSignature("client.dll", "84 C0 74 ? 41 38 7D");
	if (!dwDesired)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			SDK::Output("IsFreeTrialAccount", "optional signature not unique, hook disabled");
		}
		return CALL_ORIGINAL();
	}

	if (dwRetAddr == dwDesired)
		return false;

	return CALL_ORIGINAL();
}
