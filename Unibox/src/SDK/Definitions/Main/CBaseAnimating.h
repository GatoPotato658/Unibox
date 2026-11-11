#pragma once
#include "CBaseEntity.h"

class CStudioHdr;
class IMDLCache;

MAKE_SIGNATURE(CBaseAnimating_GetModelPtr, "client.dll", "48 89 7C 24 ? 55 48 8B EC 48 83 EC 70 80 B9", 0x0);
MAKE_SIGNATURE(CStudioHdr_Constructor, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 33 ED 48 8B F1 48 89 69 10", 0x0);
MAKE_SIGNATURE(BoneSetup_InitPose, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 4C 8B 19 45 33 D2 41 8B F1 49 8B F8", 0x0);
MAKE_SIGNATURE(BoneSetup_AccumulatePose, "client.dll", "44 89 4C 24 ? 55 53 57 41 54 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8", 0x0);
MAKE_SIGNATURE(BoneSetup_CalcAutoplaySequences, "client.dll", "40 56 57 41 54 41 55 48 83 EC ? 48 8B B4 24 ? ? ? ? 4D 8B E0 4C 89", 0x0);
MAKE_SIGNATURE(BoneSetup_CalcBoneAdj, "client.dll", "4C 8B DC 4D 89 43 ? 55 57 41 55 41 56 41 57 49 8D 6B ? 48 81 EC ? ? ? ? 48 8B 01 33 FF", 0x0);
MAKE_SIGNATURE(CStudioHdr_SequencesAvailable, "client.dll", "40 53 48 83 EC 20 48 8B D9 48 8B 09 83 B9 50 01 00 00 00 74 23", 0x0);
MAKE_SIGNATURE(CStudioHdr_LookupSequence, "client.dll", "48 89 5C 24 ? 55 48 83 EC 20 48 8B EA 48 8B D9 48 85 C9", 0x0);
MAKE_SIGNATURE(CStudioHdr_pSeqdesc, "client.dll", "48 89 74 24 10 57 48 83 EC 20 33 F6 48 8D 79 08 44 8B CA 85 D2 78 1C", 0x0);
MAKE_SIGNATURE(CStudioHdr_GetNumSeq, "client.dll", "48 8B 41 08 48 85 C0 75 0A 48 8B 01 8B 80 BC 00 00 00 C3 8B 40 20", 0x0);

struct BoneSetupState
{
	const CStudioHdr* m_pStudio;
	int m_iBoneMask;
	const float* m_pPoseParameters;
	void* m_pPoseDebugger;
};

struct AimAnimationLayer
{
	int m_nSequence;
	float m_flPrevCycle;
	float m_flWeight;
	int m_nOrder;
	float m_flPlaybackRate;
	float m_flCycle;
	float m_flLayerAnimtime;
	float m_flLayerFadeOuttime;
	float m_flBlendIn;
	float m_flBlendOut;
	bool m_bClientBlend;
};

struct AimAnimationOverlay
{
	AimAnimationLayer* m_pLayers;
	int m_nAllocationCount;
	int m_nGrowSize;
	int m_nCount;

	inline int Count() const
	{
		return m_pLayers ? m_nCount : 0;
	}

	inline const AimAnimationLayer& operator[](int iIndex) const
	{
		return m_pLayers[iIndex];
	}
};

static_assert(sizeof(BoneSetupState) == 32);
static_assert(sizeof(AimAnimationLayer) == 44);

MAKE_SIGNATURE(CBaseAnimating_FrameAdvance, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 57 48 81 EC ? ? ? ? 44 0F 29 54 24", 0x0);
MAKE_SIGNATURE(CBaseAnimating_GetBonePosition, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 8B DA 49 8B F1", 0x0);
MAKE_SIGNATURE(CBaseAnimating_SequenceDuration, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 80 B9 ? ? ? ? ? 48 8B D9 8B B9", 0x0);

class CBaseAnimating : public CBaseEntity
{
public:
	NETVAR(m_nSequence, int, "CBaseAnimating", "m_nSequence");
	NETVAR(m_nForceBone, int, "CBaseAnimating", "m_nForceBone");
	NETVAR(m_vecForce, Vec3, "CBaseAnimating", "m_vecForce");
	NETVAR(m_nSkin, int, "CBaseAnimating", "m_nSkin");
	NETVAR(m_nBody, int, "CBaseAnimating", "m_nBody");
	NETVAR(m_nHitboxSet, int, "CBaseAnimating", "m_nHitboxSet");
	NETVAR(m_flModelScale, float, "CBaseAnimating", "m_flModelScale");
	NETVAR(m_flModelWidthScale, float, "CBaseAnimating", "m_flModelWidthScale");
	NETVAR(m_flPlaybackRate, float, "CBaseAnimating", "m_flPlaybackRate");
	NETVAR(m_flEncodedController, void*, "CBaseAnimating", "m_flEncodedController");
	NETVAR(m_bClientSideAnimation, bool, "CBaseAnimating", "m_bClientSideAnimation");
	NETVAR(m_bClientSideFrameReset, bool, "CBaseAnimating", "m_bClientSideFrameReset");
	NETVAR(m_nNewSequenceParity, int, "CBaseAnimating", "m_nNewSequenceParity");
	NETVAR(m_nResetEventsParity, int, "CBaseAnimating", "m_nResetEventsParity");
	NETVAR(m_nMuzzleFlashParity, int, "CBaseAnimating", "m_nMuzzleFlashParity");
	NETVAR(m_hLightingOrigin, EHANDLE, "CBaseAnimating", "m_hLightingOrigin");
	NETVAR(m_hLightingOriginRelative, EHANDLE, "CBaseAnimating", "m_hLightingOriginRelative");
	NETVAR(m_flCycle, float, "CBaseAnimating", "m_flCycle");
	NETVAR(m_fadeMinDist, float, "CBaseAnimating", "m_fadeMinDist");
	NETVAR(m_fadeMaxDist, float, "CBaseAnimating", "m_fadeMaxDist");
	NETVAR(m_flFadeScale, float, "CBaseAnimating", "m_flFadeScale");
	inline std::array<float, 24>& m_flPoseParameter()
	{
		static int nOffset = U::NetVars.GetNetVar("CBaseAnimating", "m_flPoseParameter");
		return *reinterpret_cast<std::array<float, 24>*>(uintptr_t(this) + nOffset);
	}

	NETVAR_OFF(m_pStudioHdr, CStudioHdr*, "CBaseAnimating", "m_nMuzzleFlashParity", 16);
	inline AimAnimationOverlay& m_AnimOverlay()
	{
		return *reinterpret_cast<AimAnimationOverlay*>(uintptr_t(this) + 3096);
	}
	NETVAR_OFF(m_CachedBoneData, CUtlVector<matrix3x4>, "CBaseAnimating", "m_hLightingOrigin", -88);
	NETVAR_OFF(m_bSequenceLoops, bool, "CBaseAnimating", "m_flFadeScale", 13);

	VIRTUAL_ARGS(GetAttachment, bool, 71, (int number, Vec3& origin), this, number, std::ref(origin))
	VIRTUAL_ARGS(FireEvent, void, 175, (const Vector& origin, const QAngle& angles, int event, const char* options), this, std::ref(origin), std::ref(angles), event, options);

	SIGNATURE_ARGS(FrameAdvance, float, CBaseAnimating, (float flInterval), this, flInterval);
	SIGNATURE_ARGS(GetBonePosition, float, CBaseAnimating, (int iBone, Vector& origin, QAngle& angles), this, iBone, std::ref(origin), std::ref(angles));
	SIGNATURE(SequenceDuration, float, CBaseAnimating, this);
	inline float SequenceDuration(int iSequence)
	{
		int iOriginalSequence = m_nSequence();
		m_nSequence() = iSequence;
		bool bReturn = S::CBaseAnimating_SequenceDuration.Call<float>(this);
		m_nSequence() = iOriginalSequence;
		return bReturn;
	}

	CStudioHdr* GetModelPtr();
	CStudioHdr* GetAimStudio();
	mstudiohitboxset_t* GetHitboxSet();
	mstudiohitboxset_t* GetAimHitboxSet();
	bool SetupAimBones(matrix3x4* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime);
	int GetHitboxGroup(int nHitbox);
	int GetNumOfHitboxes();
	Vec3 GetHitboxOrigin(matrix3x4* aBones, int nHitbox, Vec3 vOffset = {});
	Vec3 GetHitboxCenter(matrix3x4* aBones, int nHitbox, Vec3 vOffset = {});
	void GetHitboxInfo(matrix3x4* aBones, int nHitbox, Vec3* pCenter = nullptr, Vec3* pMins = nullptr, Vec3* pMaxs = nullptr, matrix3x4* pMatrix = nullptr, Vec3 vOffset = {});
};

using InternalDrawModelFn = int(__fastcall*)(CBaseAnimating*, int);

class CBaseAnimatingOverlay : public CBaseAnimating
{
public:

};

class CCurrencyPack : public CBaseAnimating
{
public:
	NETVAR(m_bDistributed, bool, "CCurrencyPack", "m_bDistributed");
};

class CHalloweenPickup : public CBaseAnimating
{
public:
};

class CHalloweenGiftPickup : public CHalloweenPickup
{
public:
	NETVAR(m_hTargetPlayer, EHANDLE, "CHalloweenGiftPickup", "m_hTargetPlayer");
};
