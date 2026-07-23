#if defined(_WIN64) && !defined(TEXTMODE)
#include "../SDK/SDK.h"
#include "../Features/Players/SteamProfileCache.h"

MAKE_SIGNATURE(CAvatarImage_ApplyImage, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B 41 40 41 BD FF FF 00 00 0F B7 1D ? ? ? ? 41 8B E9 48 89 44 24 40 4D 8B E0 F2 0F 10 44 24 40 8B F2 F2 0F 11 44 24 40 4C 8B F1 89 54 24 48 66 41 3B DD 0F 84 ? ? ? ? 48 8B 15 ? ? ? ? 66 90 0F B7 C3 48 83 C2 08 48 8D 0C 40 48 8D 3C CD ? ? ? ? 48 03 D7 48 8D 4C 24 40 FF 15 ? ? ? ? 84 C0", 0x0);

MAKE_HOOK(CAvatarImage_ApplyImage, S::CAvatarImage_ApplyImage(), void,
	void* pAvatarImage, int iImage, const uint8_t* pRgba, uint32_t uWidth, uint32_t uHeight)

{
	if (pAvatarImage && pRgba && uWidth && uHeight)
	{
		const uint64_t uSteamID = *reinterpret_cast<const uint64_t*>(reinterpret_cast<uintptr_t>(pAvatarImage) + 0x40);
		F::SteamProfileCache.CaptureNativeAvatar(uSteamID, pRgba, uWidth, uHeight);
	}
	CALL_ORIGINAL(pAvatarImage, iImage, pRgba, uWidth, uHeight);
}
#endif
