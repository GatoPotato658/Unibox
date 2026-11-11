#include "CheatDetection.h"

#include "../Players/PlayerUtils.h"
#include "../Output/Output.h"
#include "../NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"

static constexpr float FLAG_SCORE = 100.f;
static constexpr float SCORE_DECAY = 1.f;

static constexpr float PITCH_SATURATED = 89.9f;
static constexpr int PITCH_STREAK = 3;
static constexpr float PITCH_WEIGHT = 50.f;

static constexpr int CHOKE_STREAK = 3;
static constexpr float CHOKE_WEIGHT = 20.f;

static constexpr int OVERFLOW_PACKETS = 3;
static constexpr float OVERFLOW_WEIGHT = 50.f;
static constexpr int SHIFT_TICKS = 10;
static constexpr float SHIFT_WEIGHT = 50.f;

static constexpr float DOUBLE_TAP_WEIGHT = 50.f;

static constexpr int SNAP_GAP = 2;
static constexpr float SNAP_DEGREES = 20.f;
static constexpr float RETURN_DEGREES = 5.f;
static constexpr float SILENT_AIM_WEIGHT = 34.f;

static constexpr int MAX_WEARABLES = 16;
static constexpr float REGION_INTERVAL = 2.f;
static constexpr float REGION_WEIGHT = 50.f;

static constexpr int PERF_MAX_RECORDS = 32;
static constexpr double PERF_TIMEOUT = 2.0;
static constexpr double PROBE_INTERVAL = 3.0;
static constexpr double PROBE_SPACING = 1.0;

static const char* GetDetectionName(EDetection eDetection)
{
	switch (eDetection)
	{
	case EDetection::InvalidPitch: return "invalid pitch";
	case EDetection::PacketChoke: return "packet choke";
	case EDetection::CommandOverflow: return "command overflow";
	case EDetection::TickbaseShift: return "tickbase shift";
	case EDetection::DoubleTap: return "double tap";
	case EDetection::SilentAim: return "silent aim";
	case EDetection::EquipRegions: return "equip region unlock";
	default: return "unknown";
	}
}

static bool IsConclusive(EDetection eDetection)
{
	return eDetection != EDetection::PacketChoke;
}

static bool IsShortGap(int iGap)
{
	return iGap > 0 && iGap <= SNAP_GAP;
}

static bool SharesEquipRegion(CTFPlayer* pPlayer, uint32_t uAccountID)
{
	auto pSchema = CEconItemSchema::GetInstance();
	if (!pSchema)
		return false;

	std::array<int, MAX_WEARABLES> aSeen = {};
	int iSeen = 0;
	uint32_t uCovered = 0;

	auto& vWearables = pPlayer->m_hMyWearables();
	for (int i = 0; i < vWearables.Count() && iSeen < MAX_WEARABLES; i++)
	{
		auto pWearable = vWearables[i].Get();
		if (!pWearable || pWearable->m_iAccountID() != uAccountID)
			continue;

		const int iDefinition = pWearable->m_iItemDefinitionIndex();
		if (std::find(aSeen.begin(), aSeen.begin() + iSeen, iDefinition) != aSeen.begin() + iSeen)
			continue;

		aSeen[iSeen++] = iDefinition;

		auto pDefinition = pSchema->GetItemDefinition(iDefinition);
		if (!pDefinition)
			continue;

		const uint32_t uRegions = pDefinition->m_uEquipRegionMask();
		if (uRegions & uCovered)
			return true;

		uCovered |= uRegions;
	}

	return false;
}

void CCheatDetection::Reset()
{
	m_aRecords = {};
	m_dNextProbe = 0.0;
	m_tPerf = {};
	m_bPerfInFlight = false;
}

void CCheatDetection::Adopt(DetectionRecord_t& tRecord, int iIndex, uint32_t uHandle)
{
	tRecord = {};
	tRecord.m_uHandle = uHandle;

	player_info_t tInfo = {};
	tRecord.m_bEligible = I::EngineClient->GetPlayerInfo(iIndex, &tInfo) && tInfo.friendsID
		&& !tInfo.fakeplayer && !tInfo.ishltv && !tInfo.isreplay;
	tRecord.m_uAccountID = tInfo.friendsID;
}

void CCheatDetection::Run()
{
	ResolvePerf();

	if (!Vars::CheatDetection::Enabled.Value || !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo())
		return;

	static auto sv_cheats = H::ConVars.FindVar("sv_cheats");
	if (sv_cheats && sv_cheats->GetBool())
		return;

	const int iServerTick = I::ClientState->m_ClockDriftMgr.m_nServerTick;
	const int iLocal = I::EngineClient->GetLocalPlayer();
	const int iCheaterTag = F::PlayerUtils.TagToIndex(CHEATER_TAG);

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		const int iIndex = pEntity->entindex();
		if (iIndex == iLocal || iIndex < 1 || iIndex >= MAX_PLAYERS)
			continue;

		auto& tRecord = m_aRecords[iIndex];
		const uint32_t uHandle = uint32_t(pEntity->GetRefEHandle().ToInt());
		if (tRecord.m_uHandle != uHandle)
			Adopt(tRecord, iIndex, uHandle);

		if (!tRecord.m_bEligible || tRecord.m_bFlagged)
			continue;

		if (F::PlayerUtils.HasTag(tRecord.m_uAccountID, iCheaterTag))
		{
			tRecord.m_bFlagged = true;
			continue;
		}

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->IsDormant())
		{
			tRecord.m_bTracked = false;
			continue;
		}

		if (!pPlayer->IsAlive())
		{
			tRecord.m_bTracked = false;
			tRecord.m_bRegionSeen = false;
			continue;
		}

		Observe(tRecord, iIndex, pPlayer, iServerTick);
	}

	Probe();
}

void CCheatDetection::Observe(DetectionRecord_t& tRecord, int iIndex, CTFPlayer* pPlayer, int iServerTick)
{
	if (iServerTick >= tRecord.m_iRegionTick)
	{
		tRecord.m_iRegionTick = iServerTick + TIME_TO_TICKS(REGION_INTERVAL);
		if (!tRecord.m_bRegionSeen && SharesEquipRegion(pPlayer, tRecord.m_uAccountID))
		{
			tRecord.m_bRegionSeen = true;
			Infract(tRecord, iIndex, EDetection::EquipRegions, REGION_WEIGHT);
		}
	}

	const int iSimTick = TIME_TO_TICKS(pPlayer->m_flSimulationTime());
	if (tRecord.m_bTracked && iSimTick == tRecord.m_iSimTick)
	{
		tRecord.m_iSeenTick = iServerTick;
		return;
	}

	const Vec3 vEye = { pPlayer->m_angEyeAnglesX(), pPlayer->m_angEyeAnglesY(), 0.f };
	const int iGap = iSimTick - tRecord.m_iSimTick;

	if (tRecord.m_bTracked)
	{
		if (tRecord.m_iSeenTick - tRecord.m_iChangedTick >= MAX_NEW_COMMANDS)
		{
			tRecord.m_bProbeWanted = true;
			if (++tRecord.m_iChokeStreak == CHOKE_STREAK)
			{
				tRecord.m_iChokeStreak = 0;
				Infract(tRecord, iIndex, EDetection::PacketChoke, CHOKE_WEIGHT);
			}
		}
		else
			tRecord.m_iChokeStreak = 0;

		if (tRecord.m_bShot && IsShortGap(tRecord.m_iGap) && IsShortGap(iGap)
			&& Math::CalcFov(tRecord.m_vPreviousEye, tRecord.m_vEye) >= SNAP_DEGREES
			&& Math::CalcFov(tRecord.m_vPreviousEye, vEye) <= RETURN_DEGREES)
			Infract(tRecord, iIndex, EDetection::SilentAim, SILENT_AIM_WEIGHT);

		tRecord.m_vPreviousEye = tRecord.m_vEye;
	}
	else
	{
		tRecord.m_iChokeStreak = 0;
		tRecord.m_iPitchStreak = 0;
		tRecord.m_vPreviousEye = vEye;
	}

	if (fabsf(vEye.x) < PITCH_SATURATED)
		tRecord.m_iPitchStreak = 0;
	else if (++tRecord.m_iPitchStreak == PITCH_STREAK)
	{
		tRecord.m_iPitchStreak = 0;
		Infract(tRecord, iIndex, EDetection::InvalidPitch, PITCH_WEIGHT);
	}

	tRecord.m_vEye = vEye;
	tRecord.m_iGap = tRecord.m_bTracked ? iGap : 0;
	tRecord.m_iShots = 0;
	tRecord.m_bShot = false;
	tRecord.m_bTracked = true;
	tRecord.m_iSimTick = iSimTick;
	tRecord.m_iChangedTick = iServerTick;
	tRecord.m_iSeenTick = iServerTick;
}

void CCheatDetection::OnFireBullets(int iShooter)
{
	if (iShooter < 1 || iShooter >= MAX_PLAYERS)
		return;

	auto& tRecord = m_aRecords[iShooter];
	if (!tRecord.m_bTracked || tRecord.m_bFlagged
		|| tRecord.m_iChangedTick != I::ClientState->m_ClockDriftMgr.m_nServerTick)
		return;

	tRecord.m_bShot = true;

	if (++tRecord.m_iShots == 2 && tRecord.m_iGap && tRecord.m_iGap < 2)
		Infract(tRecord, iShooter, EDetection::DoubleTap, DOUBLE_TAP_WEIGHT);
}

void CCheatDetection::Probe()
{
	const double dNow = SDK::PlatFloatTime();
	if (dNow < m_dNextProbe || m_bPerfInFlight
		|| F::NoSpreadHitscan.IsWaitingForPlayerPerf() || F::AntiCheatCompatibility.Active())
		return;

	for (int i = 1; i < MAX_PLAYERS; i++)
	{
		auto& tRecord = m_aRecords[i];
		if (!tRecord.m_bProbeWanted || !tRecord.m_bTracked || tRecord.m_bFlagged
			|| dNow - tRecord.m_dProbeTime < PROBE_INTERVAL)
			continue;

		I::ClientState->SendStringCmd(std::format("playerperf {} {}", i, PERF_MAX_RECORDS).c_str());

		m_tPerf = {};
		m_dPerfSent = dNow;
		m_iPerfIndex = i;
		m_bPerfInFlight = true;

		tRecord.m_bProbeWanted = false;
		tRecord.m_dProbeTime = dNow;
		m_dNextProbe = dNow + PROBE_SPACING;
		return;
	}
}

bool CCheatDetection::ParsePlayerPerf(const bf_read& msgData)
{
	if (!m_bPerfInFlight)
		return false;

	bf_read tReader = msgData;
	tReader.Reset();
	if (tReader.ReadByte() != HUD_PRINTCONSOLE)
		return false;

	bool bRecognised = false;
	char sLine[128];
	int iChars = 0;
	while (tReader.ReadString(sLine, sizeof(sLine), true, &iChars) && iChars > 0)
	{
		PerfFrame_t tFrame = {};
		PerfPacket_t tPacket = {};
		double dGameSimulation = 0.0, dVelocity = 0.0;

		if (sscanf_s(sLine, "%lf %d %d %lf %lf vel %lf", &tFrame.m_dTime, &tFrame.m_iNumCmds, &tFrame.m_iTicksCorrected,
			&tFrame.m_dFinalSimulation, &dGameSimulation, &dVelocity) == 6)
		{
			if (m_tPerf.m_vFrames.size() < PERF_MAX_RECORDS)
				m_tPerf.m_vFrames.push_back(tFrame);
		}
		else if (sscanf_s(sLine, "%lf %d %d", &tPacket.m_dTime, &tPacket.m_iNumCmds, &tPacket.m_iDropped) == 3)
		{
			if (m_tPerf.m_vPackets.size() < PERF_MAX_RECORDS)
				m_tPerf.m_vPackets.push_back(tPacket);
		}
		else
			break;

		bRecognised = true;
	}

	return bRecognised;
}

void CCheatDetection::ResolvePerf()
{
	if (!m_bPerfInFlight)
		return;

	if (m_tPerf.m_vPackets.empty())
	{
		if (SDK::PlatFloatTime() - m_dPerfSent > PERF_TIMEOUT)
			m_bPerfInFlight = false;
		return;
	}

	m_bPerfInFlight = false;
	OnPerf(m_iPerfIndex, m_tPerf);
}

void CCheatDetection::OnPerf(int iIndex, const PerfDump_t& tDump)
{
	auto& tRecord = m_aRecords[iIndex];
	if (!tRecord.m_dProbeTime || tRecord.m_bFlagged)
		return;

	const double dSeen = tRecord.m_dPerfSeen;
	const auto& vPackets = tDump.m_vPackets;
	const auto& vFrames = tDump.m_vFrames;

	int iOverflows = 0;
	for (size_t i = 0; i < vPackets.size() && vPackets[i].m_dTime > dSeen; i++)
	{
		const auto& tPacket = vPackets[i];
		if (tPacket.m_iNumCmds >= MAX_NEW_COMMANDS - 1 && tPacket.m_iNumCmds + tPacket.m_iDropped > MAX_NEW_COMMANDS)
			iOverflows++;
	}

	int iShifts = 0;
	for (size_t i = 0; i + 1 < vFrames.size() && vFrames[i].m_dTime > dSeen; i++)
	{
		const auto& tFrame = vFrames[i];
		const auto& tPrevious = vFrames[i + 1];
		const int iAdvance = TIME_TO_TICKS(float(tFrame.m_dFinalSimulation - tPrevious.m_dFinalSimulation));
		if (!tFrame.m_iTicksCorrected || tFrame.m_iNumCmds - iAdvance < SHIFT_TICKS)
			continue;

		const bool bBurst = std::any_of(vPackets.begin(), vPackets.end(), [&](const PerfPacket_t& tPacket)
		{
			return tPacket.m_dTime >= tPrevious.m_dTime && tPacket.m_dTime <= tFrame.m_dTime && tPacket.m_iNumCmds >= SHIFT_TICKS;
		});
		if (bBurst)
			iShifts++;
	}

	tRecord.m_dPerfSeen = std::max(vPackets.front().m_dTime, vFrames.empty() ? 0.0 : vFrames.front().m_dTime);

	if (iOverflows >= OVERFLOW_PACKETS)
		Infract(tRecord, iIndex, EDetection::CommandOverflow, OVERFLOW_WEIGHT);
	if (iShifts)
		Infract(tRecord, iIndex, EDetection::TickbaseShift, SHIFT_WEIGHT);
}

void CCheatDetection::Infract(DetectionRecord_t& tRecord, int iIndex, EDetection eDetection, float flWeight)
{
	if (tRecord.m_bFlagged)
		return;

	const double dNow = SDK::PlatFloatTime();
	const float flDecay = float(dNow - tRecord.m_dScoreTime) * SCORE_DECAY;

	tRecord.m_flScore = std::max(tRecord.m_flScore - flDecay, 0.f) + flWeight;
	tRecord.m_dScoreTime = dNow;
	tRecord.m_aHits[size_t(eDetection)]++;

	if (tRecord.m_flScore >= FLAG_SCORE && IsConclusive(eDetection))
		Flag(tRecord, iIndex);
}

void CCheatDetection::Flag(DetectionRecord_t& tRecord, int iIndex)
{
	tRecord.m_bFlagged = true;

	const uint32_t uAccountID = tRecord.m_uAccountID;
	player_info_t tInfo = {};
	if (H::Entities.IsFriend(uAccountID) || H::Entities.InParty(uAccountID)
		|| F::PlayerUtils.HasTag(uAccountID, F::PlayerUtils.TagToIndex(FRIEND_TAG))
		|| F::PlayerUtils.HasTag(uAccountID, F::PlayerUtils.TagToIndex(IGNORED_TAG))
		|| !I::EngineClient->GetPlayerInfo(iIndex, &tInfo))
		return;

	std::string sReason;
	int iDetections = 0;
	for (size_t i = 0; i < tRecord.m_aHits.size(); i++)
	{
		if (!tRecord.m_aHits[i])
			continue;

		if (!sReason.empty())
			sReason += ", ";
		sReason += std::format("{} x{}", GetDetectionName(EDetection(i)), tRecord.m_aHits[i]);
		iDetections += tRecord.m_aHits[i];
	}

	const char* sName = F::PlayerUtils.GetPlayerName(iIndex, tInfo.name);
	F::Output.CheatDetection(sName, "marked", sReason.c_str());
	F::PlayerUtils.AddTag(uAccountID, F::PlayerUtils.TagToIndex(CHEATER_TAG), true, sName, sReason.c_str(), iDetections, true);
}
