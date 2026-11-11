#pragma once
#include "../../SDK/SDK.h"
#include "../ImGui/IndicatorCache.h"
#include <optional>

struct PacketRecord_t
{
	int m_nSequenceNr = -1;
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
	Vec3 m_vAngles = {};
	Vec3 m_vVelocity = {};
	float m_flCycle = 0.f;
	float m_flPlaybackRate = 1.f;
	matrix3x4 m_aBones[MAXSTUDIOBONES];
};

class CBacktrack
{
private:
	void MakeRecords();
	void CleanRecords();

	void SyncLatency(CNetChannel* pNetChan);
	float GetFakeTarget(CNetChannel* pNetChan);
	const PacketRecord_t* ChoosePacket(double dNow, float flTarget);
	void PushLatencySample(double dDelay);
	bool PeekBlock(CNetChannel* pNetChan, bf_read& tBuffer, int iStream, BlockPeek_t& tOut);
	bool PeekPacket(CNetChannel* pNetChan, bf_read& tBuffer, int iFirstStream);
	bool IsCopy(CNetChannel* pNetChan, SubChannel_t& tState);

	std::unordered_map<CBaseEntity*, std::deque<TickRecord>> m_mRecords = {};
	std::unordered_map<int, bool> m_mDidShoot = {};

	FakeLatency_t m_tLatency = {};
	float m_flMaxUnlag = 1.f;
	float m_flFakeLatency = 0.f;
	float m_flFakeInterp = 0.015f;

	bool m_bSettingUpBones = false;

	struct DrawCache_t
	{
		std::string m_sPingText = {};
		std::string m_sScoreboardText = {};
		bool m_bValid = false;
	};
	CIndicatorCache<DrawCache_t> m_tDrawCache = {};
public:
	void Store();
	void CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void SendLerp();
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();
	void Reset();

	bool GetRecords(CBaseEntity* pEntity, std::vector<TickRecord*>& vReturn);
	std::vector<TickRecord*> GetValidRecords(std::vector<TickRecord*>& vRecords, CTFPlayer* pLocal = nullptr, bool bDistance = false, float flTimeMod = 0.f);
	matrix3x4* GetBones(CBaseEntity* pEntity);

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
