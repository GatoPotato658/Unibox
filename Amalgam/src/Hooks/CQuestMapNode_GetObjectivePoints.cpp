#include "../SDK/SDK.h"

MAKE_SIGNATURE(CQuestMapNode_GetObjectivePoints, "client.dll", "85 D2 74 ? 83 EA 01 74 ? 83 FA 01 74 ? 33 C0 C3 8B 41 3C C3 8B 41 38 C3 8B 41 34 C3", 0x0);

MAKE_HOOK(CQuestMapNode_GetObjectivePoints, S::CQuestMapNode_GetObjectivePoints(), uint32_t,
	void* rcx, int iObjective)
{
	DEBUG_RETURN(CQuestMapNode_GetObjectivePoints, rcx, iObjective);

	if (!rcx)
		return 0;

	return CALL_ORIGINAL(rcx, iObjective);
}
