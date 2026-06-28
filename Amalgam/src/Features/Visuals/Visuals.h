#pragma once
#include "../../SDK/SDK.h"

//#define DEBUG_UNI
#ifdef DEBUG_UNI
#include "Images.h"
#endif

#include <map>

//#define DEBUG_TEXT

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

#ifdef DEBUG_TEXT
struct DebugText_t
{
	std::string m_sText = "";
	Color_t m_tColor = {};
	std::optional<Vec2> vPosition2D = std::nullopt;
	std::optional<Vec3> vPosition3D = std::nullopt;
};
#endif

class CVisuals
{
private:
	std::unordered_map<CBaseEntity*, Projectile_t> m_mProjectiles = {};
	std::vector<Sightline_t> m_vSightLines = {};
	std::vector<PickupData_t> m_vPickups = {};
	std::vector<Vec3> m_vAngles = {};
	std::vector<KeyValues*> m_v_world_texture_key_values = {};

#ifdef DEBUG_TEXT
	std::vector<DebugText_t> m_vDebugText = {};
#endif

#ifdef DEBUG_UNI
	struct ImageArrayInfo_t
	{
		int m_iTextureID = 0;
		int m_iHeight = 0;
		int m_iWidth = 0;
		const unsigned char* m_pArray = nullptr;
	};
	std::array<ImageArrayInfo_t, 6> m_aUniTextures = {
		ImageArrayInfo_t(0, uni_image_0_h, uni_image_0_w, uni_image_0_arr),
		ImageArrayInfo_t(0, uni_image_1_h, uni_image_1_w, uni_image_1_arr),
		ImageArrayInfo_t(0, uni_image_2_h, uni_image_2_w, uni_image_2_arr),
		ImageArrayInfo_t(0, uni_image_3_h, uni_image_3_w, uni_image_3_arr),
		ImageArrayInfo_t(0, uni_image_4_h, uni_image_4_w, uni_image_4_arr),
		ImageArrayInfo_t(0, uni_image_5_h, uni_image_5_w, uni_image_5_arr)
	};
	ImageArrayInfo_t* m_pCurrentUniTexture = nullptr;
#endif
public:
	void Event(IGameEvent* pEvent, uint32_t uHash);
	void Store();
	void Tick();

	void ProjectileTrace(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon, const bool bInterp = true);
	void DrawPickupTimers();
	void Triggers(CTFPlayer* pLocal);

	void DrawDebugInfo(CTFPlayer* pLocal);

#ifdef DEBUG_TEXT
	void AddDebugText(const DebugText_t& sText);
	void AddDebugText(const std::string& sString, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddDebugText(const std::string& sString, const Vec2& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddDebugText(const std::string& sString, const Vec3& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void ClearDebugText();
#endif

	std::vector<DrawBox_t> GetHitboxes(matrix3x4* aBones, CBaseAnimating* pEntity, std::vector<int> vHitboxes = {}, int iTarget = -1);
	void DrawEffects();
	void DrawHitboxes(int iStore = 0);

	void FOV(CTFPlayer* pLocal, CViewSetup* pView);
	void ThirdPerson(CTFPlayer* pLocal, CViewSetup* pView);

	void OverrideWorldTextures();
	void Modulate();
	void RestoreWorldModulation();

	void CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	
	void LocalAnimations(CTFPlayer* pLocal, CUserCmd* pCmd);
	
#ifdef DEBUG_UNI
	void DrawUni();
	void RemoveUni();
	bool m_bUniDraw = true;
#endif
};

ADD_FEATURE(CVisuals, Visuals);
