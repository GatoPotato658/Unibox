#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"


MAKE_HOOK(CNetChannel_SendDatagram, S::CNetChannel_SendDatagram(), int,
	CNetChannel* pNetChan, bf_write* datagram)
{
	DEBUG_RETURN(CNetChannel_SendDatagram, pNetChan, datagram);

	if (datagram || G::Unload || pNetChan != I::ClientState->m_NetChannel)
		return CALL_ORIGINAL(pNetChan, datagram);

	F::Backtrack.AdjustPing(pNetChan);
	const int iReturn = CALL_ORIGINAL(pNetChan, datagram);
	F::Backtrack.RestorePing(pNetChan, iReturn > 0);
	return iReturn;
}
