#ifndef TEXTMODE
#include "../SDK/SDK.h"


MAKE_HOOK(CTFPlayerShared_IsPlayerDominated, S::CTFPlayerShared_IsPlayerDominated(), bool,
	void* rcx, int index)
{
	DEBUG_RETURN(CTFPlayerShared_IsPlayerDominated, rcx, index);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFClientScoreBoardDialog_UpdatePlayerList_IsPlayerDominated_Call();
	const auto dwJump = S::CTFClientScoreBoardDialog_UpdatePlayerList_Jump();

	bool bReturn = CALL_ORIGINAL(rcx, index);

	if (dwRetAddr == dwDesired && Vars::Visuals::UI::RevealScoreboard.Value && !SDK::CleanScreenshot() && !bReturn)
		*static_cast<uintptr_t*>(_AddressOfReturnAddress()) = dwJump;

	return bReturn;
}

MAKE_HOOK(KeyValues_SetInt, S::KeyValues_SetInt(), void,
	void* rcx, const char* keyName, int value)
{
	DEBUG_RETURN(KeyValues_SetInt, rcx, keyName, value);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFClientScoreBoardDialog_UpdatePlayerList_SetInt_Call();
	const auto dwJump = S::CTFClientScoreBoardDialog_UpdatePlayerList_Jump();

	CALL_ORIGINAL(rcx, keyName, value);

	if (dwRetAddr == dwDesired && Vars::Visuals::UI::RevealScoreboard.Value && !SDK::CleanScreenshot() && keyName && FNV1A::Hash32(keyName) == FNV1A::Hash32Const("nemesis"))
		*static_cast<uintptr_t*>(_AddressOfReturnAddress()) = dwJump;
}

MAKE_HOOK(CClientScoreBoardDialog_NeedsUpdate, S::CClientScoreBoardDialog_NeedsUpdate(), bool,
	void* rcx)
{
	DEBUG_RETURN(CClientScoreBoardDialog_NeedsUpdate, rcx);

	static bool bStaticMod = false;
	const bool bLastMod = bStaticMod;
	const bool bCurrMod = bStaticMod = (Vars::Visuals::UI::RevealScoreboard.Value || Vars::Visuals::UI::ScoreboardColors.Value) && !SDK::CleanScreenshot();

	if (bCurrMod != bLastMod)
		return true;

	return CALL_ORIGINAL(rcx);
}
#endif