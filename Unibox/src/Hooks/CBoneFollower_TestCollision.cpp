#ifdef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CBoneFollower_TestCollision, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 4C 8B F9 48 8B EA", 0x0);

MAKE_HOOK(CBoneFollower_TestCollision, S::CBoneFollower_TestCollision(), bool,
    void* rcx, void *ray, unsigned int mask, void* trace)
{	
	// Avoid crash
	int iModelIndex = *reinterpret_cast<int*>(uintptr_t(rcx) + 0x7C0);
	int iSolidIndex = *reinterpret_cast<int*>(uintptr_t(rcx) + 0x7C4);
	auto pCollide = I::ModelInfoClient->GetVCollide(iModelIndex);
	if (!pCollide || !pCollide->solids || iSolidIndex >= pCollide->solidCount)
		return false;

	return CALL_ORIGINAL(rcx, ray, mask, trace);
}
#endif