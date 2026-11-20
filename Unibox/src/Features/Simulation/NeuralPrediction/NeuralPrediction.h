#pragma once
#include "../../../SDK/SDK.h"
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

struct NeuralKnot_t
{
	float m_flWishFwd = 0.f;
	float m_flWishSide = 0.f;
	float m_flTurnRateDeg = 0.f;
	float m_flJumpProb = 0.f;
	float m_flDuckProb = 0.f;
	float m_flSigmaXY = 32.f;
	float m_flSigmaZ = 16.f;
	float m_flCommitProb = 0.f;
};

struct NeuralHorizon_t
{
	static constexpr int kNumKnots = 16;
	static constexpr int kKnots[kNumKnots] = { 1, 2, 4, 6, 9, 12, 16, 21, 27, 34, 42, 52, 64, 78, 96, 120 };

	std::array<NeuralKnot_t, kNumKnots> m_aKnots = {};
	float m_flSimTime = -1.f;
	float m_flBaseYaw = 0.f;
	uint32_t m_uSteamId32 = 0;
	bool m_bValid = false;

	NeuralKnot_t SampleAtTick(int iTick) const
	{
		if (iTick <= kKnots[0])
			return m_aKnots[0];
		if (iTick >= kKnots[kNumKnots - 1])
			return m_aKnots[kNumKnots - 1];

		for (int k = 0; k < kNumKnots - 1; ++k)
		{
			if (iTick >= kKnots[k] && iTick <= kKnots[k + 1])
			{
				const float t = float(iTick - kKnots[k]) / float(kKnots[k + 1] - kKnots[k]);
				const auto& a = m_aKnots[k];
				const auto& b = m_aKnots[k + 1];
				NeuralKnot_t out;
				out.m_flWishFwd = a.m_flWishFwd + (b.m_flWishFwd - a.m_flWishFwd) * t;
				out.m_flWishSide = a.m_flWishSide + (b.m_flWishSide - a.m_flWishSide) * t;
				out.m_flTurnRateDeg = a.m_flTurnRateDeg + (b.m_flTurnRateDeg - a.m_flTurnRateDeg) * t;
				out.m_flJumpProb = a.m_flJumpProb + (b.m_flJumpProb - a.m_flJumpProb) * t;
				out.m_flDuckProb = a.m_flDuckProb + (b.m_flDuckProb - a.m_flDuckProb) * t;
				out.m_flSigmaXY = a.m_flSigmaXY + (b.m_flSigmaXY - a.m_flSigmaXY) * t;
				out.m_flSigmaZ = a.m_flSigmaZ + (b.m_flSigmaZ - a.m_flSigmaZ) * t;
				out.m_flCommitProb = a.m_flCommitProb + (b.m_flCommitProb - a.m_flCommitProb) * t;
				return out;
			}
		}
		return m_aKnots[kNumKnots - 1];
	}
};

struct ShotFeedback_t
{
	int m_iEntIndex = 0;
	uint32_t m_uSteamId32 = 0;
	int m_iStartTick = 0;
	int m_iTargetTick = 0;
	int m_iLateTicks = 0;
	Vec3 m_vPredicted = {};
	Vec3 m_vActual = {};
	float m_flHullGap = -1.f;
	bool m_bTargetAlive = true;
	bool m_bHasSpread = false;
	bool m_bOccluded = false;
	bool m_bNeuralUsed = false;
	bool m_bHitEvent = false;
	float m_flTargetEyeYaw = 0.f;
};

struct NeuralBaseline_t
{
	int m_iGroundModel = 0;
	float m_flGroundYaw = 0.f;
	float m_flGroundScale = 1.f;
	float m_flGroundTurn = 0.f;
	int m_iAirModel = 0;
	float m_flAirYaw = 0.f;
	float m_flAirScale = 1.f;
	float m_flAirTurn = 0.f;
	int m_iHopTicks = -1;
	int m_iGroundTicks = 0;
};

class CNeuralPrediction
{
public:
	static constexpr int kSeqLen = 32;
	static constexpr int kSeqDim = 22;
	static constexpr int kCtxDim = 44;
	static constexpr int kOutSteps = 16;
	static constexpr int kOutDims = 8;
	static constexpr int kMaxPlayers = 65;

	struct ObsFrame_t
	{
		uint32_t m_uSteamId32 = 0;
		int m_iServerTick = 0;
		float m_flSimTime = 0.f;
		Vec3 m_vOrigin = {};
		Vec3 m_vVelocity = {};
		float m_flEyePitch = 0.f;
		float m_flEyeYaw = 0.f;
		float m_flWishFwd = 0.f;
		float m_flWishSide = 0.f;
		float m_flMaxSpeed = 300.f;
		float m_flGroundDist = 512.f;
		Vec3 m_vGroundNormal = {};
		float m_flCeilingDist = 512.f;
		std::array<float, 8> m_aWallDists = {};
		std::array<float, 8> m_aLedgeDepths = {};
		int m_iFlags = 0;
		int m_iHealth = 100;
		int m_iMaxHealth = 100;
		uint8_t m_uClassId = 0;
		uint8_t m_uWaterLevel = 0;
		uint8_t m_uAirDash = 0;
		uint8_t m_uAirDucked = 0;
		uint8_t m_uJumping = 0;
		uint8_t m_uBlast = 0;
		uint8_t m_uCharge = 0;
		uint8_t m_uPara = 0;
		uint8_t m_uLostFooting = 0;
		uint8_t m_uWallContact = 0;
	};

	struct SnapshotSlot_t
	{
		std::atomic_flag m_tLock = ATOMIC_FLAG_INIT;
		std::atomic<bool> m_bDirty{ false };
		std::array<float, kSeqLen * kSeqDim> m_aXSeq = {};
		std::array<float, kCtxDim> m_aXCtx = {};
		float m_flSimTime = -1.f;
		float m_flEyeYaw = 0.f;
		uint32_t m_uSteamId32 = 0;
	};

	struct PlayerPubSlot_t
	{
		std::array<NeuralHorizon_t, 2> m_aBuf = {};
		std::atomic<int> m_iIdx{ 0 };
		std::atomic<uint64_t> m_uSeq{ 0 };
	};

	struct SteamAdapt_t
	{
		float m_flBiasFwd = 0.f;
		float m_flBiasSide = 0.f;
		float m_flBiasTurn = 0.f;
		float m_flScaleMult = 1.f;
		float m_flEmaErr = 0.f;
		int m_iSamples = 0;
	};

	struct ObsEvent_t
	{
		int m_iEntIndex = 0;
		ObsFrame_t m_tFrame = {};
		bool m_bHasSnapshot = false;
		std::array<float, kSeqLen * kSeqDim> m_aXSeq = {};
		std::array<float, kCtxDim> m_aXCtx = {};
		NeuralBaseline_t m_tBaseline = {};
	};

private:
	void InferenceWorkerLoop(std::stop_token tStop);
	void LearningWorkerLoop(std::stop_token tStop);

	std::array<std::deque<ObsFrame_t>, kMaxPlayers> m_aHist = {};
	std::array<SnapshotSlot_t, kMaxPlayers> m_aMailbox = {};
	std::array<PlayerPubSlot_t, kMaxPlayers> m_aPublished = {};

	std::mutex m_mtxObsQueue;
	std::deque<ObsEvent_t> m_qObsEvents;

	std::mutex m_mtxShotQueue;
	std::deque<ShotFeedback_t> m_qShotEvents;

	std::mutex m_mtxReset;
	std::array<bool, kMaxPlayers> m_aPendingReset = {};

	std::mutex m_mtxAdapt;
	std::unordered_map<uint32_t, SteamAdapt_t> m_mSteamAdapt;

	std::mutex m_mtxWeights;
	std::vector<float> m_vPromotedPlastic;
	std::atomic<uint64_t> m_uWeightVersion{ 0 };

	std::atomic<uint32_t> m_uInferWake{ 0 };
	std::atomic<bool> m_bFlushCheckpoint{ false };
	std::atomic<bool> m_bInitialized{ false };
	std::atomic<bool> m_bRunning{ false };

	std::atomic<float> m_flNeuralEmaErr{ 0.f };
	std::atomic<float> m_flAnalyticEmaErr{ 0.f };
	std::atomic<int> m_iValidationSamples{ 0 };
	std::atomic<int> m_iTrainSteps{ 0 };

	std::jthread m_tInferThread;
	std::jthread m_tTrainThread;

public:
	void Initialize();
	void Shutdown();
	void OnLevelChange();

	void ResetPlayer(int iEntIndex);
	void OnPlayerUpdate(CTFPlayer* pPlayer, const Vec3& vOrigin, const Vec3& vVelocity, float flSimTime, float flEyeYaw, float flMaxSpeed, bool bDucked, const NeuralBaseline_t& tBaseline);
	void OnPlayerEvent(int iEntIndex, bool bDied);
	void RecordShotFeedback(const ShotFeedback_t& tFeedback);

	bool GetPrediction(int iEntIndex, uint32_t uSteamId32, NeuralHorizon_t& tOut) const;
	bool ShouldFallback() const;

	float GetNeuralEmaError() const { return m_flNeuralEmaErr.load(std::memory_order_relaxed); }
	float GetAnalyticEmaError() const { return m_flAnalyticEmaErr.load(std::memory_order_relaxed); }
	int GetValidationSamples() const { return m_iValidationSamples.load(std::memory_order_relaxed); }
	int GetTrainSteps() const { return m_iTrainSteps.load(std::memory_order_relaxed); }
};

ADD_FEATURE(CNeuralPrediction, NeuralPred);
