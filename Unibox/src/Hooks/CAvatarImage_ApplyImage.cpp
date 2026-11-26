#if defined(_WIN64) && !defined(TEXTMODE)
#include "../SDK/SDK.h"
#include "../Features/Players/SteamProfileCache.h"



MAKE_HOOK(CAvatarImage_ApplyImage, S::CAvatarImage_ApplyImage(), void,
	void* pAvatarImage, int iImage, const uint8_t* pRgba, uint32_t uWidth, uint32_t uHeight)

{
	TRACK_HOOK;

	if (pAvatarImage && pRgba && uWidth && uHeight)
	{
		static const int iSteamIDOffset = S::CAvatarImage_ApplyImage_SteamIDOffset.ReadAt<uint8_t>(9);
		const uint64_t uSteamID = *reinterpret_cast<const uint64_t*>(reinterpret_cast<uintptr_t>(pAvatarImage) + iSteamIDOffset);
		F::SteamProfileCache.CaptureNativeAvatar(uSteamID, pRgba, uWidth, uHeight);
	}
	CALL_ORIGINAL(pAvatarImage, iImage, pRgba, uWidth, uHeight);
}
#endif
