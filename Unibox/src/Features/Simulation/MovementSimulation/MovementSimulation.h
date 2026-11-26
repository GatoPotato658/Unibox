#pragma once
#include "../../../SDK/SDK.h"
#include "../NeuralPrediction/NeuralPrediction.h"
#include <functional>
#include <utility>

Enum(Move, Ground, Air, Swim)
Enum(MoveModel, None, Input, Stop, World, View, Sweep, Charge, Neural)

using RunTickCallback = const std::function<void(CMoveData&)>;

struct MoveData
{
	Vec3 m_vDirection = {};
	float m_flSimTime = 0.f;
	int m_iMode = MoveEnum::Ground;
	Vec3 m_vVelocity = {};
	Vec3 m_vOrigin = {};
	bool m_bInputDirection = false;
	int m_iTicks = 1;
	float m_flEyeYaw = 0.f;
	float m_flMaxSpeed = 0.f;
	bool m_bDucked = false;
	bool m_bWall = false;
};

struct MoveController_t
{
	int m_iModel = MoveModelEnum::None;
	float m_flYaw = 0.f;
	float m_flScale = 1.f;
	float m_flTurn = 0.f;
	float m_flConfidence = 0.f;
};

struct MoveFit_t
{
	float m_flSimTime = -1.f;
	MoveController_t m_tGround = {};
	MoveController_t m_tAir = {};
	bool m_bGroundCurrent = false;
	bool m_bAirCurrent = false;
	int m_iHopTicks = -1;
	int m_iGroundTicks = 0;
	int m_iMaxHops = 0;
};

struct MovePhysics_t
{
	float m_flGravity = 800.f;
	float m_flFriction = 4.f;
	float m_flStopSpeed = 100.f;
	float m_flAccelerate = 10.f;
	float m_flAirAccelerate = 10.f;
	float m_flMaxVelocity = 3500.f;
	float m_flStepSize = 18.f;
	float m_flParachuteXY = 300.f;
	float m_flParachuteZ = -100.f;
	float m_flAirCurrentFriction = 1.f;
	float m_flAirCurrentControl = 1.f;
	float m_flRestick = 200.f;
	float m_flLostFootingFriction = 0.1f;
	float m_flClampBack = 0.9f;
	float m_flClampBackMin = 100.f;
	float m_flChargeSpeed = 750.f;
	float m_flAirCap = 30.f;
};

struct MoveStorage
{
	CTFPlayer* m_pPlayer = nullptr;
	CMoveData m_MoveData = {};

	MovePhysics_t m_tPhysics = {};
	MoveController_t m_tGround = {};
	MoveController_t m_tAir = {};
	float m_flViewYaw = 0.f;
	float m_flSweepKept = 1.f;

	Vec3 m_vMins = {};
	Vec3 m_vMaxs = {};
	Vec3 m_vViewOffset = {};
	int m_nMask = MASK_PLAYERSOLID;
	int m_iTeam = -1;
	bool m_bLocal = false;
	bool m_bOnGround = false;
	bool m_bDucked = false;
	int m_iWaterLevel = 0;
	float m_flSurfaceFriction = 1.f;
	bool m_bLostFooting = false;
	bool m_bAirCurrent = false;
	bool m_bParachute = false;
	bool m_bCharging = false;
	float m_flChargeMeter = 0.f;
	float m_flChargeDrain = 0.f;
	float m_flChargeExitSpeed = 0.f;
	float m_flChargeExitCap = 0.f;
	bool m_bBunnyHop = false;
	int m_iHopTicks = -1;
	int m_iGroundTicks = 0;
	int m_iMaxHops = 0;
	int m_iPredictedHops = 0;
	int m_iSimTicks = 0;

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	bool m_bPredictNetworked = true;
	Vec3 m_vPredictedOrigin = {};

	std::vector<Vec3> m_vPath = {};

	NeuralHorizon_t m_tNeural = {};
	bool m_bNeuralGround = false;
	bool m_bNeuralAir = false;
	int m_iNeuralStartTick = 0;
	float m_flNeuralBaseYaw = 0.f;
	int m_iNeuralStep = 0;

	bool m_bFailed = false;
	bool m_bInitFailed = false;
	bool m_bCached = false;
	int m_iCacheSlot = -1;
	int m_iCacheStep = 0;
};

class CMovementSimulation
{
private:
	struct MoveCacheStep_t
	{
		Vec3 m_vAbsOrigin = {};
		Vec3 m_vVelocity = {};
		Vec3 m_vPredictedOrigin = {};
		MoveController_t m_tGround = {};
		MoveController_t m_tAir = {};
		float m_flViewYaw = 0.f;
		float m_flSweepKept = 1.f;
		float m_flMaxSpeed = 0.f;
		float m_flAirCap = 30.f;
		float m_flChargeMeter = 0.f;
		float m_flSurfaceFriction = 1.f;
		int m_iWaterLevel = 0;
		int m_iHopTicks = -1;
		int m_iGroundTicks = 0;
		int m_iPredictedHops = 0;
		int m_iSimTicks = 0;
		bool m_bOnGround = false;
		bool m_bLostFooting = false;
		bool m_bAirCurrent = false;
		bool m_bCharging = false;
		float m_flSimTime = 0.f;
		float m_flPredictedSimTime = 0.f;
		bool m_bPredictNetworked = true;
		int m_iNeuralStep = 0;
	};

	struct MoveCacheEntry_t
	{
		int m_iEntIndex = 0;
		float m_flSimTime = 0.f;
		Vec3 m_vStartOrigin = {};
		bool m_bStrafe = true;
		bool m_bPredict = true;
		int m_iStrafeFlags = 0;
		int m_iChoke = 0;
		bool m_bStuckFailed = false;
		bool m_bFitted = false;
		float m_flConfidence = 0.f;
		MoveStorage m_tInitState = {};
		MoveStorage m_tFrontier = {};
		std::vector<MoveCacheStep_t> m_vSteps = {};
	};

	void StoreRecord(CTFPlayer* pPlayer, std::deque<MoveData>& vRecords, MoveData tRecord);
	const MoveFit_t& GetFit(CTFPlayer* pPlayer, bool bStrafe);

	void SetupPhysics(MoveStorage& tMoveStorage);
	void SetupMoveData(MoveStorage& tMoveStorage, const Vec3* pvOrigin);
	void SetupController(MoveStorage& tMoveStorage, bool bStrafe);
	void StepTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback* pCallback);
	static MoveCacheStep_t MakeCacheStep(const MoveStorage& tMoveStorage);
	static void ApplyCacheStep(MoveStorage& tMoveStorage, const MoveCacheStep_t& tStep, bool bPath);

	std::unordered_map<int, std::deque<MoveData>> m_mRecords = {};
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};
	std::unordered_map<int, MoveFit_t> m_mFits = {};
	int m_iCacheTick = -1;
	std::vector<MoveCacheEntry_t> m_vCache = {};

public:
	void Store();
	void StorePlayer(CTFPlayer* pPlayer, CMoveData& tMoveData, float flTime);

	bool Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance = true, bool bStrafe = true, bool bPredict = true, const Vec3* pvOrigin = nullptr);
	void RunTick(MoveStorage& tMoveStorage, bool bPath = true, RunTickCallback* pCallback = nullptr);
	void RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback fCallback);

	float GetPredictedDelta(CBaseEntity* pEntity);
	float GetMaxSimulationTime(float flDesiredMaxTime) const { return flDesiredMaxTime; }
	std::pair<float, float> GetUncertaintyAtTick(const MoveStorage& tMoveStorage, int iTick) const;
};

ADD_FEATURE(CMovementSimulation, MoveSim);
