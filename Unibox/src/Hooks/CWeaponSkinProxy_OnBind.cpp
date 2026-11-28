#ifndef TEXTMODE
#include "../Features/Visuals/UltraToaster/UltraToaster.h"


namespace Hooks::CWeaponSkinProxy_OnBind
{
	using FN = void(__fastcall*)(void*, void*);

	static bool Init();
	static CHook Hook("CWeaponSkinProxy_OnBind", Init);

	static void __fastcall Func(void* rcx, void* pRenderable)
	{
		TRACK_HOOK;
		if (pRenderable && Vars::Visuals::UltraToaster::WeaponSkins.Value && UltraToaster::IsActive())
			pRenderable = nullptr;
		Hook.As<FN>()(rcx, pRenderable);
	}

	static bool Init()
	{
		auto pProxy = U::Memory.FindInterface("client.dll", "WeaponSkin_IMaterialProxy003");
		if (!pProxy)
			return true;

		auto pOnBind = U::Memory.GetVirtual(pProxy, 1);
		U::Memory.CallVirtual<2, void>(pProxy);
		if (pOnBind)
			Hook.Create(pOnBind, Func);
		return true;
	}
}
#endif
