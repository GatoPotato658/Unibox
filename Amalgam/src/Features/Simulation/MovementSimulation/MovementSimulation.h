#pragma once
#include "../../../SDK/SDK.h"
#include <functional>

Enum(Move, Ground, Air, Swim)

using RunTickCallback = const std::function<void(CMoveData&)>;

struct InputEstimate
{
	float m_flAlpha = 0.f;
	float m_flWishSpeed = 0.f;
	float m_flEyeYaw = 0.f;
	float m_flViewRate = 0.f;
	float m_flKeyOffset = 0.f;
	bool m_bKeyInput = false;
	bool m_bValid = false;
};

struct MoveStorage
{
	CTFPlayer* m_pPlayer = nullptr;
	CMoveData m_MoveData = {};
	byte* m_pData = nullptr;

	float m_flAverageYaw = 0.f;
	InputEstimate m_tGroundInput = {};
	InputEstimate m_tAirInput = {};
	float m_flViewYaw = 0.f;
	bool m_bBunnyHop = false;

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	bool m_bDirectMove = true;

	bool m_bPredictNetworked = true;
	Vec3 m_vPredictedOrigin = {};

	std::vector<Vec3> m_vPath = {};

	bool m_bFailed = false;
	bool m_bInitFailed = false;

	int m_iTick = 0;
	bool m_bCached = false;
	int m_iSubstepCounter = 0;
};

struct MoveData
{
	Vec3 m_vDirection = {};
	float m_flSimTime = 0.f;
	int m_iMode = 0;
	Vec3 m_vVelocity = {};
	Vec3 m_vOrigin = {};
	bool m_bInputDirection = false;
	float m_flEyeYaw = 0.f;
};

struct SimTickRecord
{
	Vec3 m_vOrigin = {};
	Vec3 m_vVelocity = {};
	Vec3 m_vPredictedOrigin = {};
	float m_flSimTime = 0.f;
	bool m_bPredictNetworked = true;
	bool m_bDirectMove = true;
};

struct TrajectoryCache
{
	int m_iTickCount = -1;
	float m_flSimulationTime = -1.f;
	Vec3 m_vRequestedOrigin = {};
	bool m_bRequestedOrigin = false;
	bool m_bPredict = true;
	bool m_bStrafe = true;
	bool m_bHitchance = true;
	bool m_bInitResult = true;
	int m_iInitTicks = 0;
	Vec3 m_vInitialOrigin = {};
	Vec3 m_vInitialVelocity = {};
	bool m_bFailed = false;
	float m_flAverageYaw = 0.f;
	InputEstimate m_tGroundInput = {};
	InputEstimate m_tAirInput = {};
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	bool m_bDirectMove = true;
	std::vector<SimTickRecord> m_vTicks = {};
};

class CMovementSimulation
{
private:
	void Store(MoveStorage& tMoveStorage);
	void Reset(MoveStorage& tMoveStorage);

	void SetupMoveData(MoveStorage& tMoveStorage, const Vec3* pvOrigin = nullptr);
	bool CheckStuck(MoveStorage& tMoveStorage);
	void GetAverageYaw(MoveStorage& tMoveStorage, int iSamples);
	InputEstimate GetInputEstimate(MoveStorage& tMoveStorage, int iMode, int iSamples);
	bool StrafePrediction(MoveStorage& tMoveStorage, bool bStrafe = true, bool bHitchance = false);

	static constexpr int MaxChokeTicks = 33;
	static constexpr int MaxLatencyTicks = 66;

	void SetBounds(CTFPlayer* pPlayer);
	void RestoreBounds(CTFPlayer* pPlayer);

	bool m_bOldInPrediction = false;
	bool m_bOldFirstTimePredicted = false;
	float m_flOldFrametime = 0.f;

	std::unordered_map<int, std::deque<MoveData>> m_mRecords = {};
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};
	std::unordered_map<int, TrajectoryCache> m_mTrajectoryCache = {};

public:
	void Store();
	void StorePlayer(CTFPlayer* pPlayer, CMoveData& tMoveData, float flTime);

	bool Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance = true, bool bStrafe = true, bool bPredict = true, const Vec3* pvOrigin = nullptr);
	bool SetDuck(MoveStorage& tMoveStorage, bool bDuck);
	void RunTick(MoveStorage& tMoveStorage, bool bPath = true, RunTickCallback* pCallback = nullptr);
	void RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback fCallback);
	void Restore(MoveStorage& tMoveStorage);

	float GetPredictedDelta(CBaseEntity* pEntity);

	float GetAccuracy() const;
	float GetMaxSimulationTime(float flDesiredMaxTime) const;
	int GetStrafeSamples(int iBaseSamples) const;
	int GetSubstep(int iCurrentSimTick) const;
	int GetMaxTargetCandidates(int iConfiguredMax) const;
	int GetChoke(int iIndex) const;
	int GetLatency(int iLeadTicks, int iChoke) const;
	void ClearCache();
};

ADD_FEATURE(CMovementSimulation, MoveSim);
