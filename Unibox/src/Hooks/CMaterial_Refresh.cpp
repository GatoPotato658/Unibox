#include "../SDK/SDK.h"


static int CallRefreshOriginal(void* pOriginal, void* rcx)
{
	int iReturn;
	__try { iReturn = reinterpret_cast<int(__fastcall*)(void*)>(pOriginal)(rcx); }
	__except (EXCEPTION_EXECUTE_HANDLER) { iReturn = 0; }
	return iReturn;
}

MAKE_HOOK(CMaterial_Refresh, S::CMaterial_Refresh(), int,
	void* rcx)
{
	DEBUG_RETURN(CMaterial_Refresh, rcx);

	return CallRefreshOriginal(Hook.m_pOriginal, rcx);
}
