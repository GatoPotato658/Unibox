#pragma once
#include "../../../SDK/SDK.h"

class CSpreadOptimizer
{
private:
	static constexpr int MaskSize = 64;
	static constexpr int MaskHalf = MaskSize / 2;
	static constexpr int MaxHitboxes = 32;

	struct KernelEntry_t
	{
		float m_flX = 0.f;
		float m_flY = 0.f;
		float m_flWeight = 0.f;
	};

	struct Rect_t
	{
		float m_flLowU = 0.f, m_flLowV = 0.f;
		float m_flHighU = 0.f, m_flHighV = 0.f;
		bool m_bValid = false;
	};

	enum class Cell_t : uint8_t
	{
		Unknown,
		Miss,
		Blocked,
		Hit
	};

	struct Score_t
	{
		float m_flHits = 0.f;
		float m_flOpenHits = 0.f;
		float m_flMeanI = 0.f;
		float m_flMeanJ = 0.f;
	};

	bool BuildKernel(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool SetupMask(CTFPlayer* pLocal, CBaseEntity* pTarget, const matrix3x4* aBones, float flModelScale, const Vec3& vHullMins, const Vec3& vHullMaxs, const Vec3& vEye);
	bool Project(const Vec3& vPoint, float& flU, float& flV) const;
	Vec3 Point(float flI, float flJ) const;
	Cell_t Ray(const Vec3& vEnd, float flU, float flV);
	Cell_t Cell(int iI, int iJ);
	Score_t Score(float flA, float flB);
	Score_t Refine(float& flA, float& flB, Score_t tBest);

	std::vector<KernelEntry_t> m_vKernel = {};
	float m_flKernelWeight = 0.f;
	float m_flSpread = 0.f;
	float m_flRange = 0.f;
	bool m_bPrior = false;

	CTFPlayer* m_pLocal = nullptr;
	CBaseEntity* m_pTarget = nullptr;
	const matrix3x4* m_aBones = nullptr;
	const mstudiohitboxset_t* m_pSet = nullptr;
	int m_iHitboxes = 0;
	float m_flModelScale = 1.f;

	Vec3 m_vEye = {};
	Vec3 m_vCentre = {};
	Vec3 m_vForward = {}, m_vRight = {}, m_vUp = {};
	Vec3 m_vHullMins = {}, m_vHullMaxs = {};
	float m_flCell = 0.f;
	float m_flDistance = 0.f;
	Rect_t m_aRects[MaxHitboxes] = {};
	Cell_t m_aCells[MaskSize * MaskSize] = {};
	int m_iTraces = 0;
	int m_iTraceLimit = 0;
	float m_flOpenHits = 0.f;

public:
	bool Applies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool Optimize(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget, const matrix3x4* aBones, float flModelScale, const Vec3& vHullMins, const Vec3& vHullMaxs, const Vec3& vEye, Vec3& vAimPos);
};

ADD_FEATURE(CSpreadOptimizer, SpreadOptimizer);
