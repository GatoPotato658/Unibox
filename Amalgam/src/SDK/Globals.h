#pragma once
#include "Definitions/Definitions.h"
#include "Definitions/Main/CUserCmd.h"
#include "../Utils/Signatures/Signatures.h"
#include "../Utils/Memory/Memory.h"

MAKE_SIGNATURE(RandomSeed, "client.dll", "0F B6 1D ? ? ? ? 89 9D", 0x0);

struct DrawLine_t
{
	std::pair<Vec3, Vec3> m_paOrigin;
	float m_flTime;
	Color_t m_tColor;
	bool m_bZBuffer = false;
};

struct DrawPath_t
{
	std::vector<Vec3> m_vPath;
	float m_flTime;
	Color_t m_tColor;
	int m_iStyle;
	bool m_bZBuffer = false;
};

struct DrawBox_t
{
	Vec3 m_vOrigin;
	Vec3 m_vMins;
	Vec3 m_vMaxs;
	Vec3 m_vAngles;
	float m_flTime;
	Color_t m_tColorEdge;
	Color_t m_tColorFace;
	bool m_bZBuffer = false;
};

struct DrawSphere_t
{
	Vec3 m_vOrigin;
	float m_flRadius;
	int m_nTheta;
	int m_nPhi;
	float m_flTime;
	Color_t m_tColorEdge;
	Color_t m_tColorFace;
	bool m_bZBuffer = false;
};

struct DrawSwept_t
{
	std::pair<Vec3, Vec3> m_paOrigin;
	Vec3 m_vMins;
	Vec3 m_vMaxs;
	Vec3 m_vAngles;
	float m_flTime;
	Color_t m_tColor;
	bool m_bZBuffer = false;
};

struct DrawTriangle_t
{
	std::array<Vec3, 3> m_aOrigin;
	float m_flTime;
	Color_t m_tColor;
	bool m_bZBuffer = false;
};

struct AimTarget_t
{
	int m_iEntIndex = 0;
	int m_iTickCount = 0;
	int m_iDuration = 32;
};

struct AimPoint_t
{
	Vec3 m_vOrigin = {};
	int m_iTickCount = 0;
	int m_iDuration = 32;
};

struct WeaponAmmoInfo_t
{
	int m_iClip = 0;
	int m_iMaxClip = 0;
	int m_iReserve = 0;
	int m_iMaxReserve = 0;
	bool m_bUsesAmmo = false;
};

Enum(TriggerType, None,
	 Hurt,
	 Ignite,
	 Push,
	 Regenerate,
	 RespawnRoom,
	 CaptureArea,
	 Catapult,
	 ApplyImpulse
);

struct BrushSurface_t
{
	Vec3 m_vCenter = {};
	std::vector<Vec3> m_vPoints = {};
};
struct model_t;
struct TriggerData_t
{
	model_t* m_pModel = nullptr;
	TriggerTypeEnum::TriggerTypeEnum m_eType = TriggerTypeEnum::None;
	Vec3 m_vOrigin = {};
	Vec3 m_vCenter = {};
	Vec3 m_vAngles = {};
	Vec3 m_vRotate = {};
	int m_iTeam = 0;

	std::vector<BrushSurface_t> m_vBrushSurfaces = {};

	bool PointIsWithin(Vec3 vPoint) const;
};

struct PasstimeMapGoalData_t
{
	Vec3 m_vOrigin = {};
	std::string m_sTargetname = {};
	int m_iTeam = 0;
	int m_iSpawnflags = 0;
	bool m_bStartDisabled = false;
};

namespace G
{
	inline bool Unload = false;
	inline bool SendPacket = false;

	inline int Attacking = 0;
	inline bool Reloading = false;
	inline bool CanPrimaryAttack = false;
	inline bool CanSecondaryAttack = false;
	inline bool CanHeadshot = false;
	inline int Throwing = false;
	inline float Lerp = 0.015f;
	inline float FOV = 90.f;
	inline float WranglerSecondFireTime = 0.f;

	inline EWeaponType PrimaryWeaponType = {}, SecondaryWeaponType = {};

	inline CUserCmd* CurrentUserCmd = nullptr;
	inline CUserCmd* LastUserCmd = nullptr;
	inline CUserCmd OriginalCmd = {};
	inline CUserCmd DummyCmd = {};

	inline AimTarget_t AimTarget = {};
	inline AimPoint_t AimPoint = {};

	inline bool SilentAngles = false;
	inline bool PSilentAngles = false;

	inline bool AntiAim = false;
	inline bool Choking = false;
	inline bool AimbotSteering = false;

	inline bool UpdatingAnims = false;
	inline bool FlipViewmodels = false;

	inline std::vector<DrawLine_t> LineStorage = {};
	inline std::vector<DrawPath_t> PathStorage = {};
	inline std::vector<DrawBox_t> BoxStorage = {};
	inline std::vector<DrawSphere_t> SphereStorage = {};
	inline std::vector<DrawSwept_t> SweptStorage = {};
	inline std::vector<DrawTriangle_t> TriangleStorage = {};
	inline std::vector<TriggerData_t> TriggerStorage = {};
	inline std::vector<PasstimeMapGoalData_t> PasstimeGoalStorage = {};

	inline int SavedDefIndexes[3] = {-1,-1,-1};
	inline int SavedWepIds[3] = {-1,-1,-1};
	inline int SavedWepSlots[3] = {-1,-1,-1};
	inline WeaponAmmoInfo_t AmmoInSlot[2] = {WeaponAmmoInfo_t(), WeaponAmmoInfo_t()};
	
	inline int& RandomSeed()
	{
		static auto& pRandomSeed = *reinterpret_cast<int*>(U::Memory.RelToAbs(S::RandomSeed()));
		return pRandomSeed;
	}
};
