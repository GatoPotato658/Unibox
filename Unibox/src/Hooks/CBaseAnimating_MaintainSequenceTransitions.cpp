#include "../SDK/SDK.h"

MAKE_SIGNATURE(CBaseAnimating_MaintainSequenceTransitions, "client.dll", "4C 89 4C 24 ? 41 56", 0x0);

MAKE_HOOK(CBaseAnimating_MaintainSequenceTransitions, S::CBaseAnimating_MaintainSequenceTransitions(), void,
	CBaseAnimating* rcx, void* boneSetup, float flCycle, Vec3 pos[], Vector4D q[])
{
	DEBUG_RETURN(CBaseAnimating_MaintainSequenceTransitions, rcx, boneSetup, flCycle, pos, q);
}