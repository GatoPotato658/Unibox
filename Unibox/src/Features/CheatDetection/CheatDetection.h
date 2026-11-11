#pragma once
#include "../../SDK/SDK.h"

enum class EDetection : uint8_t
{
	InvalidPitch,
	PacketChoke,
	CommandOverflow,
	TickbaseShift,
	DoubleTap,
	SilentAim,
	EquipRegions,
	Count
};

struct PerfPacket_t
{
	double m_dTime = 0.0;
	int m_iNumCmds = 0;
	int m_iDropped = 0;
};

struct PerfFrame_t
{
	double m_dTime = 0.0;
	int m_iNumCmds = 0;
	int m_iTicksCorrected = 0;
	double m_dFinalSimulation = 0.0;
};

struct PerfDump_t
{
	std::vector<PerfFrame_t> m_vFrames = {};
	std::vector<PerfPacket_t> m_vPackets = {};
};

struct DetectionRecord_t
{
	uint32_t m_uHandle = INVALID_EHANDLE_INDEX;
	uint32_t m_uAccountID = 0;
	bool m_bEligible = false;
	bool m_bFlagged = false;

	float m_flScore = 0.f;
	double m_dScoreTime = 0.0;
	std::array<int, size_t(EDetection::Count)> m_aHits = {};

	bool m_bTracked = false;
	int m_iSimTick = 0;
	int m_iChangedTick = 0;
	int m_iSeenTick = 0;
	int m_iChokeStreak = 0;
	int m_iPitchStreak = 0;

	Vec3 m_vEye = {};
	Vec3 m_vPreviousEye = {};
	int m_iGap = 0;
	int m_iShots = 0;
	bool m_bShot = false;

	bool m_bProbeWanted = false;
	double m_dProbeTime = 0.0;
	double m_dPerfSeen = 0.0;

	int m_iRegionTick = 0;
	bool m_bRegionSeen = false;
};

class CCheatDetection
{
private:
	void Adopt(DetectionRecord_t& tRecord, int iIndex, uint32_t uHandle);
	void Observe(DetectionRecord_t& tRecord, int iIndex, CTFPlayer* pPlayer, int iServerTick);
	void Infract(DetectionRecord_t& tRecord, int iIndex, EDetection eDetection, float flWeight);
	void Flag(DetectionRecord_t& tRecord, int iIndex);

	void Probe();
	void ResolvePerf();
	void OnPerf(int iIndex, const PerfDump_t& tDump);

	std::array<DetectionRecord_t, MAX_PLAYERS> m_aRecords = {};
	double m_dNextProbe = 0.0;

	PerfDump_t m_tPerf = {};
	double m_dPerfSent = 0.0;
	int m_iPerfIndex = 0;
	bool m_bPerfInFlight = false;

public:
	void Run();
	void OnFireBullets(int iShooter);
	bool ParsePlayerPerf(const bf_read& msgData);
	void Reset();

	bool IsWaitingForPlayerPerf() const { return m_bPerfInFlight; }
};

ADD_FEATURE(CCheatDetection, CheatDetection);
