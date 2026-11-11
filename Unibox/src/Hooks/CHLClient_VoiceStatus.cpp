#include "../SDK/SDK.h"

#include "../Features/Misc/Radio/Radio.h"

MAKE_HOOK(CHLClient_VoiceStatus, U::Memory.GetVirtual(I::Client, 33), void,
	void* rcx, int iEntity, qboolean bTalking)
{
	DEBUG_RETURN(CHLClient_VoiceStatus, rcx, iEntity, bTalking);

	CALL_ORIGINAL(rcx, iEntity, bTalking);
	if (!G::Unload)
		F::Radio.OnVoiceStatus(iEntity, bTalking);
}
