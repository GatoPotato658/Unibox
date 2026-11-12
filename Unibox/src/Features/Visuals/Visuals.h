#pragma once
#include "../../SDK/SDK.h"
#include "../ImGui/IndicatorCache.h"

struct ImDrawList;

void RenderBacktrackModel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, int iFlags);

struct Projectile_t
{
	std::vector<Vec3> m_vPath = {};
	float m_flTime = 0.f;
	float m_flRadius = 0.f;
	Vec3 m_vNormal = { 0, 0, 1 };
	Color_t m_tColor = {};
	int m_iFlags = 0b0;
};

struct Sightline_t
{
	Vec3 m_vStart = {};
	Vec3 m_vEnd = {};
	Color_t m_tColor = {};
	bool m_bZBuffer = false;
};

struct PickupData_t
{
	int m_iType = 0;
	float m_flTime = 0.f;
	Vec3 m_vLocation = {};
};

class CVisuals
{
private:
	std::unordered_map<CBaseEntity*, Projectile_t> m_mProjectiles = {};
	std::vector<Sightline_t> m_vSightLines = {};
	std::vector<PickupData_t> m_vPickups = {};
	std::vector<Vec3> m_vAngles = {};
	std::vector<KeyValues*> m_v_world_texture_key_values = {};
	Vec3 m_vPrevAimAngles = {};

	struct AimPos_t
	{
		bool m_bValid = false;
		float m_flX = 0.f, m_flY = 0.f;
	};
	CIndicatorCache<AimPos_t> m_tAimPosCache = {};

public:
	void Event(IGameEvent* pEvent, uint32_t uHash);
	void Store();
	void Tick();

	void ProjectileTrace(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon, const bool bInterp = true);
	void DrawPickupTimers();
	void Triggers(CTFPlayer* pLocal);

	std::vector<DrawBox_t> GetHitboxes(matrix3x4* aBones, CBaseAnimating* pEntity, std::vector<int> vHitboxes = {}, int iTarget = -1);
	void DrawEffects();
	void DrawHitboxes(int iStore = 0);
	void CacheBestAimPos(CTFPlayer* pLocal);
	void DrawBestAimPos(ImDrawList* pDrawList);

	void FOV(CTFPlayer* pLocal, CViewSetup* pView);
	void ThirdPerson(CTFPlayer* pLocal, CViewSetup* pView);

	void OverrideWorldTextures();
	void Modulate();
	void RestoreWorldModulation();

	void CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void LocalAnimations(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Precipitation();
};

ADD_FEATURE(CVisuals, Visuals);
