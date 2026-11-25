#ifndef TEXTMODE
#include "../SDK/SDK.h"

static ITexture* CallFindTextureOriginal(void* pOriginal, void* rcx, char const* pTextureName, const char* pTextureGroupName, bool complain, int nAdditionalCreationFlags)
{
	ITexture* pReturn;
	__try { pReturn = reinterpret_cast<ITexture*(__fastcall*)(void*, char const*, const char*, bool, int)>(pOriginal)(rcx, pTextureName, pTextureGroupName, complain, nAdditionalCreationFlags); }
	__except (EXCEPTION_EXECUTE_HANDLER) { pReturn = nullptr; }
	return pReturn;
}

MAKE_HOOK(IMaterialSystem_FindTexture, U::Memory.GetVirtual(I::MaterialSystem, 79), ITexture*,
	void* rcx, char const* pTextureName, const char* pTextureGroupName, bool complain, int nAdditionalCreationFlags)
{
	DEBUG_RETURN(IMaterialSystem_FindTexture, rcx, pTextureName, pTextureGroupName, complain, nAdditionalCreationFlags);

	ITexture* pReturn = CallFindTextureOriginal(Hook.m_pOriginal, rcx, pTextureName, pTextureGroupName, complain, nAdditionalCreationFlags);

	auto uHash = FNV1A::Hash32(Vars::Visuals::World::WorldTexture.Value.c_str());
	if (uHash == FNV1A::Hash32Const("Flat"))
	{
		auto fOverrideTexture = [&]()
		{
			if (!pReturn || pReturn->IsTranslucent() || !pTextureName || !pTextureGroupName)
				return;

			std::string_view sName = pTextureName;
			std::string_view sGroup = pTextureGroupName;
			if (!sGroup.starts_with(TEXTURE_GROUP_WORLD))
				return;

			Vec3 vColor; pReturn->GetLowResColorSample(0.5f, 0.5f, &vColor.x);
			Color_t tColor = { byte(vColor.x * 255), byte(vColor.y * 255), byte(vColor.z * 255), 255 };
			pReturn = I::MaterialSystem->CreateTextureFromBits(1, 1, 1, IMAGE_FORMAT_RGBA8888, 4, &tColor.r);
		};
		fOverrideTexture();
	}

	return pReturn;
}
#endif
