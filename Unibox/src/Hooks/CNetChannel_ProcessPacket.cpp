#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Ticks/Ticks.h"

MAKE_SIGNATURE(CNetChannel_ProcessPacket, "engine.dll", "44 88 44 24 18 48 89 54 24 10 53", 0x0);
MAKE_SIGNATURE(CL_EventQueue, "engine.dll", "48 8B 2D ? ? ? ? 48 85 ED 0F 84", 0x0);

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
