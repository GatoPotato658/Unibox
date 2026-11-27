#pragma once
#include "../../../SDK/SDK.h"

// shit annoyed me so hard like it kept shooting the cart on frontier @rei-kes please learn

struct CartState_t
{
	CBaseEntity* m_pEntity = nullptr;
	Vec3 m_vOrigin = {};
	Vec3 m_vAngles = {};
	Vec3 m_vVelocity = {};
	Vec3 m_vAngVelocity = {};
	float m_flRadius = 0.f;
};

class CCartSim
{
public:
	void Store();

	const std::vector<CartState_t>& GetCarts() const { return m_vCarts; }
	const CartState_t* GetCart(const CBaseEntity* pEntity) const;
	bool HasMovingCarts() const { return m_bHasMoving; }
	bool IsMovingCart(const CBaseEntity* pEntity) const;

	void PoseCarts(float flTime);
	void PoseCartsNear(float flTime, const Vec3& vOrigin, float flDist);
	void RestoreCarts();

private:
	struct Sample_t { float m_flTime; Vec3 m_vOrigin; Vec3 m_vAngles; };
	std::unordered_map<int, std::deque<Sample_t>> m_mHistory = {};
	std::vector<CartState_t> m_vCarts = {};
	bool m_bHasMoving = false;
	int m_iPosedCount = 0;
};

ADD_FEATURE(CCartSim, CartSim);

struct CartPoseGuard_t
{
	CartPoseGuard_t() = default;
	~CartPoseGuard_t() { F::CartSim.RestoreCarts(); }
	CartPoseGuard_t(const CartPoseGuard_t&) = delete;
	CartPoseGuard_t& operator=(const CartPoseGuard_t&) = delete;
};
