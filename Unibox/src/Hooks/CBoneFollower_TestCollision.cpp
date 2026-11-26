#ifdef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CBoneFollower_TestCollision, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 4C 8B F9 48 8B EA", 0x0);

MAKE_SIGNATURE(CBoneFollower_TestCollision_ModelIndexOffset, "client.dll", "41 8B 97 ? ? ? ? 48 8B 01 FF 50 20 4C 8B 05", 0x0);
MAKE_SIGNATURE(CBoneFollower_TestCollision_SolidIndexOffset, "client.dll", "49 63 8F ? ? ? ? 48 8B D5 4C 8B 47 08", 0x0);

MAKE_HOOK(CBoneFollower_TestCollision, S::CBoneFollower_TestCollision(), bool,
    void* rcx, void *ray, unsigned int mask, void* trace)
{
	TRACK_HOOK;

	static const int iModelIndexOffset = S::CBoneFollower_TestCollision_ModelIndexOffset.ReadAt<int>(3);
	static const int iSolidIndexOffset = S::CBoneFollower_TestCollision_SolidIndexOffset.ReadAt<int>(3);

	int iModelIndex = *reinterpret_cast<int*>(uintptr_t(rcx) + iModelIndexOffset);
	int iSolidIndex = *reinterpret_cast<int*>(uintptr_t(rcx) + iSolidIndexOffset);
	auto pCollide = I::ModelInfoClient->GetVCollide(iModelIndex);
	if (!pCollide || !pCollide->solids || iSolidIndex >= pCollide->solidCount)
		return false;

	return CALL_ORIGINAL(rcx, ray, mask, trace);
}
#endif