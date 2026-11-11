#pragma once
#include "../../../SDK/SDK.h"
#include <functional>

Enum(Move, Ground, Air, Swim)
Enum(MoveModel, None, Input, Stop, World, View, Sweep, Charge)

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

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	bool m_bPredictNetworked = true;
	Vec3 m_vPredictedOrigin = {};

	std::vector<Vec3> m_vPath = {};

	bool m_bFailed = false;
	bool m_bInitFailed = false;
};

class CMovementSimulation
{
private:
	void StoreRecord(CTFPlayer* pPlayer, std::deque<MoveData>& vRecords, MoveData tRecord);
	const MoveFit_t& GetFit(CTFPlayer* pPlayer, bool bStrafe);

	void SetupPhysics(MoveStorage& tMoveStorage);
	void SetupMoveData(MoveStorage& tMoveStorage, const Vec3* pvOrigin);
	void SetupController(MoveStorage& tMoveStorage, bool bStrafe);

	std::unordered_map<int, std::deque<MoveData>> m_mRecords = {};
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};
	std::unordered_map<int, MoveFit_t> m_mFits = {};

public:
	void Store();
	void StorePlayer(CTFPlayer* pPlayer, CMoveData& tMoveData, float flTime);

	bool Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance = true, bool bStrafe = true, bool bPredict = true, const Vec3* pvOrigin = nullptr);
	void RunTick(MoveStorage& tMoveStorage, bool bPath = true, RunTickCallback* pCallback = nullptr);
	void RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback fCallback);

	float GetPredictedDelta(CBaseEntity* pEntity);
	float GetMaxSimulationTime(float flDesiredMaxTime) const { return flDesiredMaxTime; }
};

ADD_FEATURE(CMovementSimulation, MoveSim);
