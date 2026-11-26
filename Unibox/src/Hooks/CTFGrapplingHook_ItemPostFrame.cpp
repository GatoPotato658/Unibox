#include "../SDK/SDK.h"


static bool s_bOriginalFired = false;

MAKE_HOOK(CTFGrapplingHook_ItemPostFrame, S::CTFGrapplingHook_ItemPostFrame(), void,
	void* rcx)
{
	TRACK_HOOK;

	static bool& bFired = *reinterpret_cast<bool*>(U::Memory.RelToAbs(S::CTFGrapplingHook_ItemPostFrame_Fired()));
	s_bOriginalFired = bFired;
	bFired = false; // this var breaks prediction for this weapon pretty hard otherwise
	CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CTFGrapplingHook_PrimaryAttack, S::CTFGrapplingHook_PrimaryAttack(), void,
	void* rcx)
{
	TRACK_HOOK;

	bool bOriginalFirstTimePredicted = I::Prediction->m_bFirstTimePredicted;
	I::Prediction->m_bFirstTimePredicted = !s_bOriginalFired; // don't spam deny sound
	CALL_ORIGINAL(rcx);
	I::Prediction->m_bFirstTimePredicted = bOriginalFirstTimePredicted;
}