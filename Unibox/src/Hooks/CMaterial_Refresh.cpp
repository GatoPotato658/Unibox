#include "../SDK/SDK.h"

MAKE_SIGNATURE(CMaterial_Refresh, "materialsystem.dll", "41 56 48 83 EC 30 48 8B 01 4C 8B F1 FF 90 C0 01 00 00 84 C0 0F 85", 0x0);

MAKE_HOOK(CMaterial_Refresh, S::CMaterial_Refresh(), int,
	void* rcx)
{
	DEBUG_RETURN(CMaterial_Refresh, rcx);

	int iReturn;
	__try { iReturn = CALL_ORIGINAL(rcx); }
	__except (EXCEPTION_EXECUTE_HANDLER) { iReturn = 0; }
	return iReturn;
}
