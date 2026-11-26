#include "../SDK/SDK.h"
#include "../Features/Spectate/Spectate.h"


MAKE_HOOK(CClientState_ProcessFixAngle, S::CClientState_ProcessFixAngle(), bool,
	CClientState* rcx, SVC_FixAngle* msg)
{
	DEBUG_RETURN(CClientState_ProcessFixAngle, rcx, msg);

	if (Vars::Visuals::Removals::AngleForcing.Value)
		return false;

	if (F::Spectate.HasTarget())
		F::Spectate.m_vOldView = msg->m_Angle;

	return CALL_ORIGINAL(rcx, msg);
}