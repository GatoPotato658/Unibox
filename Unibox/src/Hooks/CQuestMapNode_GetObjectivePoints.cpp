#include "../SDK/SDK.h"


MAKE_HOOK(CQuestMapNode_GetObjectivePoints, S::CQuestMapNode_GetObjectivePoints(), uint32_t,
	void* rcx, int iObjective)
{
	DEBUG_RETURN(CQuestMapNode_GetObjectivePoints, rcx, iObjective);

	if (!rcx)
		return 0;

	return CALL_ORIGINAL(rcx, iObjective);
}
