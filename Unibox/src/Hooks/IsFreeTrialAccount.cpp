#include "../SDK/SDK.h"


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
