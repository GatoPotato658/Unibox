#include "../SDK/SDK.h"

#include <excpt.h>

// invalid seq/state crashfix
MAKE_SIGNATURE(CIKContext_CountRules, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 49 8B D8 8B FA 48 8B F1", 0x0);

MAKE_HOOK(CIKContext_CountRules, S::CIKContext_CountRules(), int,
	void* pStudioHdr, int iSequence, void* pPoseParameter)
{
	DEBUG_RETURN(CIKContext_CountRules, pStudioHdr, iSequence, pPoseParameter);

	if (G::Unload || !pStudioHdr)
		return 0;

	__try
	{
		return CALL_ORIGINAL(pStudioHdr, iSequence, pPoseParameter);
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return 0;
	}
}
