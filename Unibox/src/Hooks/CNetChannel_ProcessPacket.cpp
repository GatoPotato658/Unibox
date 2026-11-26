#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Ticks/Ticks.h"


MAKE_HOOK(CNetChannel_ProcessPacket, S::CNetChannel_ProcessPacket(), void,
	CNetChannel* pNetChan, void* packet, bool bHasHeader)
{
	DEBUG_RETURN(CNetChannel_ProcessPacket, pNetChan, packet, bHasHeader);

	if (G::Unload || pNetChan != I::ClientState->m_NetChannel)
		return CALL_ORIGINAL(pNetChan, packet, bHasHeader);

	const double dReceived = F::Ticks.PacketTime(pNetChan->IsLoopback());
	const int nPreviousSequenceNr = pNetChan->m_nInSequenceNr;

	F::Backtrack.PacketStart(pNetChan, bHasHeader);
	CALL_ORIGINAL(pNetChan, packet, bHasHeader);
	if (pNetChan != I::ClientState->m_NetChannel)
		return;

	F::Backtrack.PacketEnd(pNetChan, dReceived);
	if (bHasHeader && pNetChan->m_nInSequenceNr == nPreviousSequenceNr)
		return;

	F::Ticks.ObserveServerTick();

	static auto ppEventQueue = S::CL_EventQueue() ? reinterpret_cast<CEventInfo**>(U::Memory.RelToAbs(S::CL_EventQueue(), 3)) : nullptr;
	if (!ppEventQueue)
		return;

	for (auto pEvent = *ppEventQueue; pEvent; pEvent = pEvent->next)
		pEvent->fire_delay = 0.f;
	I::EngineClient->FireEvents();
}
