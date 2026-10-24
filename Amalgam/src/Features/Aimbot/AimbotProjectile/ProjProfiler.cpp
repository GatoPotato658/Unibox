#ifdef PROJ_PROFILER
#include "ProjProfiler.h"

#include "../AimbotGlobal/AimbotGlobal.h"
#include "../../Configs/Configs.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

static constexpr const char* s_aZoneNames[ProjZoneEnum::Count] = {
	"root (code outside any zone)",
	"ManageTargets",
	"CanHit body (setup/glue/restore)",
	"MoveSim::Initialize",
	"MoveSim::StrafePrediction",
	"MoveSim::RunTick (simulated)",
	"MoveSim::Restore",
	"ProjSim::GetInfo",
	"ProjSim::Initialize",
	"ProjSim::RunTick (physics only)",
	"CanHit tick loop body",
	"CalculateAngle",
	"SetupSplashPoints",
	"GetSplashPoints",
	"HandleDirect",
	"HandleSplash",
	"HandlePoint",
	"TestAngle (tick loop + glue)",
	"SDK::Trace",
	"SDK::TraceHull",
};

static constexpr const char* s_aCounterNames[ProjCounterEnum::Count] = {
	"canhit",
	"movesim_ticks",
	"movesim_ticks_cached",
	"projsim_ticks_simple",
	"projsim_ticks_physics",
	"calc_angle",
	"calc_angle_2pass",
	"test_angle",
	"test_angle_hit",
	"second_test",
	"handle_point",
	"direct_points",
	"splash_history",
	"splash_setup_points",
	"splash_points_gen",
};

static inline double GetFrequency()
{
	static const double flFrequency = []()
		{
			LARGE_INTEGER tFrequency = {};
			QueryPerformanceFrequency(&tFrequency);
			return double(tFrequency.QuadPart);
		}();
	return flFrequency;
}

int64_t CProjProfiler::Now()
{
	LARGE_INTEGER tCounter = {};
	QueryPerformanceCounter(&tCounter);
	return tCounter.QuadPart;
}

double CProjProfiler::ToMs(int64_t iTicks)
{
	return double(iTicks) * 1000.0 / GetFrequency();
}

static inline const char* WeaponName(int iWeaponID)
{
	switch (iWeaponID)
	{
	case TF_WEAPON_ROCKETLAUNCHER: return "rocketlauncher";
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT: return "directhit";
	case TF_WEAPON_PARTICLE_CANNON: return "cowmangler";
	case TF_WEAPON_RAYGUN: return "bison";
	case TF_WEAPON_DRG_POMSON: return "pomson";
	case TF_WEAPON_FLAREGUN: return "flaregun";
	case TF_WEAPON_FLAREGUN_REVENGE: return "manmelter";
	case TF_WEAPON_COMPOUND_BOW: return "huntsman";
	case TF_WEAPON_CROSSBOW: return "crossbow";
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE: return "rescue ranger";
	case TF_WEAPON_SYRINGEGUN_MEDIC: return "syringegun";
	case TF_WEAPON_PIPEBOMBLAUNCHER: return "stickybomb";
	case TF_WEAPON_GRENADELAUNCHER: return "grenadelauncher";
	case TF_WEAPON_CANNON: return "loosecannon";
	case TF_WEAPON_LASER_POINTER: return "wrangler";
	case TF_WEAPON_MECHANICAL_ARM: return "short circuit";
	case TF_WEAPON_GRAPPLINGHOOK: return "grappling hook";
	case TF_WEAPON_PASSTIME_GUN: return "jack";
	case TF_WEAPON_FLAMETHROWER: return "flamethrower";
	case TF_WEAPON_BAT_WOOD: return "sandman";
	case TF_WEAPON_BAT_GIFTWRAP: return "wrap assassin";
	case TF_WEAPON_LUNCHBOX: return "lunchbox";
	case TF_WEAPON_CLEAVER: return "cleaver";
	case TF_WEAPON_JAR: return "jarate";
	case TF_WEAPON_JAR_MILK: return "mad milk";
	case TF_WEAPON_JAR_GAS: return "gas passer";
	case -1: return "n/a";
	}
	return "other";
}

static inline const char* TargetTypeName(int iType)
{
	switch (iType)
	{
	case TargetEnum::Player: return "player";
	case TargetEnum::Sentry: return "sentry";
	case TargetEnum::Dispenser: return "dispenser";
	case TargetEnum::Teleporter: return "teleporter";
	case TargetEnum::Sticky: return "sticky";
	case TargetEnum::NPC: return "npc";
	case TargetEnum::Bomb: return "bomb";
	}
	return "unknown";
}

static inline const char* ResultName(int iResult)
{
	switch (iResult)
	{
	case 0: return "miss";
	case 1: return "hit";
	case 2: return "assist-hit";
	}
	return "n/a";
}

static inline int64_t SumSelf(const ProjZones_t& aZones)
{
	int64_t iSum = 0;
	for (auto& tZone : aZones)
		iSum += tZone.m_iSelf;
	return iSum;
}

static inline int64_t Traces(const ProjZones_t& aZones)
{
	return int64_t(aZones[ProjZoneEnum::Trace].m_iCount) + aZones[ProjZoneEnum::TraceHull].m_iCount;
}

static std::string FormatZones(const ProjZones_t& aZones, size_t iMax)
{
	std::vector<int> vOrder;
	for (int i = 0; i < ProjZoneEnum::Count; i++)
	{
		if (aZones[i].m_iCount)
			vOrder.push_back(i);
	}
	std::sort(vOrder.begin(), vOrder.end(), [&](int a, int b) { return aZones[a].m_iSelf > aZones[b].m_iSelf; });

	std::string sOut;
	for (size_t i = 0; i < vOrder.size() && i < iMax; i++)
	{
		const auto& tZone = aZones[vOrder[i]];
		sOut += std::format("{}{}={:.3f}ms({}x)", i ? ", " : "", s_aZoneNames[vOrder[i]], CProjProfiler::ToMs(tZone.m_iSelf), tZone.m_iCount);
	}
	return sOut.empty() ? "none" : sOut;
}

static std::string FormatCounters(const ProjCounters_t& aCounters)
{
	std::string sOut;
	for (int i = 0; i < ProjCounterEnum::Count; i++)
	{
		if (aCounters[i])
			sOut += std::format("{}{}={}", sOut.empty() ? "" : " ", s_aCounterNames[i], aCounters[i]);
	}
	return sOut.empty() ? "none" : sOut;
}

static void AppendZoneTable(std::string& sOut, const ProjZones_t& aZones)
{
	std::vector<int> vOrder;
	for (int i = 0; i < ProjZoneEnum::Count; i++)
	{
		if (aZones[i].m_iCount)
			vOrder.push_back(i);
	}
	std::sort(vOrder.begin(), vOrder.end(), [&](int a, int b) { return aZones[a].m_iSelf > aZones[b].m_iSelf; });

	sOut += std::format("    {:<34}{:>8}{:>11}{:>11}{:>11}{:>10}\n", "zone", "calls", "self_ms", "incl_ms", "max_ms", "avg_us");
	for (int i : vOrder)
	{
		const auto& tZone = aZones[i];
		sOut += std::format("    {:<34}{:>8}{:>11.3f}{:>11.3f}{:>11.3f}{:>10.1f}\n",
			s_aZoneNames[i], tZone.m_iCount, CProjProfiler::ToMs(tZone.m_iSelf), CProjProfiler::ToMs(tZone.m_iTotal), CProjProfiler::ToMs(tZone.m_iMax),
			CProjProfiler::ToMs(tZone.m_iTotal) * 1000.0 / tZone.m_iCount);
	}
}

bool CProjProfiler::Enter(ProjScope_t* pScope, int iZone, bool bRootOnly)
{
	if (bRootOnly && !m_pFrame)
		return false;
	if (GetCurrentThreadId() != m_dwThread)
		return false;

	pScope->m_iZone = iZone;
	pScope->m_iChild = 0;
	pScope->m_pParent = m_pScope;
	m_pScope = pScope;
	pScope->m_iStart = Now();
	return true;
}

void CProjProfiler::Leave(ProjScope_t* pScope)
{
	const int64_t iElapsed = Now() - pScope->m_iStart;
	const int64_t iSelf = iElapsed - pScope->m_iChild;
	pScope->m_iStart = 0;

	m_pScope = pScope->m_pParent;
	if (m_pScope)
		m_pScope->m_iChild += iElapsed;

	auto& tZone = (m_pFrame ? m_pFrame->m_aZones : m_aAmbient)[pScope->m_iZone];
	tZone.m_iTotal += iElapsed;
	tZone.m_iSelf += iSelf;
	tZone.m_iMax = std::max(tZone.m_iMax, iElapsed);
	tZone.m_iCount++;
}

void CProjProfiler::Update()
{
	if (!Enabled() || !I::GlobalVars)
		return;

	m_dwThread = GetCurrentThreadId();

	const int iFrameCount = I::GlobalVars->framecount;
	if (iFrameCount == m_iLastFrameCount)
		return;
	m_iLastFrameCount = iFrameCount;

	const float flFrameTime = I::GlobalVars->absoluteframetime;
	m_aFrameTimes[m_iFrameTimeHead] = flFrameTime;
	m_iFrameTimeHead = (m_iFrameTimeHead + 1) % FrameTimeSize;
	m_iFrameTimeCount = std::min(m_iFrameTimeCount + 1, int(FrameTimeSize));

	if (flFrameTime * Vars::Aimbot::Projectile::LagProfileFPS.Value > 1.f)
		Dump("FRAME", flFrameTime, iFrameCount - 1);
}

bool CProjProfiler::Begin(const char* sName, int iWeaponID)
{
	if (m_pFrame || !Enabled() || !I::GlobalVars)
		return false;

	m_dwThread = GetCurrentThreadId();

	auto& tFrame = m_aRing[m_iHead];
	m_iHead = (m_iHead + 1) % RingSize;
	m_iStored = std::min(m_iStored + 1, int(RingSize));

	tFrame.Reset();
	tFrame.m_sName = sName;
	tFrame.m_iFrameCount = I::GlobalVars->framecount;
	tFrame.m_iTick = I::GlobalVars->tickcount;
	tFrame.m_iWeaponID = iWeaponID;
	tFrame.m_flFrameTime = I::GlobalVars->absoluteframetime;
	tFrame.m_aPre = m_aAmbient;
	m_aAmbient = {};

	m_pFrame = &tFrame;
	tFrame.m_iStart = Now();
	return true;
}

void CProjProfiler::End()
{
	if (!m_pFrame)
		return;

	auto& tFrame = *m_pFrame;
	tFrame.m_iEnd = Now();
	tFrame.m_bPhysics = F::ProjSim.m_bPhysics;
	tFrame.m_bFastPhysics = F::ProjSim.m_bFastPhysics;
	tFrame.m_bSpecial = F::ProjSim.m_pSpecial != nullptr;
	m_pFrame = nullptr;
	m_pScope = nullptr;

	if (ToMs(tFrame.m_iEnd - tFrame.m_iStart) * Vars::Aimbot::Projectile::LagProfileFPS.Value > 1000.0)
		Dump("SELF", 0.f, tFrame.m_iFrameCount);
}

void CProjProfiler::Dump(const char* sReason, float flFrameTime, int iWindowFrame)
{
	const int64_t iNow = Now();
	const bool bSelf = flFrameTime <= 0.f;
	const int iFPS = Vars::Aimbot::Projectile::LagProfileFPS.Value;
	const int iOldest = (m_iHead - m_iStored + RingSize) % RingSize;

	std::vector<const ProjFrame_t*> vRecords, vWindow;
	for (int i = 0; i < m_iStored; i++)
	{
		auto pFrame = &m_aRing[(iOldest + i) % RingSize];
		vRecords.push_back(pFrame);
		if (pFrame->m_iFrameCount == iWindowFrame)
			vWindow.push_back(pFrame);
	}

	if (vWindow.empty())
	{
		m_iQuiet++;
		return;
	}
	if (m_iLastDump && ToMs(iNow - m_iLastDump) < 1000.0)
	{
		m_iSuppressed++;
		return;
	}
	m_iLastDump = iNow;
	m_iDumps++;

	std::string sOut;
	sOut.reserve(32768);

	if (m_iDumps == 1)
	{
		sOut += std::format("################ PROJ LAG LOG | session {} {} | build {} {} ################\n", SDK::GetDate(), SDK::GetTime(), __CONFIGURATION__, __PLATFORM__);
		sOut += "how to read: every record is one profiled entry point (Run = projectile aimbot per CreateMove, Preview = aimbot preview, others are named).\n";
		sOut += "  self_ms = time inside a zone excluding nested zones; incl_ms = including nested zones; sum of all self_ms in a record == record dur.\n";
		sOut += "  'root (code outside any zone)' is time in the entry point that is not covered by a zone. ProjSim::RunTick is only timed for physics projectiles; plain ballistic ticks are counted (projsim_ticks_simple) and their cost lands in the parent zone.\n";
		sOut += "  timer overhead is ~0.05us per zone entry; with thousands of Trace/TraceHull calls expect a few % inflation.\n\n";
	}

	double flWindowMs = 0.0;
	for (auto pFrame : vWindow)
		flWindowMs += ToMs(pFrame->m_iEnd - pFrame->m_iStart);

	int iCount = std::min(m_iFrameTimeCount, 64);
	double flSum = 0.0, flWorst = 0.0; int iUnder = 0;
	for (int i = 0; i < iCount; i++)
	{
		const float flTime = m_aFrameTimes[(m_iFrameTimeHead - 1 - i + FrameTimeSize) % FrameTimeSize];
		flSum += flTime, flWorst = std::max(flWorst, double(flTime));
		if (flTime * iFPS > 1.f)
			iUnder++;
	}

	sOut += std::format("==================== PROJ LAG DUMP #{} ====================\n", m_iDumps);
	sOut += std::format("when     : {} {} | uptime {:.1f}s | map {} | tick {} | framecount {}\n",
		SDK::GetDate(), SDK::GetTime(), I::GlobalVars->realtime, I::EngineClient->GetLevelName(), I::GlobalVars->tickcount, I::GlobalVars->framecount);
	if (!bSelf)
		sOut += std::format("trigger  : FRAME  frametime {:.1f}ms = {:.1f} fps (threshold {} fps = {:.1f}ms) | lag frame = framecount {}\n", flFrameTime * 1000.f, 1.f / flFrameTime, iFPS, 1000.f / iFPS, iWindowFrame);
	else
		sOut += std::format("trigger  : SELF   a single projectile entry point alone took longer than the {} fps budget of {:.1f}ms | framecount {}\n", iFPS, 1000.f / iFPS, iWindowFrame);
	if (iCount)
		sOut += std::format("fps stats: last {} frames avg {:.1f}ms ({:.1f} fps) worst {:.1f}ms | {} under {} fps\n", iCount, flSum * 1000.0 / iCount, iCount / flSum, flWorst * 1000.0, iUnder, iFPS);
	sOut += std::format("skipped  : {} rate-limited dumps and {} lag frames with no projectile activity since the previous dump\n", m_iSuppressed, m_iQuiet);
	sOut += std::format("world    : players={} projectiles={}\n", H::Entities.GetGroup(EntityEnum::PlayerAll).size(), H::Entities.GetGroup(EntityEnum::WorldProjectile).size());

	if (!bSelf)
	{
		const double flFrameMs = flFrameTime * 1000.0, flShare = flWindowMs * 100.0 / flFrameMs;
		sOut += std::format("verdict  : projectile code ran {:.2f}ms across {} entry point(s) in the lag frame = {:.0f}% of the {:.1f}ms frame -> {}\n",
			flWindowMs, vWindow.size(), flShare, flFrameMs, flShare >= 50.0 ? "LIKELY THE CAUSE" : flShare >= 15.0 ? "CONTRIBUTING" : "NOT the main cause (look elsewhere)");
	}
	else
		sOut += std::format("verdict  : projectile code ran {:.2f}ms across {} entry point(s) in this frame -> THE CAUSE\n", flWindowMs, vWindow.size());

	sOut += std::format("config   : movesim_accuracy={:g}% max_sim_time={:g}s (effective {:.2f}s) max_targets={} splash_prediction={} splash_mode={} splash_points={}/{} splash_restrict={}/{}/{}\n",
		Vars::Aimbot::General::MoveSimAccuracy.Value, Vars::Aimbot::Projectile::MaxSimulationTime.Value, F::MoveSim.GetMaxSimulationTime(Vars::Aimbot::Projectile::MaxSimulationTime.Value),
		Vars::Aimbot::General::MaxTargets.Value, Vars::Aimbot::Projectile::SplashPrediction.Value, Vars::Aimbot::Projectile::SplashMode.Value,
		Vars::Aimbot::Projectile::SplashPointsDirect.Value, Vars::Aimbot::Projectile::SplashPointsArc.Value,
		Vars::Aimbot::Projectile::SplashRestrictDirect.Value, Vars::Aimbot::Projectile::SplashRestrictArc.Value, Vars::Aimbot::Projectile::SplashRestrictFirst.Value);
	sOut += std::format("           trace_interval direct/splash/lob={}/{}/{} interval_retest={} modifiers={:#b} account_ping={} strafe_prediction={:#b} aim_type={} autoshoot={} nospread={}\n",
		Vars::Aimbot::Projectile::DirectTraceInterval.Value, Vars::Aimbot::Projectile::SplashTraceInterval.Value, Vars::Aimbot::Projectile::LobTraceInterval.Value,
		Vars::Aimbot::Projectile::IntervalRetest.Value, Vars::Aimbot::Projectile::Modifiers.Value, Vars::Aimbot::Projectile::AccountPing.Value,
		Vars::Aimbot::Projectile::StrafePrediction.Value, Vars::Aimbot::General::AimType.Value, Vars::Aimbot::General::AutoShoot.Value, Vars::Aimbot::General::NoSpread.Value);

	sOut += std::format("\ntimeline (oldest -> newest, * = lag window, last {} entry points):\n", vRecords.size());
	for (auto pFrame : vRecords)
	{
		const double flDur = ToMs(pFrame->m_iEnd - pFrame->m_iStart);
		const auto& aC = pFrame->m_aCounters;
		sOut += std::format("  {} {:<16} frame={} age={:>8.1f}ms dur={:>8.3f}ms weapon={:<14} targets={} canhit={} movesim_ticks={}(+{} cached) projsim_ticks={}/{}phys traces={} test_angle={} calc_angle={}\n",
			pFrame->m_iFrameCount == iWindowFrame ? "*" : " ", pFrame->m_sName, pFrame->m_iFrameCount, ToMs(iNow - pFrame->m_iEnd), flDur, WeaponName(pFrame->m_iWeaponID),
			pFrame->m_vTargets.size() + pFrame->m_iTargetsDropped, aC[ProjCounterEnum::CanHits], aC[ProjCounterEnum::MoveTicks], aC[ProjCounterEnum::MoveTicksCached],
			aC[ProjCounterEnum::ProjTicksSimple] + aC[ProjCounterEnum::ProjTicksPhysics], aC[ProjCounterEnum::ProjTicksPhysics], Traces(pFrame->m_aZones),
			aC[ProjCounterEnum::TestAngles], aC[ProjCounterEnum::CalcAngles]);
	}

	std::vector<const ProjFrame_t*> vDetail = vWindow;
	std::sort(vDetail.begin(), vDetail.end(), [](const ProjFrame_t* a, const ProjFrame_t* b) { return a->m_iEnd - a->m_iStart > b->m_iEnd - b->m_iStart; });
	if (vDetail.size() > 4)
		vDetail.resize(4);

	const ProjFrame_t* pHeaviest = nullptr;
	for (auto pFrame : vRecords)
	{
		if (pFrame->m_iFrameCount != iWindowFrame && (!pHeaviest || pFrame->m_iEnd - pFrame->m_iStart > pHeaviest->m_iEnd - pHeaviest->m_iStart))
			pHeaviest = pFrame;
	}
	if (pHeaviest && ToMs(pHeaviest->m_iEnd - pHeaviest->m_iStart) > 1.0)
		vDetail.push_back(pHeaviest);
	std::sort(vDetail.begin(), vDetail.end(), [](const ProjFrame_t* a, const ProjFrame_t* b) { return a->m_iStart < b->m_iStart; });

	for (auto pFrame : vDetail)
	{
		const double flDur = ToMs(pFrame->m_iEnd - pFrame->m_iStart);
		sOut += std::format("\n--- {} | frame={} tick={} | dur={:.3f}ms | frametime when it began={:.1f}ms | weapon={}({}) physics={} fastphysics={} special={}{}\n",
			pFrame->m_sName, pFrame->m_iFrameCount, pFrame->m_iTick, flDur, pFrame->m_flFrameTime * 1000.f, WeaponName(pFrame->m_iWeaponID), pFrame->m_iWeaponID,
			pFrame->m_bPhysics, pFrame->m_bFastPhysics, pFrame->m_bSpecial, pFrame->m_iFrameCount == iWindowFrame ? "" : " | NOT in the lag window, heaviest recent entry");
		AppendZoneTable(sOut, pFrame->m_aZones);
		sOut += std::format("    counters: {}\n", FormatCounters(pFrame->m_aCounters));
		if (SumSelf(pFrame->m_aPre))
			sOut += std::format("    sim/trace work outside any entry point before this one: {}\n", FormatZones(pFrame->m_aPre, 6));

		for (size_t i = 0; i < pFrame->m_vTargets.size(); i++)
		{
			const auto& tTarget = pFrame->m_vTargets[i];
			sOut += std::format("    target[{}] ent={} type={} hitbox={} dist={:.0f} dur={:.3f}ms result={} note=\"{}\"\n",
				i, tTarget.m_iEntity, TargetTypeName(tTarget.m_iType), tTarget.m_iHitbox, tTarget.m_flDist, ToMs(tTarget.m_iDuration), ResultName(tTarget.m_iResult), tTarget.m_sNote);
			sOut += std::format("      sim: max_sim_time={:.2f}s max_tick={} last_tick={} move_failed={} move_cached={} direct_offsets={} splash_modes={} direct_history={} splash_history={}\n",
				tTarget.m_flMaxSimTime, tTarget.m_iMaxTick, tTarget.m_iLastTick, tTarget.m_bMoveFailed, tTarget.m_bMoveCached, tTarget.m_iDirects, tTarget.m_iSplashModes,
				tTarget.m_iDirectHistory, tTarget.m_iSplashHistory);
			sOut += std::format("      self : {}\n", FormatZones(tTarget.m_aZones, 8));
			sOut += std::format("      count: {}\n", FormatCounters(tTarget.m_aCounters));
		}
		if (pFrame->m_iTargetsDropped)
			sOut += std::format("    ({} more targets not recorded)\n", pFrame->m_iTargetsDropped);
	}
	sOut += "\n";

	const std::string sPath = F::Configs.m_sConfigPath + "ProjLag.txt";
	std::error_code tError;
	if (std::filesystem::exists(sPath, tError) && std::filesystem::file_size(sPath, tError) > 8u * 1024u * 1024u)
	{
		const std::string sOld = F::Configs.m_sConfigPath + "ProjLag.old.txt";
		std::filesystem::remove(sOld, tError);
		std::filesystem::rename(sPath, sOld, tError);
	}
	if (std::ofstream tFile(sPath, std::ios::app | std::ios::binary); tFile)
		tFile << sOut;

	m_iSuppressed = m_iQuiet = 0;
	SDK::Output("ProjLag", std::format("dump #{} written to {}", m_iDumps, sPath).c_str(), { 175, 150, 255, 255 }, OUTPUT_CONSOLE);
}

ProjTargetScope_t::ProjTargetScope_t(int iEntity, int iType, int iHitbox, float flDist)
{
	auto pFrame = F::ProjProfiler.m_pFrame;
	if (!pFrame || GetCurrentThreadId() != F::ProjProfiler.m_dwThread)
		return;
	if (pFrame->m_vTargets.size() >= CProjProfiler::MaxTargets)
	{
		pFrame->m_iTargetsDropped++;
		return;
	}

	auto& tTarget = pFrame->m_vTargets.emplace_back();
	tTarget.m_iEntity = iEntity;
	tTarget.m_iType = iType;
	tTarget.m_iHitbox = iHitbox;
	tTarget.m_flDist = flDist;
	tTarget.m_iResult = -1;

	m_bValid = true;
	m_iIndex = pFrame->m_vTargets.size() - 1;
	m_aZoneBase = pFrame->m_aZones;
	m_aCounterBase = pFrame->m_aCounters;
	m_iStart = CProjProfiler::Now();
}

ProjTargetScope_t::~ProjTargetScope_t()
{
	auto pTarget = Get();
	if (!pTarget)
		return;

	auto pFrame = F::ProjProfiler.m_pFrame;
	pTarget->m_iDuration = CProjProfiler::Now() - m_iStart;
	for (int i = 0; i < ProjZoneEnum::Count; i++)
	{
		pTarget->m_aZones[i].m_iTotal = pFrame->m_aZones[i].m_iTotal - m_aZoneBase[i].m_iTotal;
		pTarget->m_aZones[i].m_iSelf = pFrame->m_aZones[i].m_iSelf - m_aZoneBase[i].m_iSelf;
		pTarget->m_aZones[i].m_iCount = pFrame->m_aZones[i].m_iCount - m_aZoneBase[i].m_iCount;
	}
	for (int i = 0; i < ProjCounterEnum::Count; i++)
		pTarget->m_aCounters[i] = pFrame->m_aCounters[i] - m_aCounterBase[i];
}

ProjTarget_t* ProjTargetScope_t::Get()
{
	auto pFrame = F::ProjProfiler.m_pFrame;
	if (!m_bValid || !pFrame || m_iIndex >= pFrame->m_vTargets.size())
		return nullptr;
	return &pFrame->m_vTargets[m_iIndex];
}

int ProjTargetScope_t::Result(int iResult, const char* sNote)
{
	if (auto pTarget = Get())
		pTarget->m_iResult = iResult, pTarget->m_sNote = sNote;
	return iResult;
}

void ProjTargetScope_t::SetSim(float flMaxSimTime, int iMaxTick, bool bMoveFailed, bool bMoveCached)
{
	if (auto pTarget = Get())
		pTarget->m_flMaxSimTime = flMaxSimTime, pTarget->m_iMaxTick = iMaxTick, pTarget->m_bMoveFailed = bMoveFailed, pTarget->m_bMoveCached = bMoveCached;
}

void ProjTargetScope_t::SetLoop(int iLastTick, int iDirects, int iSplashModes)
{
	if (auto pTarget = Get())
		pTarget->m_iLastTick = iLastTick, pTarget->m_iDirects = iDirects, pTarget->m_iSplashModes = iSplashModes;
}

void ProjTargetScope_t::SetHistory(int iDirectHistory, int iSplashHistory)
{
	if (auto pTarget = Get())
		pTarget->m_iDirectHistory = iDirectHistory, pTarget->m_iSplashHistory = iSplashHistory;
}
#endif
