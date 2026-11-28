#include "../SDK/SDK.h"

#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/Visuals/Glow/Glow.h"
#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/Visuals/UltraToaster/UltraToaster.h"

#include <cstddef>
#include <string_view>


static bool s_bDrawingViewmodel = false;

MAKE_HOOK(IVModelRender_DrawModelExecute, U::Memory.GetVirtual(I::ModelRender, 19), void,
	IVModelRender* rcx, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	DEBUG_RETURN(IVModelRender_DrawModelExecute, rcx, pState, pInfo, pBoneToWorld);

#ifndef TEXTMODE
	if (I::EngineVGui->IsGameUIVisible() || SDK::CleanScreenshot()
		|| F::CameraWindow.m_bDrawing || !F::Materials.m_bLoaded || G::Unload)
		return CALL_ORIGINAL(rcx, pState, pInfo, pBoneToWorld);

	if (F::Chams.m_bRendering)
		return F::Chams.RenderHandler(rcx, pState, pInfo, pBoneToWorld);
	if (F::Glow.m_bRendering)
		return F::Glow.RenderHandler(rcx, pState, pInfo, pBoneToWorld);

	if (F::Chams.m_mEntities.contains(pInfo.entity_index))
		return;

	auto pEntityBase = I::ClientEntityList->GetClientEntity(pInfo.entity_index);
	auto pEntity = pEntityBase ? pEntityBase->As<CBaseEntity>() : nullptr;
	if (pEntity && pEntity->IsWearableVM() /*pEntity->IsViewmodel()*/)
	{
		F::Glow.RenderViewmodel(rcx, pState, pInfo, pBoneToWorld);
		if (F::Chams.RenderViewmodel(rcx, pState, pInfo, pBoneToWorld))
			return;
	}

	CALL_ORIGINAL(rcx, pState, pInfo, pBoneToWorld);
#endif
}

#ifndef TEXTMODE
static_assert(offsetof(ModelRenderInfo_t, pRenderable) == 0x18);
static_assert(offsetof(ModelRenderInfo_t, flags) == 0x40);
static_assert(offsetof(ModelRenderInfo_t, entity_index) == 0x44);

static bool IsOtherPlayer(const ModelRenderInfo_t& tInfo)
{
	if (!tInfo.pRenderable || tInfo.entity_index <= 0 || tInfo.entity_index == I::EngineClient->GetLocalPlayer()
		|| tInfo.entity_index > I::EngineClient->GetMaxClients())
		return false;

	auto* pEntity = I::ClientEntityList->GetClientEntity(tInfo.entity_index);
	return pEntity && pEntity->GetClientRenderable() == tInfo.pRenderable && pEntity->As<CBaseEntity>()->IsPlayer();
}

MAKE_HOOK(CModelRender_SelectLod, S::CModelRender_SelectLod(), int,
	IVModelRender* rcx, const ModelRenderInfo_t& tInfo, studiohwdata_t* pHardware)
{
	DEBUG_RETURN(CModelRender_SelectLod, rcx, tInfo, pHardware);

	const int iLod = CALL_ORIGINAL(rcx, tInfo, pHardware);
	const int iBias = Vars::Visuals::UltraToaster::PlayerLODBias.Value;
	if (iBias <= 0 || !(tInfo.flags & STUDIO_RENDER)
		|| (tInfo.flags & (STUDIO_SHADOWDEPTHTEXTURE | STUDIO_SSAODEPTHTEXTURE | STUDIO_WIREFRAME))
		|| !UltraToaster::IsActive() || !IsOtherPlayer(tInfo))
		return iLod;

	return UltraToaster::LowerLod(pHardware, iLod, iBias);
}

MAKE_HOOK(IVModelRender_DrawModelSetup, U::Memory.GetVirtual(I::ModelRender, 18), bool,
	IVModelRender* rcx, ModelRenderInfo_t& tInfo, DrawModelState_t* pState, matrix3x4* pCustomBones, matrix3x4** ppBonesOut)
{
	DEBUG_RETURN(IVModelRender_DrawModelSetup, rcx, tInfo, pState, pCustomBones, ppBonesOut);

	if ((tInfo.flags & STUDIO_SHADOWDEPTHTEXTURE) && Vars::Visuals::UltraToaster::PlayerShadows.Value
		&& UltraToaster::IsActive() && IsOtherPlayer(tInfo))
	{
		*ppBonesOut = nullptr;
		return false;
	}

	return CALL_ORIGINAL(rcx, tInfo, pState, pCustomBones, ppBonesOut);
}

static bool IsCosmeticWearable(CEconWearable* pWearable)
{
	auto* pOwner = pWearable->GetOwnerEntity();
	if (!pOwner || !pOwner->IsPlayer())
		return false;

	static const int iAssociatedWeapon = U::NetVars.GetNetVar("CTFWearable", "m_hWeaponAssociatedWith");
	static const int iDisguiseWearable = U::NetVars.GetNetVar("CTFWearable", "m_bDisguiseWearable");
	const auto uWearable = reinterpret_cast<uintptr_t>(pWearable);
	if (iAssociatedWeapon <= 0 || iDisguiseWearable <= 0
		|| reinterpret_cast<const EHANDLE*>(uWearable + iAssociatedWeapon)->IsValid()
		|| *reinterpret_cast<const bool*>(uWearable + iDisguiseWearable))
		return false;

	const int iDefinition = pWearable->m_iItemDefinitionIndex();
	auto* pSchema = iDefinition > 0 ? CEconItemSchema::GetInstance() : nullptr;
	auto* pDefinition = pSchema ? pSchema->GetItemDefinition(iDefinition) : nullptr;
	const char* sItemClass = pDefinition ? pDefinition->m_pszItemClass() : nullptr;
	if (!sItemClass || std::string_view(sItemClass) != "tf_wearable")
		return false;

	auto* pRawDefinition = *reinterpret_cast<KeyValues* const*>(reinterpret_cast<uintptr_t>(pDefinition) + sizeof(void*));
	const char* sSlot = pRawDefinition ? pRawDefinition->GetString("item_slot") : nullptr;
	return sSlot && (std::string_view(sSlot) == "head" || std::string_view(sSlot) == "misc");
}

MAKE_HOOK(CBaseAnimating_InternalDrawModel, S::CBaseAnimating_InternalDrawModel(), int,
	CBaseAnimating* rcx, int flags)
{
	DEBUG_RETURN(CBaseAnimating_InternalDrawModel, rcx, flags);

	if (Vars::Visuals::UltraToaster::Enabled.Value && Vars::Visuals::UltraToaster::Cosmetics.Value
		&& rcx->GetClassID() == ETFClassID::CTFWearable && UltraToaster::IsActive()
		&& IsCosmeticWearable(static_cast<CEconWearable*>(rcx)))
		return 0;

	if (!s_bDrawingViewmodel && Vars::Visuals::UltraToaster::Enabled.Value && Vars::Visuals::UltraToaster::PlayerWeapons.Value
		&& rcx->IsBaseCombatWeapon() && UltraToaster::IsActive())
	{
		auto* pOwner = rcx->GetOwnerEntity();
		if (pOwner && pOwner->IsPlayer() && pOwner->entindex() != I::EngineClient->GetLocalPlayer())
			return 0;
	}

	if (!s_bDrawingViewmodel /*|| !(flags & STUDIO_RENDER)*/)
		return CALL_ORIGINAL(rcx, flags);

	int iReturn;
	F::Glow.RenderViewmodel(rcx, flags);
	if (F::Chams.RenderViewmodel(rcx, flags, &iReturn))
		return iReturn;

	return CALL_ORIGINAL(rcx, 1);
}


MAKE_HOOK(CBaseViewModel_DrawModel, S::CBaseViewModel_DrawModel(), int,
	void* rcx, int flags)
{
	DEBUG_RETURN(CBaseAnimating_DrawModel, rcx, flags);

	if (s_bDrawingViewmodel || I::EngineVGui->IsGameUIVisible() || SDK::CleanScreenshot()
		|| F::CameraWindow.m_bDrawing || !F::Materials.m_bLoaded || G::Unload)
		return CALL_ORIGINAL(rcx, flags);

	s_bDrawingViewmodel = true;
	int iReturn = CALL_ORIGINAL(rcx, flags);
	s_bDrawingViewmodel = false;
	return iReturn;
}
#endif
