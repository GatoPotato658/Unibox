#include "../SDK/SDK.h"

#include "../Features/World/World.h"

MAKE_SIGNATURE(CM_FreeMap, "engine.dll", "48 8D 0D ? ? ? ? E9 ? ? ? ? CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 45 33 C9", 0x0);

MAKE_HOOK(CM_FreeMap, S::CM_FreeMap(), void,
	)
{
	DEBUG_RETURN(CM_FreeMap);

	CALL_ORIGINAL();

	F::World.Uncache();
}