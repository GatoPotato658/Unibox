#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"


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
