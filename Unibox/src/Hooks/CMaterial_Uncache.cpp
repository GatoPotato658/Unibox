#include "../SDK/SDK.h"

#include "../Features/Visuals/Materials/Materials.h"


static void CallUncacheOriginal(void* pOriginal, IMaterial* rcx, bool bPreserveVars)
{
	__try { reinterpret_cast<void(__fastcall*)(IMaterial*, bool)>(pOriginal)(rcx, bPreserveVars); }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

MAKE_HOOK(CMaterial_Uncache, S::CMaterial_Uncache(), void,
	IMaterial* rcx, bool bPreserveVars)
{
	DEBUG_RETURN(CMaterial_Uncache, rcx, bPreserveVars);

	if (F::Materials.IsCustomMaterial(rcx))
		return;

	CallUncacheOriginal(Hook.m_pOriginal, rcx, bPreserveVars);
}
