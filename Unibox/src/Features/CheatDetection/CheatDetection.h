#pragma once
#include "../../SDK/SDK.h"

struct AngleHistory_t
{
	Vec3 m_vAngle;
	bool m_bAttacking;
};

struct PlayerInfo
{
	uint32_t m_uAccountID = 0;
	std::string m_sName = "";

	int m_iDetections = 0;
	int m_iLastOutputTick = 0;

	struct PacketChoking_t
	{
		std::deque<int> m_vChokes = {};
		bool m_bInfract = false;
	} m_PacketChoking;

	struct AimFlicking_t
	{
		std::deque<AngleHistory_t> m_vAngles = {};
	} m_AimFlicking;

	struct LagCompAbuse_t
	{
		std::deque<int> m_vViolationTicks = {};
		bool m_bInfract = false;
	} m_LagCompAbuse;

	struct DuckSpeed_t
	{
		int m_iStartTick = 0;
	} m_DuckSpeed;

	struct CritTracker_t
	{
		struct WeaponHistory_t
		{
			std::deque<bool> m_vHistory = {};
			int m_iCrits = 0;
		};

		std::unordered_map<int, WeaponHistory_t> m_mWeaponHistory = {};
		bool m_bInfract = false;
	} m_CritTracker;
};

class CCheatDetection
{
private:
	bool ShouldScan();

	bool InvalidPitch(CTFPlayer* pEntity);
	bool IsChoking(CTFPlayer* pEntity);
	bool IsFlicking(CTFPlayer* pEntity);
	bool IsDuckSpeed(CTFPlayer* pEntity);
	bool IsLagCompAbusing(CTFPlayer* pEntity);
	bool IsCritManipulating(CTFPlayer* pEntity);
	void TrackCritEvent(CTFPlayer* pEntity, CTFWeaponBase* pWeapon, bool bCrit);

	void Infract(CTFPlayer* pEntity, const char* sReason);

	std::unordered_map<CTFPlayer*, PlayerInfo> mData = {};

public:
	void Run();

	void ReportPacket(CTFPlayer* pEntity, int iChoke, int iDelta);
	void ReportDamage(IGameEvent* pEvent);
	void Reset();
};

ADD_FEATURE(CCheatDetection, CheatDetection);
