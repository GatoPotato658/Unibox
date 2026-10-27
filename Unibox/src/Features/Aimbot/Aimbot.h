#pragma once
#include "../../SDK/SDK.h"
#include "../ImGui/IndicatorCache.h"

struct ImDrawList;

struct RealPath_t
{
	DrawPath_t m_tPath = {};
	size_t m_iSize = 0;
};

class CAimbot
{
private:
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void RunAimbot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSecondaryType = false);

	struct FovCircle_t
	{
		bool m_bValid = false;
		float m_flRadius = 0.f;
		float m_flX = 0.f, m_flY = 0.f;
		Color_t m_tColor = {};
	};
	CIndicatorCache<FovCircle_t> m_tDrawCache = {};

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw(ImDrawList* pDrawList);
	void Store(CBaseEntity* pEntity, size_t iSize);
	void Store(bool bFrameStageNotify = true);
	float GetSmoothStrength(const Vec3& vCurAngle, const Vec3& vToAngle) const;

	EWeaponType m_eRanType = EWeaponType::UNKNOWN;
	bool m_bRunningSecondary = false;

	std::unordered_map<int, RealPath_t> m_mRealPaths = {};
};

ADD_FEATURE(CAimbot, Aimbot);