#include "../SDK/SDK.h"


static inline int GetNetworkBase(int nTick, int nEntity)
{
	int nEntityMod = nEntity % I::GlobalVars->nTimestampRandomizeWindow;
	int nBaseTick = I::GlobalVars->nTimestampNetworkingBase * int((nTick - nEntityMod) / I::GlobalVars->nTimestampNetworkingBase);
	return nBaseTick;
}

MAKE_HOOK(RecvProxy_SimulationTime, S::RecvProxy_SimulationTime(), void,
	const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	DEBUG_RETURN(RecvProxy_SimulationTime, pData, pStruct, pOut);

	auto pEntity = reinterpret_cast<CBaseEntity*>(pStruct);
	if (!pEntity || !pEntity->IsPlayer() || pEntity->entindex() == I::EngineClient->GetLocalPlayer())
		return CALL_ORIGINAL(pData, pStruct, pOut);

	if (!pData->m_Value.m_Int) // fix setting invalid simtime every 100 ticks if choking
		return;

	int addt = pData->m_Value.m_Int;
	int t = GetNetworkBase(I::GlobalVars->tickcount, pEntity->entindex());
	t += addt;

	while (t < I::GlobalVars->tickcount - 127)
		t += 256;
	while (t > I::GlobalVars->tickcount + 127)
		t -= 256;

	pEntity->m_flSimulationTime() = TICKS_TO_TIME(t);
}