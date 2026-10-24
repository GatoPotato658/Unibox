#pragma once
#include "../../../SDK/SDK.h"

#include <array>
#include <vector>

Enum(ProjZone, Root, TargetSelect, CanHit, MoveInit, MoveStrafe, MoveTick, MoveRestore, ProjGetInfo, ProjInit, ProjTick, TickLoop, CalcAngle, SplashSetup, SplashPoints, HandleDirect, HandleSplash, HandlePoint, TestAngle, Trace, TraceHull, Count)
Enum(ProjCounter, CanHits, MoveTicks, MoveTicksCached, ProjTicksSimple, ProjTicksPhysics, CalcAngles, CalcAnglesTwoPass, TestAngles, TestAnglesHit, SecondTests, HandlePoints, DirectPoints, SplashHistory, SplashSetupPoints, SplashPointsGenerated, Count)

struct ProjZone_t
{
	int64_t m_iTotal = 0;
	int64_t m_iSelf = 0;
	int64_t m_iMax = 0;
	int m_iCount = 0;
};
using ProjZones_t = std::array<ProjZone_t, ProjZoneEnum::Count>;
using ProjCounters_t = std::array<int64_t, ProjCounterEnum::Count>;

struct ProjTarget_t
{
	int m_iEntity = 0;
	int m_iType = 0;
	int m_iHitbox = -1;
	float m_flDist = 0.f;
	int m_iResult = 0;
	const char* m_sNote = "";

	float m_flMaxSimTime = 0.f;
	int m_iMaxTick = 0;
	int m_iLastTick = 0;
	int m_iDirects = 0;
	int m_iSplashModes = 0;
	int m_iDirectHistory = 0;
	int m_iSplashHistory = 0;
	bool m_bMoveFailed = false;
	bool m_bMoveCached = false;

	int64_t m_iDuration = 0;
	ProjZones_t m_aZones = {};
	ProjCounters_t m_aCounters = {};
};

struct ProjFrame_t
{
	const char* m_sName = "";
	int m_iFrameCount = 0;
	int m_iTick = 0;
	int m_iWeaponID = -1;
	float m_flFrameTime = 0.f;
	int64_t m_iStart = 0;
	int64_t m_iEnd = 0;
	bool m_bPhysics = false;
	bool m_bFastPhysics = false;
	bool m_bSpecial = false;

	ProjZones_t m_aZones = {};
	ProjZones_t m_aPre = {};
	ProjCounters_t m_aCounters = {};
	std::vector<ProjTarget_t> m_vTargets = {};
	int m_iTargetsDropped = 0;

	void Reset()
	{
		m_sName = "", m_iFrameCount = 0, m_iTick = 0, m_iWeaponID = -1, m_flFrameTime = 0.f;
		m_iStart = m_iEnd = 0;
		m_bPhysics = m_bFastPhysics = m_bSpecial = false;
		m_aZones = {}, m_aPre = {}, m_aCounters = {};
		m_vTargets.clear();
		m_iTargetsDropped = 0;
	}
};

#ifdef PROJ_PROFILER
class ProjScope_t;

class CProjProfiler
{
private:
	static constexpr int RingSize = 24;
	static constexpr int FrameTimeSize = 128;
	static constexpr int MaxTargets = 12;

	bool Enter(ProjScope_t* pScope, int iZone, bool bRootOnly);
	void Leave(ProjScope_t* pScope);

	void Dump(const char* sReason, float flFrameTime, int iWindowFrame);

	std::array<ProjFrame_t, RingSize> m_aRing = {};
	int m_iHead = 0;
	int m_iStored = 0;
	ProjFrame_t* m_pFrame = nullptr;
	unsigned long m_dwThread = 0;
	ProjScope_t* m_pScope = nullptr;
	ProjZones_t m_aAmbient = {};

	std::array<float, FrameTimeSize> m_aFrameTimes = {};
	int m_iFrameTimeHead = 0;
	int m_iFrameTimeCount = 0;
	int m_iLastFrameCount = -1;

	int64_t m_iLastDump = 0;
	int m_iDumps = 0;
	int m_iSuppressed = 0;
	int m_iQuiet = 0;
	int64_t m_iFirstStamp = 0;

	friend class ProjScope_t;
	friend class ProjTargetScope_t;

public:
	static int64_t Now();
	static double ToMs(int64_t iTicks);

	bool Enabled() const { return Vars::Aimbot::Projectile::LagProfile.Value; }
	bool Active() const { return m_pFrame != nullptr; }

	void Update();
	bool Begin(const char* sName, int iWeaponID);
	void End();

	inline void Count(int iCounter, int64_t iAmount = 1)
	{
		if (m_pFrame && GetCurrentThreadId() == m_dwThread)
			m_pFrame->m_aCounters[iCounter] += iAmount;
	}
};

ADD_FEATURE(CProjProfiler, ProjProfiler);

class ProjScope_t
{
public:
	enum { Ambient, RootOnly, Never };

	inline ProjScope_t(int iZone, int iMode = Ambient)
	{
		if (iMode != Never && F::ProjProfiler.Enabled())
			F::ProjProfiler.Enter(this, iZone, iMode == RootOnly);
	}
	inline ~ProjScope_t() { Close(); }
	inline void Close()
	{
		if (m_iStart)
			F::ProjProfiler.Leave(this);
	}

private:
	int m_iZone = 0;
	int64_t m_iStart = 0;
	int64_t m_iChild = 0;
	ProjScope_t* m_pParent = nullptr;

	friend class CProjProfiler;
};

class ProjFrameScope_t
{
public:
	inline ProjFrameScope_t(const char* sName, int iWeaponID = -1)
		: m_bOwner(F::ProjProfiler.Begin(sName, iWeaponID)), m_tRoot(ProjZoneEnum::Root, m_bOwner ? ProjScope_t::Ambient : ProjScope_t::Never) {}
	inline ~ProjFrameScope_t()
	{
		if (!m_bOwner)
			return;
		m_tRoot.Close();
		F::ProjProfiler.End();
	}

private:
	bool m_bOwner;
	ProjScope_t m_tRoot;
};

class ProjTargetScope_t
{
public:
	ProjTargetScope_t(int iEntity, int iType, int iHitbox, float flDist);
	~ProjTargetScope_t();

	int Result(int iResult, const char* sNote);
	void SetSim(float flMaxSimTime, int iMaxTick, bool bMoveFailed, bool bMoveCached);
	void SetLoop(int iLastTick, int iDirects, int iSplashModes);
	void SetHistory(int iDirectHistory, int iSplashHistory);

private:
	ProjTarget_t* Get();

	bool m_bValid = false;
	size_t m_iIndex = 0;
	int64_t m_iStart = 0;
	ProjZones_t m_aZoneBase = {};
	ProjCounters_t m_aCounterBase = {};
};

#define PROJ_CAT_(a, b) a##b
#define PROJ_CAT(a, b) PROJ_CAT_(a, b)
#define PROJ_ZONE(zone) ProjScope_t PROJ_CAT(tProjZone, __COUNTER__)(ProjZoneEnum::zone)
#define PROJ_ZONE_ROOT(zone) ProjScope_t PROJ_CAT(tProjZone, __COUNTER__)(ProjZoneEnum::zone, ProjScope_t::RootOnly)
#define PROJ_COUNT(counter) F::ProjProfiler.Count(ProjCounterEnum::counter)
#define PROJ_COUNT_N(counter, amount) F::ProjProfiler.Count(ProjCounterEnum::counter, amount)
#define PROJ_FRAME(name, weapon) ProjFrameScope_t PROJ_CAT(tProjFrame, __COUNTER__)(name, weapon)
#else
class CProjProfiler
{
public:
	bool Enabled() const { return false; }
	bool Active() const { return false; }
	void Update() {}
};

ADD_FEATURE(CProjProfiler, ProjProfiler);

class ProjScope_t
{
public:
	enum { Ambient, RootOnly, Never };

	ProjScope_t(int, int = Ambient) {}
	void Close() {}
};

class ProjTargetScope_t
{
public:
	ProjTargetScope_t(int, int, int, float) {}
	int Result(int iResult, const char*) { return iResult; }
	void SetSim(float, int, bool, bool) {}
	void SetLoop(int, int, int) {}
	void SetHistory(int, int) {}
};

#define PROJ_ZONE(zone)
#define PROJ_ZONE_ROOT(zone)
#define PROJ_COUNT(counter)
#define PROJ_COUNT_N(counter, amount)
#define PROJ_FRAME(name, weapon)
#endif
