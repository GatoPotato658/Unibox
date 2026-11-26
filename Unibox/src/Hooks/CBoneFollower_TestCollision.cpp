#ifdef TEXTMODE
#include "../SDK/SDK.h"



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