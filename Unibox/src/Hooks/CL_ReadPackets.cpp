#include "../SDK/SDK.h"

#include "../Features/NetworkFix/NetworkFix.h"


MAKE_HOOK(CL_ReadPackets, S::CL_ReadPackets(), void,
	bool bFinalTick)
{
	DEBUG_RETURN(CL_ReadPackets, bFinalTick);

	if (F::NetworkFix.ShouldReadPackets())
		CALL_ORIGINAL(bFinalTick);
}