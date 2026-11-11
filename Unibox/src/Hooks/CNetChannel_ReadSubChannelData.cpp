#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"

MAKE_SIGNATURE(CNetChannel_ReadSubChannelData, "engine.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 56 41 57 48 83 EC 30 49 63 C0", 0x0);

MAKE_HOOK(CNetChannel_ReadSubChannelData, S::CNetChannel_ReadSubChannelData(), bool,
	CNetChannel* pNetChan, bf_read& buf, int stream)
{
	DEBUG_RETURN(CNetChannel_ReadSubChannelData, pNetChan, buf, stream);

	if (G::Unload || pNetChan != I::ClientState->m_NetChannel || !F::Backtrack.IsFiltering(pNetChan))
		return CALL_ORIGINAL(pNetChan, buf, stream);

	if (F::Backtrack.SkipBlock(pNetChan, buf, stream))
		return true;

	const bool bAccepted = CALL_ORIGINAL(pNetChan, buf, stream);
	if (bAccepted)
		F::Backtrack.AcceptBlock(stream);
	return bAccepted;
}
