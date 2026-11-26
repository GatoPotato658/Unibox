#pragma once
#include "../../SDK/SDK.h"
#include "../ImGui/IndicatorCache.h"
#include <optional>

struct PacketRecord_t
{
	int m_nSequenceNr = -1;
	int m_nReliableState = 0;
	int m_iFlip = -1;
	double m_dTime = 0.0;
};

struct ReliableBlock_t
{
	uint64_t m_uHash = 0;
	int m_iStart = 0;
	int m_iNum = 0;
	int m_iLength = 0;
	bool m_bValid = false;

	bool operator==(const ReliableBlock_t& tOther) const
	{
		return m_bValid && tOther.m_bValid && m_uHash == tOther.m_uHash && m_iStart == tOther.m_iStart
			&& m_iNum == tOther.m_iNum && m_iLength == tOther.m_iLength;
	}
};

struct BlockPeek_t
{
	bool m_bPresent = false;
	int m_iBits = 0;
	ReliableBlock_t m_tBlock = {};
};

struct SubChannel_t
{
	std::array<ReliableBlock_t, MAX_STREAMS> m_aStreams = {};
	int m_nFlipSequenceNr = std::numeric_limits<int>::max();
	int m_nPreviousSequenceNr = -1;
	int m_iCopies = 0;
	bool m_bFlipPending = false;
};

struct FakeLatency_t
{
	CNetChannel* m_pNetChan = nullptr;
	std::deque<PacketRecord_t> m_dPackets = {};
	std::deque<double> m_dSamples = {};
	int m_nPresentedSequenceNr = -1;
	int m_nRealInSequenceNr = 0;
	int m_nRealInReliableState = 0;
	bool m_bFaked = false;
	double m_dLastFaked = -1.0;
	int m_nSendingSequenceNr = -1;
	int m_nLastSentSequenceNr = -1;
	std::array<SubChannel_t, MAX_SUBCHANNELS> m_aSubChannels = {};

	int m_iPacketIndex = -1;
	int m_nReliableBefore = 0;
	int m_nPreviousAck = -1;
	bool m_bPacketOpen = false;
	bool m_bPacketHeader = false;
	bool m_bPacketCopy = false;
	std::array<BlockPeek_t, MAX_STREAMS> m_aPeeks = {};
};

struct HitboxInfo_t
{
	int m_iBone = -1, m_nHitbox = -1;
	Vec3 m_vCenter = {};
	Vec3 m_vMin = {}, m_vMax = {};
};

struct TickRecord
{
	float m_flSimTime = 0.f;
	Vec3 m_vOrigin = {};
	Vec3 m_vMins = {};
	Vec3 m_vMaxs = {};
	bool m_bOnShot = false;
	bool m_bInvalid = false;
	Vec3 m_vVelocity = {};
	float m_flModelScale = 1.f;
	matrix3x4 m_aBones[MAXSTUDIOBONES];
	std::shared_ptr<std::array<matrix3x4, MAXSTUDIOBONES>> m_pRenderBones = {};

	matrix3x4* GetRenderBones() { return m_pRenderBones ? m_pRenderBones->data() : m_aBones; }
};

struct PlayerTrack_t
{
	CBaseEntity* m_pEntity = nullptr;
	int m_iEntIndex = -1;
	std::vector<TickRecord> m_vRing = {};
	int m_iHead = 0;
	int m_iCount = 0;

	void Clear()
	{
		m_iHead = 0;
		m_iCount = 0;
	}

	void Reset()
	{
		m_pEntity = nullptr;
		m_iEntIndex = -1;
		m_vRing.clear();
		m_iHead = 0;
		m_iCount = 0;
	}

	bool Empty() const { return m_iCount == 0; }
	int Size() const { return m_iCount; }

	TickRecord& At(int iIndex)
	{
		const int iCap = int(m_vRing.size());
		return m_vRing[(m_iHead + iIndex) % iCap];
	}

	const TickRecord& At(int iIndex) const
	{
		const int iCap = int(m_vRing.size());
		return m_vRing[(m_iHead + iIndex) % iCap];
	}

	TickRecord& Front() { return m_vRing[m_iHead]; }
	const TickRecord& Front() const { return m_vRing[m_iHead]; }

	TickRecord& Back() { return At(m_iCount - 1); }
	const TickRecord& Back() const { return At(m_iCount - 1); }

	void PopBack()
	{
		if (m_iCount > 0)
			m_iCount--;
	}

	void PopFront()
	{
		if (m_iCount <= 0)
			return;
		m_iHead = (m_iHead + 1) % int(m_vRing.size());
		m_iCount--;
	}

	void Resize(int iNewCount)
	{
		if (iNewCount < m_iCount)
			m_iCount = std::max(0, iNewCount);
	}

	TickRecord& PushFrontSlot(int iDesiredCap)
	{
		int iCap = int(m_vRing.size());
		if (iCap == 0)
		{
			m_vRing.resize(std::max(iDesiredCap, 4));
			iCap = int(m_vRing.size());
			m_iHead = 0;
			m_iCount = 1;
			return m_vRing[0];
		}
		if (m_iCount == iCap)
		{
			const int iNewCap = std::max(iCap * 2, iDesiredCap);
			std::vector<TickRecord> vNew(iNewCap);
			for (int i = 0; i < m_iCount; i++)
				vNew[i + 1] = std::move(At(i));
			m_vRing = std::move(vNew);
			m_iHead = 0;
			m_iCount++;
			return m_vRing[0];
		}
		m_iHead = (m_iHead - 1 + iCap) % iCap;
		m_iCount++;
		return m_vRing[m_iHead];
	}
};

class CBacktrack
{
private:
	static constexpr int MAX_TRACK_PLAYERS = 102;

	PlayerTrack_t* FindTrack(CBaseEntity* pEntity);
	PlayerTrack_t& GetOrCreateTrack(CBaseEntity* pEntity);

	void MakeRecords();
	void CleanRecords();

	void SyncLatency(CNetChannel* pNetChan);
	float GetFakeTarget(CNetChannel* pNetChan);
	const PacketRecord_t* ChoosePacket(double dNow, float flTarget);
	void PushLatencySample(double dDelay);
	bool PeekBlock(CNetChannel* pNetChan, bf_read& tBuffer, int iStream, BlockPeek_t& tOut);
	bool PeekPacket(CNetChannel* pNetChan, bf_read& tBuffer, int iFirstStream);
	bool IsCopy(CNetChannel* pNetChan, SubChannel_t& tState);

	std::array<PlayerTrack_t, MAX_TRACK_PLAYERS> m_aTracks = {};
	std::unordered_map<CBaseEntity*, PlayerTrack_t> m_mExtraTracks = {};
	std::unordered_map<int, bool> m_mDidShoot = {};

	FakeLatency_t m_tLatency = {};
	float m_flMaxUnlag = 1.f;
	float m_flFakeLatency = 0.f;
	float m_flFakeInterp = 0.015f;
	float m_flKeepWindow = 1.f;
	bool m_bAnyConsumer = true;

	bool m_bSettingUpBones = false;

	struct DrawCache_t
	{
		std::string m_sPingText = {};
		std::string m_sScoreboardText = {};
		bool m_bValid = false;
	};
	CIndicatorCache<DrawCache_t> m_tDrawCache = {};
	std::string m_sLastPingText = {};
	std::string m_sLastScoreboardText = {};
	bool m_bLastDrawValid = false;
public:
	void Store();
	void CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void SendLerp();
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();
	void Reset();

	bool GetRecords(CBaseEntity* pEntity, std::vector<TickRecord*>& vReturn);
	void GetValidRecords(CBaseEntity* pEntity, std::vector<TickRecord*>& vReturn, CTFPlayer* pLocal = nullptr, bool bDistance = false, float flTimeMod = 0.f);
	void GetValidRecords(const std::vector<TickRecord*>& vRecords, std::vector<TickRecord*>& vReturn, CTFPlayer* pLocal = nullptr, bool bDistance = false, float flTimeMod = 0.f);
	std::vector<TickRecord*> GetValidRecords(std::vector<TickRecord*>& vRecords, CTFPlayer* pLocal = nullptr, bool bDistance = false, float flTimeMod = 0.f);
	TickRecord* GetFirstValidRecord(CBaseEntity* pEntity);
	TickRecord* GetLatestRecord(CBaseEntity* pEntity);
	matrix3x4* GetBones(CBaseEntity* pEntity);
	matrix3x4* GetBones(CBaseEntity* pEntity, float flOldCurrentTime, float flLerp);

	float GetReal(int iFlow = MAX_FLOWS, bool bNoFake = true);
	float GetWishFake();
	float GetWishLerp();
	float GetFakeLatency();
	float GetFakeInterp();
	float GetWindow();
	void SetLerp();
	int GetServerTick();
	int GetRewindTick(float flTimeMod = 0.f);
	int GetAnticipatedChoke(int iMethod = Vars::Aimbot::General::AimType.Value);

	void ResolverUpdate(CBaseEntity* pEntity);
	void ReportShot(int iIndex);
	void AdjustPing(CNetChannel* pNetChan);
	void RestorePing(CNetChannel* pNetChan, bool bSent);

	void PacketStart(CNetChannel* pNetChan, bool bHasHeader);
	void PacketEnd(CNetChannel* pNetChan, double dReceived);
	bool IsFiltering(CNetChannel* pNetChan);
	bool SkipBlock(CNetChannel* pNetChan, bf_read& tBuffer, int iStream);
	void AcceptBlock(int iStream);

	bool IsSettingUpBones() { return m_bSettingUpBones; }
	void SetSettingUpBones(bool bSettingUpBones) { m_bSettingUpBones = bSettingUpBones; }

	int m_iTickCount = 0;
	float m_flSentInterp = -1.f;

	TickRecord m_tRecord = {};
};

ADD_FEATURE(CBacktrack, Backtrack);
