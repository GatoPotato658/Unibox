#include "Backtrack.h"

#include "../PacketManip/FakeLag/FakeLag.h"
#include "../Ticks/Ticks.h"
#include "../Aimbot/Aimbot.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../ImGui/IndicatorPanel.h"

void CBacktrack::Reset()
{
	m_mRecords.clear();
	m_mDidShoot.clear();
	m_tLatency = {};
	m_flMaxUnlag = 1.f;
	m_flFakeLatency = 0.f;
	m_flFakeInterp = 0.015f;
	m_bSettingUpBones = false;
	m_iTickCount = 0;
	m_flSentInterp = -1.f;
	m_tRecord = {};
}

float CBacktrack::GetReal(int iFlow, bool bNoFake)
{
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return 0.f;

	if (iFlow != MAX_FLOWS)
		return pNetChan->GetLatency(iFlow) - (bNoFake && iFlow == FLOW_INCOMING ? GetFakeLatency() : 0.f);
	return pNetChan->GetLatency(FLOW_INCOMING) + pNetChan->GetLatency(FLOW_OUTGOING) - (bNoFake ? GetFakeLatency() : 0.f);
}

float CBacktrack::GetWishFake()
{
	return std::clamp(Vars::Backtrack::Latency.Value / 1000.f, 0.f, m_flMaxUnlag);
}

float CBacktrack::GetWishLerp()
{
	if (F::AntiCheatCompatibility.Active())
		return std::clamp(Vars::Backtrack::Interp.Value / 1000.f, G::Lerp, 0.1f);

	return std::clamp(Vars::Backtrack::Interp.Value / 1000.f, G::Lerp, m_flMaxUnlag);
}

float CBacktrack::GetFakeLatency()
{
	return m_flFakeLatency;
}

float CBacktrack::GetFakeInterp()
{
	if (F::AntiCheatCompatibility.Active())
		return std::min(m_flFakeInterp, 0.1f);

	return m_flFakeInterp;
}

float CBacktrack::GetWindow()
{
	return Vars::Backtrack::Window.Value / 1000.f;
}

int CBacktrack::GetAnticipatedChoke(int iMethod)
{
	int iAnticipatedChoke = 0;
	if (F::Ticks.CanChoke() && G::PrimaryWeaponType != EWeaponType::HITSCAN && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
		iAnticipatedChoke = 1;
	if (F::FakeLag.m_iGoal && !Vars::Fakelag::UnchokeOnAttack.Value && F::Ticks.m_iShiftedTicks == F::Ticks.m_iShiftedGoal && !F::Ticks.m_bDoubletap)
		iAnticipatedChoke = F::FakeLag.m_iGoal - I::ClientState->chokedcommands;
	return iAnticipatedChoke;
}

int CBacktrack::GetServerTick()
{
	return m_iTickCount + TIME_TO_TICKS(GetReal(FLOW_OUTGOING)) + GetAnticipatedChoke();
}

int CBacktrack::GetRewindTick(float flTimeMod)
{
	float flCorrect = std::clamp(GetReal(MAX_FLOWS, false) + ROUND_TO_TICKS(GetFakeInterp()), 0.f, m_flMaxUnlag) + flTimeMod;
	return GetServerTick() + Vars::Backtrack::Offset.Value - TIME_TO_TICKS(flCorrect);
}

void CBacktrack::CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (F::AntiCheatCompatibility.Active())
		return;

	pCmd->tick_count += TIME_TO_TICKS(GetFakeInterp());
	if (!Vars::Visuals::Removals::Lerp.Value && !Vars::Visuals::Removals::Interpolation.Value)
		pCmd->tick_count -= TIME_TO_TICKS(G::Lerp);
}

void CBacktrack::SendLerp()
{
	static Timer tTimer = {};
	if (!tTimer.Run(0.1f))
		return;

	float flTarget = GetWishLerp();
	if (m_flSentInterp != flTarget)
	{
		m_flSentInterp = flTarget;

		auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
		if (pNetChan && I::EngineClient->IsConnected())
		{
			NET_SetConVar tConvar1 = { "cl_interp", std::to_string(m_flSentInterp).c_str() };
			pNetChan->SendNetMsg(tConvar1);

			NET_SetConVar tConvar2 = { "cl_interp_ratio", "1" };
			pNetChan->SendNetMsg(tConvar2);

			NET_SetConVar tConvar3 = { "cl_interpolate", "1" };
			pNetChan->SendNetMsg(tConvar3);
		}
	}
}

void CBacktrack::SetLerp()
{
	m_flFakeInterp = m_flSentInterp;
}

bool CBacktrack::GetRecords(CBaseEntity* pEntity, std::vector<TickRecord*>& vReturn)
{
	if (!m_mRecords.contains(pEntity))
		return false;

	auto& vRecords = m_mRecords[pEntity];
	vReturn.reserve(vRecords.size());
	for (auto& tRecord : vRecords)
		vReturn.push_back(&tRecord);
	return true;
}

std::vector<TickRecord*> CBacktrack::GetValidRecords(std::vector<TickRecord*>& vRecords, CTFPlayer* pLocal, bool bDistance, float flTimeMod)
{
	if (vRecords.empty())
		return {};

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return {};

	std::vector<TickRecord*> vReturn = {};
	float flCorrect = std::clamp(GetReal(MAX_FLOWS, false) + ROUND_TO_TICKS(GetFakeInterp()), 0.f, m_flMaxUnlag) + flTimeMod;
	int iServerTick = GetServerTick() + Vars::Backtrack::Offset.Value;

	if (!F::AntiCheatCompatibility.Active() && GetWindow())
	{
		for (auto pRecord : vRecords)
		{
			if (pRecord->m_bInvalid)
				continue;

			float flDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(pRecord->m_flSimTime));
			if (fabsf(flDelta) > GetWindow())
				continue;

			vReturn.push_back(pRecord);
		}
	}

	if (vReturn.empty())
	{
		float flMinDelta = 0.2f;
		for (auto pRecord : vRecords)
		{
			if (pRecord->m_bInvalid)
				continue;

			float flDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(pRecord->m_flSimTime));
			if (fabsf(flDelta) > flMinDelta)
				continue;

			flMinDelta = fabsf(flDelta);
			vReturn = { pRecord };
		}
	}
	else if (pLocal && vReturn.size() > 1)
	{
		if (bDistance)
			std::sort(vReturn.begin(), vReturn.end(), [&](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (Vars::Backtrack::PreferOnShot.Value && a->m_bOnShot != b->m_bOnShot)
					return a->m_bOnShot > b->m_bOnShot;

				return pLocal->m_vecOrigin().DistToSqr(a->m_vOrigin) < pLocal->m_vecOrigin().DistToSqr(b->m_vOrigin);
			});
		else
			std::sort(vReturn.begin(), vReturn.end(), [&](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (Vars::Backtrack::PreferOnShot.Value && a->m_bOnShot != b->m_bOnShot)
					return a->m_bOnShot > b->m_bOnShot;

				float flADelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(a->m_flSimTime));
				float flBDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(b->m_flSimTime));
				return fabsf(flADelta) < fabsf(flBDelta);
			});
	}

	return vReturn;
}

matrix3x4* CBacktrack::GetBones(CBaseEntity* pEntity)
{
	std::vector<TickRecord*> vRecords = {};
	if (F::Backtrack.GetRecords(pEntity, vRecords) && !vRecords.empty())
		return vRecords.front()->m_aBones;
	return nullptr;
}

static matrix3x4 s_aBones[MAXSTUDIOBONES];

void CBacktrack::MakeRecords()
{
	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer() ||
			pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() ||
			!H::Entities.GetDeltaTime(pPlayer->entindex()))
			continue;

		auto& vRecords = m_mRecords[pPlayer];
		if (!vRecords.empty() && vRecords.front().m_flSimTime >= pPlayer->m_flSimulationTime())
			continue;

		const Vec3 vOldAbsOrigin = pPlayer->GetAbsOrigin();
		const Vec3 vOldAbsAngles = pPlayer->GetAbsAngles();
		pPlayer->SetAbsOrigin(pPlayer->m_vecOrigin());
		pPlayer->SetAbsAngles(pPlayer->m_angRotation());
		pPlayer->InvalidateBoneCache();

		m_bSettingUpBones = true;
		bool bSetup = pPlayer->SetupAimBones(s_aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, pPlayer->m_flSimulationTime());
		m_bSettingUpBones = false;

		pPlayer->SetAbsOrigin(vOldAbsOrigin);
		pPlayer->SetAbsAngles(vOldAbsAngles);
		pPlayer->InvalidateBoneCache();

		if (!bSetup)
			continue;
		TickRecord tCurRecord = {
			pPlayer->m_flSimulationTime(),
			pPlayer->m_vecOrigin(),
			pPlayer->m_vecMins(),
			pPlayer->m_vecMaxs(),
			m_mDidShoot[pPlayer->entindex()]
		};
		tCurRecord.m_vAngles = pPlayer->m_angRotation();
		tCurRecord.m_vVelocity = pPlayer->m_vecVelocity();
		tCurRecord.m_flCycle = pPlayer->m_flCycle();
		tCurRecord.m_flPlaybackRate = pPlayer->m_flPlaybackRate();
		memcpy(tCurRecord.m_aBones, s_aBones, sizeof(tCurRecord.m_aBones));
		const bool bHasLastRecord = !vRecords.empty();
		const Vec3 vLastOrigin = bHasLastRecord ? vRecords.front().m_vOrigin : Vec3();
		vRecords.emplace_front(tCurRecord);

		bool bLagComp = false;
		if (bHasLastRecord)
		{
			const Vec3 vDelta = tCurRecord.m_vOrigin - vLastOrigin;

			static auto sv_lagcompensation_teleport_dist = H::ConVars.FindVar("sv_lagcompensation_teleport_dist");
			const float flDist = powf(sv_lagcompensation_teleport_dist->GetFloat(), 2.f);
			if (vDelta.Length2DSqr() > flDist)
			{
				bLagComp = true;
				if (!H::Entities.GetLagCompensation(pPlayer->entindex()))
					vRecords.resize(1);
				std::for_each(vRecords.begin() + 1, vRecords.end(), [](auto& tRecord) { tRecord.m_bInvalid = true; });
			}

			for (auto& tRecord : vRecords)
			{
				if (!tRecord.m_bInvalid)
					continue;

				tRecord.m_vOrigin = tCurRecord.m_vOrigin;
				tRecord.m_vMins = tCurRecord.m_vMins;
				tRecord.m_vMaxs = tCurRecord.m_vMaxs;
				tRecord.m_bOnShot = tCurRecord.m_bOnShot;
				memcpy(tRecord.m_aBones, tCurRecord.m_aBones, sizeof(tRecord.m_aBones));
			}
		}

		H::Entities.SetLagCompensation(pPlayer->entindex(), bLagComp);
		m_mDidShoot[pPlayer->entindex()] = false;
	}
}

void CBacktrack::CleanRecords()
{
	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
			continue;

		auto& vRecords = m_mRecords[pPlayer];

		if (!pPlayer->IsAlive() || pPlayer->IsAGhost())
		{
			vRecords.clear();
			continue;
		}

		const float flDeadtime = I::GlobalVars->curtime + GetReal() - m_flMaxUnlag;
		if (vRecords.size() > 1 && vRecords.back().m_flSimTime == std::numeric_limits<float>::max())
			vRecords.pop_back();
		while (!vRecords.empty())
		{
			if (vRecords.back().m_flSimTime < flDeadtime || vRecords.size() > 1 && vRecords.back().m_flSimTime == std::numeric_limits<float>::max())
				vRecords.pop_back();
			else
				break;
		}

	}
}

void CBacktrack::Store()
{
	if (!I::EngineClient->IsInGame())
		return;

	static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
	m_flMaxUnlag = sv_maxunlag->GetFloat();

	MakeRecords();
	CleanRecords();
}

void CBacktrack::ResolverUpdate(CBaseEntity* pEntity)
{

}

void CBacktrack::ReportShot(int iIndex)
{
	if (!Vars::Backtrack::PreferOnShot.Value)
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
	auto pPlayer = pEntity ? pEntity->As<CTFPlayer>() : nullptr;
	if (!pPlayer)
		return;

	auto pActiveWeapon = pPlayer->m_hActiveWeapon().Get();
	auto pWeapon = pActiveWeapon ? pActiveWeapon->As<CTFWeaponBase>() : nullptr;
	if (SDK::GetWeaponType(pWeapon) != EWeaponType::HITSCAN)
		return;

	m_mDidShoot[pEntity->entindex()] = true;
}

static constexpr int LATENCY_RING_ROOM = 58;
static constexpr double LATENCY_FILTER_GRACE = 1.5;
static constexpr int LATENCY_MAX_BLOCK_BYTES = 65536;
static constexpr int LATENCY_COPY_LIMIT = 32;

void CBacktrack::SyncLatency(CNetChannel* pNetChan)
{
	if (pNetChan != m_tLatency.m_pNetChan)
	{
		m_tLatency = {};
		m_tLatency.m_pNetChan = pNetChan;
		m_flFakeLatency = 0.f;
	}
}

float CBacktrack::GetFakeTarget(CNetChannel* pNetChan)
{
	if (!Vars::Backtrack::Latency.Value || I::EngineClient->IsPlayingDemo())
		return 0.f;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive())
		return 0.f;

	static auto cl_updaterate = H::ConVars.FindVar("cl_updaterate");
	static auto sv_maxupdaterate = H::ConVars.FindVar("sv_maxupdaterate");
	float flUpdateRate = cl_updaterate->GetFloat();
	if (sv_maxupdaterate->GetFloat() > 0.f)
		flUpdateRate = std::min(flUpdateRate, sv_maxupdaterate->GetFloat());

	const float flUnlagRoom = m_flMaxUnlag - pNetChan->GetLatency(FLOW_OUTGOING) - ROUND_TO_TICKS(GetFakeInterp()) - 2 * TICK_INTERVAL;
	const float flRingRoom = LATENCY_RING_ROOM / std::max(flUpdateRate, 1.f);
	return std::clamp(GetWishFake(), 0.f, std::min(flUnlagRoom, flRingRoom));
}

const PacketRecord_t* CBacktrack::ChoosePacket(double dNow, float flTarget)
{
	if (m_tLatency.m_dPackets.empty())
		return nullptr;

	const int nNewest = m_tLatency.m_dPackets.front().m_nSequenceNr;
	const PacketRecord_t* pChosen = nullptr;
	for (auto& tRecord : m_tLatency.m_dPackets)
	{
		if (nNewest - tRecord.m_nSequenceNr > LATENCY_RING_ROOM || tRecord.m_nSequenceNr < m_tLatency.m_nPresentedSequenceNr)
			break;

		pChosen = &tRecord;
		if (dNow - tRecord.m_dTime >= flTarget)
			break;
	}

	return pChosen;
}

void CBacktrack::PushLatencySample(double dDelay)
{
	auto& dSamples = m_tLatency.m_dSamples;
	dSamples.push_front(dDelay);
	if (dSamples.size() > 64)
		dSamples.pop_back();

	double dSum = 0.0;
	for (double dSample : dSamples)
		dSum += dSample;
	m_flFakeLatency = float(dSum / dSamples.size());
}

void CBacktrack::AdjustPing(CNetChannel* pNetChan)
{
	m_tLatency.m_nRealInSequenceNr = pNetChan->m_nInSequenceNr;
	m_tLatency.m_bFaked = false;

	SyncLatency(pNetChan);
	m_tLatency.m_nSendingSequenceNr = pNetChan->m_nOutSequenceNr;

	const double dNow = F::Ticks.PacketTime(pNetChan->IsLoopback());
	const float flTarget = GetFakeTarget(pNetChan);
	const bool bTransfer = pNetChan->m_ReceiveList()[FRAG_NORMAL_STREAM].buffer || pNetChan->m_ReceiveList()[FRAG_FILE_STREAM].buffer;

	auto pRecord = flTarget > 0.f && !bTransfer ? ChoosePacket(dNow, flTarget) : nullptr;
	if (!pRecord)
	{
		m_tLatency.m_nPresentedSequenceNr = -1;
		PushLatencySample(0.0);
		return;
	}

	m_tLatency.m_nPresentedSequenceNr = pRecord->m_nSequenceNr;
	m_tLatency.m_dLastFaked = dNow;
	PushLatencySample(dNow - pRecord->m_dTime);

	pNetChan->m_nInSequenceNr = pRecord->m_nSequenceNr + NET_FRAMES_BACKUP;
	m_tLatency.m_bFaked = true;
}

void CBacktrack::RestorePing(CNetChannel* pNetChan, bool bSent)
{
	if (m_tLatency.m_bFaked)
		pNetChan->m_nInSequenceNr = m_tLatency.m_nRealInSequenceNr;

	if (!bSent)
		return;

	for (auto& tState : m_tLatency.m_aSubChannels)
	{
		if (!tState.m_bFlipPending)
			continue;

		tState.m_bFlipPending = false;
		tState.m_nFlipSequenceNr = m_tLatency.m_nSendingSequenceNr;
		tState.m_nPreviousSequenceNr = m_tLatency.m_nLastSentSequenceNr;
	}

	m_tLatency.m_nLastSentSequenceNr = m_tLatency.m_nSendingSequenceNr;
}

void CBacktrack::PacketStart(CNetChannel* pNetChan, bool bHasHeader)
{
	SyncLatency(pNetChan);

	m_tLatency.m_bPacketHeader = bHasHeader;
	m_tLatency.m_bPacketOpen = true;
	m_tLatency.m_bPacketCopy = false;
	m_tLatency.m_iPacketIndex = -1;
	m_tLatency.m_nReliableBefore = pNetChan->m_nInReliableState;
	m_tLatency.m_nPreviousAck = pNetChan->m_nOutSequenceNrAck;
	m_tLatency.m_aPeeks = {};
}

void CBacktrack::PacketEnd(CNetChannel* pNetChan, double dReceived)
{
	m_tLatency.m_bPacketOpen = false;

	if (m_tLatency.m_bPacketCopy && m_tLatency.m_iPacketIndex >= 0
		&& pNetChan->m_nInReliableState == (m_tLatency.m_nReliableBefore ^ (1 << m_tLatency.m_iPacketIndex)))
		pNetChan->m_nInReliableState = m_tLatency.m_nReliableBefore;
	m_tLatency.m_bPacketCopy = false;

	if (!m_tLatency.m_bPacketHeader)
		return;

	auto& dPackets = m_tLatency.m_dPackets;
	const int nSequenceNr = pNetChan->m_nInSequenceNr;
	if (!dPackets.empty() && dPackets.front().m_nSequenceNr >= nSequenceNr)
		return;

	dPackets.push_front({ nSequenceNr, dReceived });
	if (dPackets.size() > 128)
		dPackets.pop_back();
}

bool CBacktrack::IsFiltering(CNetChannel* pNetChan)
{
	if (pNetChan != m_tLatency.m_pNetChan || m_tLatency.m_dLastFaked < 0.0)
		return false;

	return F::Ticks.PacketTime(pNetChan->IsLoopback()) - m_tLatency.m_dLastFaked < LATENCY_FILTER_GRACE;
}

bool CBacktrack::PeekBlock(CNetChannel* pNetChan, bf_read& tBuffer, int iStream, BlockPeek_t& tOut)
{
	const int iBegin = tBuffer.m_iCurBit;
	const bool bSingle = tBuffer.ReadOneBit() == 0;

	int iStart = 0, iNum = 0;
	if (!bSingle)
	{
		iStart = int(tBuffer.ReadUBitLong(MAX_FILE_SIZE_BITS - FRAGMENT_BITS));
		iNum = int(tBuffer.ReadUBitLong(3));
	}

	unsigned int uBytes = 0;
	int iFragments = 0;
	if (bSingle || !iStart)
	{
		if (bSingle)
		{
			if (tBuffer.ReadOneBit())
				tBuffer.ReadUBitLong(MAX_FILE_SIZE_BITS);
			uBytes = tBuffer.ReadVarInt32();
		}
		else
		{
			if (tBuffer.ReadOneBit())
			{
				tBuffer.ReadUBitLong(32);
				char sFilename[MAX_OSPATH] = {};
				tBuffer.ReadString(sFilename, sizeof(sFilename));
			}
			if (tBuffer.ReadOneBit())
				tBuffer.ReadUBitLong(MAX_FILE_SIZE_BITS);
			uBytes = tBuffer.ReadUBitLong(MAX_FILE_SIZE_BITS);
		}

		iFragments = int((uBytes + FRAGMENT_SIZE - 1) / FRAGMENT_SIZE);
		if (bSingle)
			iStart = 0, iNum = iFragments;
	}
	else
	{
		auto& tTransfer = pNetChan->m_ReceiveList()[iStream];
		uBytes = tTransfer.bytes;
		iFragments = tTransfer.numFragments;
	}

	int iLength = iNum * FRAGMENT_SIZE;
	if (iStart + iNum == iFragments)
	{
		const int iRest = FRAGMENT_SIZE - int(uBytes % FRAGMENT_SIZE);
		if (iRest < FRAGMENT_SIZE)
			iLength -= iRest;
	}

	if (tBuffer.m_bOverflow || iLength <= 0 || iLength > LATENCY_MAX_BLOCK_BYTES)
		return false;

	uint64_t uHash = FNV1A::uHash64;
	for (int i = 0; i < iLength; i++)
	{
		uHash ^= uint64_t(tBuffer.ReadUBitLong(8));
		uHash *= 0x100000001b3ull;
	}
	if (tBuffer.m_bOverflow)
		return false;

	tOut = { true, tBuffer.m_iCurBit - iBegin, { uHash, iStart, iNum, iLength, true } };
	return true;
}

bool CBacktrack::IsCopy(CNetChannel* pNetChan, SubChannel_t& tState)
{
	const int nAck = pNetChan->m_nOutSequenceNrAck;
	if (nAck < tState.m_nFlipSequenceNr)
		return true;

	const bool bCrossing = m_tLatency.m_nPreviousAck < tState.m_nFlipSequenceNr && tState.m_nFlipSequenceNr <= nAck;
	const bool bQueued = m_tLatency.m_nPreviousAck < tState.m_nPreviousSequenceNr;
	return bCrossing && bQueued;
}

bool CBacktrack::PeekPacket(CNetChannel* pNetChan, bf_read& tBuffer, int iFirstStream)
{
	const int iSaved = tBuffer.m_iCurBit;
	bool bIdentical = true, bPresent = false;

	auto& tState = m_tLatency.m_aSubChannels[m_tLatency.m_iPacketIndex];
	for (int iStream = iFirstStream; iStream < MAX_STREAMS; iStream++)
	{
		if (iStream != iFirstStream && !tBuffer.ReadOneBit())
			continue;

		auto& tPeek = m_tLatency.m_aPeeks[iStream];
		if (!PeekBlock(pNetChan, tBuffer, iStream, tPeek))
		{
			bIdentical = false;
			break;
		}

		bPresent = true;
		bIdentical = bIdentical && tPeek.m_tBlock == tState.m_aStreams[iStream];
	}

	tBuffer.m_iCurBit = iSaved;
	tBuffer.m_bOverflow = false;

	if (!bPresent || !bIdentical || !IsCopy(pNetChan, tState))
		return false;

	if (++tState.m_iCopies > LATENCY_COPY_LIMIT)
	{
		tState.m_iCopies = 0;
		return false;
	}

	return true;
}

bool CBacktrack::SkipBlock(CNetChannel* pNetChan, bf_read& tBuffer, int iStream)
{
	if (m_tLatency.m_bPacketOpen)
	{
		m_tLatency.m_bPacketOpen = false;

		const int iSaved = tBuffer.m_iCurBit;
		tBuffer.Seek(iSaved - 3 - (iStream + 1));
		m_tLatency.m_iPacketIndex = int(tBuffer.ReadUBitLong(3));
		tBuffer.Seek(iSaved);

		m_tLatency.m_bPacketCopy = PeekPacket(pNetChan, tBuffer, iStream);
	}

	if (!m_tLatency.m_bPacketCopy || !m_tLatency.m_aPeeks[iStream].m_bPresent)
		return false;

	tBuffer.Seek(tBuffer.m_iCurBit + m_tLatency.m_aPeeks[iStream].m_iBits);
	return true;
}

void CBacktrack::AcceptBlock(int iStream)
{
	if (m_tLatency.m_iPacketIndex < 0 || !m_tLatency.m_aPeeks[iStream].m_bPresent)
		return;

	auto& tState = m_tLatency.m_aSubChannels[m_tLatency.m_iPacketIndex];
	tState.m_aStreams[iStream] = m_tLatency.m_aPeeks[iStream].m_tBlock;
	tState.m_bFlipPending = true;
	tState.m_nFlipSequenceNr = std::numeric_limits<int>::max();
	tState.m_nPreviousSequenceNr = -1;
	tState.m_iCopies = 0;
}

void CBacktrack::CacheDrawInfo(CTFPlayer* pLocal)
{
	auto pResource = H::Entities.GetResource();
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ping)
		|| !pLocal || !pLocal->IsAlive() || !pResource || !pNetChan)
	{
		m_tDrawCache.Set({});
		return;
	}

	static float flFakeLatency = 0.f;
	{
		static Timer tTimer = {};
		if (tTimer.Run(0.5f))
			flFakeLatency = GetFakeLatency();
	}
	float flFakeLerp = GetFakeInterp() > G::Lerp ? GetFakeInterp() : 0.f;

	float flFake = std::min(flFakeLatency + flFakeLerp, m_flMaxUnlag) * 1000;
	float flLatency = std::max(pNetChan->GetLatency(FLOW_INCOMING) + pNetChan->GetLatency(FLOW_OUTGOING) - flFakeLatency, 0.f) * 1000;
	int iLatencyScoreboard = pResource->m_iPing(I::EngineClient->GetLocalPlayer());

	DrawCache_t tCache = {};
	if (flFake || Vars::Backtrack::Interp.Value > G::Lerp * 1000)
		tCache.m_sPingText = std::format("Ping {:.0f} (+ {:.0f}) ms", flLatency, flFake);
	else
		tCache.m_sPingText = std::format("Ping {:.0f} ms", flLatency);
	tCache.m_sScoreboardText = std::format("Scoreboard {} ms", iLatencyScoreboard);
	tCache.m_bValid = true;
	m_tDrawCache.Set(std::move(tCache));
}

void CBacktrack::Draw()
{
	static IndicatorAnim_t tAnim;
	static DrawCache_t s_tLastValidCache;

	const DrawCache_t tCache = m_tDrawCache.Get();
	if (tCache.m_bValid)
		s_tLastValidCache = tCache;

	const bool bEnabled = (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ping) != 0;
	const bool bVisible = bEnabled && I::EngineClient->IsInGame() && tCache.m_bValid;

	bool bOpening = true;
	const float flAnim = tAnim.Update(bVisible, bOpening);
	if (!tAnim.ShouldDraw(bVisible))
		return;

	const auto& tDraw = tCache.m_bValid ? tCache : s_tLastValidCache;

	int x = Vars::Menu::PingDisplay.Value.x;
	int y = Vars::Menu::PingDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.IndicatorScale(1);
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.IndicatorScale(50, Scale_Round))
	{
		x -= H::Draw.IndicatorScale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.IndicatorScale(50, Scale_Round))
	{
		x += H::Draw.IndicatorScale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const int iVtxStart = pDrawList->VtxBuffer.Size;
	const int iCmdStart = pDrawList->CmdBuffer.Size;

	DrawIndicatorText(pDrawList, x, y, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, tDraw.m_sPingText);
	DrawIndicatorText(pDrawList, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, tDraw.m_sScoreboardText);

	TransformIndicatorVertices(pDrawList, iVtxStart, pDrawList->VtxBuffer.Size, iCmdStart, pDrawList->CmdBuffer.Size, flAnim, bOpening, ImGui::GetIO().DisplaySize);
}
