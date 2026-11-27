#pragma once
#include "../../SDK/SDK.h"
#include "ResolverCore.h"

struct PendingShot_t
{
	int m_iUserID = -1;
	int m_iEntIndex = -1;
	float m_flFireTime = 0.f;
	float m_flExpireTime = 0.f;
	int m_nHitbox = HITBOX_MAX;
	bool m_bHeadshotWeapon = false;
	int m_iYawHyp = 0;
	int m_iPitchHyp = 0;
	Vec3 m_vShootPos = {};
	Vec3 m_vTargetPos = {};
	bool m_bImpactSeen = false;
	bool m_bImpactOccluded = false;
	bool m_bActive = false;
};

class CResolver
{
private:
	static constexpr int kMaxPlayers = 65;
	static constexpr int kMaxPendingShots = 16;

	ResolverTrack_t* GetTrackForEntity(int iEntIndex, int iUserID);
	ResolverTrack_t* FindTrackByUserID(int iUserID);
	void StoreSniperDots(CTFPlayerResource* pResource);
	std::optional<float> GetPitchForSniperDot(CTFPlayer* pEntity, int iEntIndex);

	std::array<ResolverTrack_t, kMaxPlayers> m_aTracks = {};
	std::array<Vec3, kMaxPlayers> m_aSniperDots = {};
	std::array<bool, kMaxPlayers> m_aHasSniperDot = {};
	std::array<PendingShot_t, kMaxPendingShots> m_aPendingShots = {};
	int m_iPendingHead = 0;

	float m_flLastYawCycle = 0.f;
	float m_flLastPitchCycle = 0.f;
	float m_flLastMinwalkCycle = 0.f;
	float m_flLastViewCycle = 0.f;

public:
	void FrameStageNotify();
	void CreateMove();
	void HitscanRan(CTFPlayer* pLocal, CTFPlayer* pTarget, CTFWeaponBase* pWeapon, int nHitbox = HITBOX_MAX, const Vec3& vShootPos = {}, const Vec3& vTargetPos = {});
	void BulletImpact(IGameEvent* pEvent);
	void PlayerHurt(IGameEvent* pEvent);
	void SetYaw(int iUserID, float flValue, bool bAuto = false);
	void SetPitch(int iUserID, float flValue, bool bInverse = false, bool bAuto = false);
	void SetMinwalk(int iUserID, bool bValue);
	void SetView(int iUserID, bool bValue);
	bool GetAngles(CTFPlayer* pPlayer, float* pYaw = nullptr, float* pPitch = nullptr, bool* pMinwalk = nullptr, bool bFake = false);
	void Reset();
};

ADD_FEATURE(CResolver, Resolver);
