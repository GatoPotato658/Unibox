#include "MovementSimulation.h"

#include "../../EnginePrediction/EnginePrediction.h"
#include "../../Aimbot/AimbotProjectile/ProjProfiler.h"
#include <numeric>

void CMovementSimulation::Store(MoveStorage& tMoveStorage)
{
	auto pMap = tMoveStorage.m_pPlayer->GetPredDescMap();
	if (!pMap)
		return;

	size_t iSize = tMoveStorage.m_pPlayer->GetIntermediateDataSize();
	tMoveStorage.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));

	CPredictionCopy copy = { PC_NETWORKED_ONLY, tMoveStorage.m_pData, PC_DATA_PACKED, tMoveStorage.m_pPlayer, PC_DATA_NORMAL };
	copy.TransferData("MovementSimulationStore", tMoveStorage.m_pPlayer->entindex(), pMap);
}

void CMovementSimulation::Reset(MoveStorage& tMoveStorage)
{
	if (tMoveStorage.m_pData)
	{
		auto pMap = tMoveStorage.m_pPlayer->GetPredDescMap();
		if (pMap)
		{
			CPredictionCopy copy = { PC_NETWORKED_ONLY, tMoveStorage.m_pPlayer, PC_DATA_NORMAL, tMoveStorage.m_pData, PC_DATA_PACKED };
			copy.TransferData("MovementSimulationReset", tMoveStorage.m_pPlayer->entindex(), pMap);
		}

		I::MemAlloc->Free(tMoveStorage.m_pData);
		tMoveStorage.m_pData = nullptr;
	}
}

static inline int GetMoveMode(CTFPlayer* pPlayer)
{
	if (pPlayer->IsSwimming())
		return MoveEnum::Swim;

	if (pPlayer->IsOnGround())
		return MoveEnum::Ground;

	return MoveEnum::Air;
}

// m_flMaxspeed is the server-computed TeamFortress_CalculateMaxSpeed result and is networked to
// every client, so it already carries weapon modifiers, conditions, runes and the MvM flag penalty.
// The client build of TeamFortress_CalculateMaxSpeed has no whip block, so SDK::MaxSpeed's
// approximation is only a fallback for a player we have no networked sample for.
static inline float GetPlayerMaxSpeed(CTFPlayer* pPlayer)
{
	float flMaxSpeed = pPlayer->m_flMaxspeed();
	if (flMaxSpeed <= 0.f)
		flMaxSpeed = SDK::MaxSpeed(pPlayer);
	return flMaxSpeed;
}

static inline float GetHeading(const Vec3& vVelocity)
{
	return Math::Rad2Deg(atan2f(vVelocity.y, vVelocity.x));
}

// Dormant players do not advance their simulation time, so storing them again would append
// duplicates that skew every per-tick average computed from the record history.
static inline MoveData* PushRecord(std::deque<MoveData>& vRecords, MoveData&& tRecord)
{
	if (!vRecords.empty() && vRecords.front().m_flSimTime == tRecord.m_flSimTime)
		return nullptr;

	vRecords.emplace_front(std::move(tRecord));
	if (vRecords.size() > 66)
		vRecords.pop_back();

	return &vRecords.front();
}

static inline void HandleMovement(CTFPlayer* pPlayer, MoveData* pLastRecord, MoveData& tCurRecord, std::deque<MoveData>& vRecords)
{
	bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();

	if (pLastRecord && !pPlayer->IsDormant())
	{

		{
			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter(pPlayer);
			SDK::TraceHull(pLastRecord->m_vOrigin, pLastRecord->m_vOrigin + pLastRecord->m_vVelocity * TICK_INTERVAL, pPlayer->m_vecMins() + PLAYER_ORIGIN_COMPRESSION, pPlayer->m_vecMaxs() - PLAYER_ORIGIN_COMPRESSION, pPlayer->SolidMask(), &filter, &trace);
			if (trace.DidHit() && trace.plane.normal.z < 0.707f)
			{
				pLastRecord = nullptr;
				vRecords.resize(1);
			}
		}
	}

	if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
	{
		G::DummyCmd.forwardmove = 520.f;
		G::DummyCmd.sidemove = 0.f;
		SDK::FixMovement(&G::DummyCmd, bLocal ? G::CurrentUserCmd->viewangles : pPlayer->GetEyeAngles(), {});
		tCurRecord.m_vDirection.x = G::DummyCmd.forwardmove;
		tCurRecord.m_vDirection.y = -G::DummyCmd.sidemove;
		return;
	}

	tCurRecord.m_bInputDirection = bLocal;
	switch (tCurRecord.m_iMode)
	{
	case MoveEnum::Ground:
		if (!bLocal)
			break;
		else if (Vars::Misc::Movement::AutoStrafe.Value || Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
			tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized2D() * 520.f, tCurRecord.m_bInputDirection = false;
		break;
	case MoveEnum::Air:
		if (!bLocal)
			break;
		else if (Vars::Misc::Movement::AutoStrafe.Value || tCurRecord.m_vDirection.To2D().IsZero() || pLastRecord && tCurRecord.m_vVelocity.To2D() == pLastRecord->m_vVelocity.To2D())
			tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized2D() * 520.f, tCurRecord.m_bInputDirection = false;
		break;
	case MoveEnum::Swim:
		if (!bLocal)
			tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized() * 520.f, tCurRecord.m_bInputDirection = false;
	}
}

void CMovementSimulation::Store()
{
	ClearCache();

	if (I::EngineClient->IsPlayingDemo())
		return;

	int iLocalIdx = I::EngineClient->GetLocalPlayer();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		auto& vRecords = m_mRecords[pPlayer->entindex()];
		if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->m_vecVelocity().IsZero())
		{
			vRecords.clear();
			continue;
		}
		else if (pPlayer->entindex() == iLocalIdx || !H::Entities.GetDeltaTime(pPlayer->entindex()) && !pPlayer->IsDormant())
			continue;

		Vec3 vVelocity = pPlayer->m_vecVelocity();
		Vec3 vOrigin = pPlayer->m_vecOrigin();
		Vec3 vDirection = vVelocity.To2D();

		MoveData* pLastRecord = !vRecords.empty() ? &vRecords.front() : nullptr;
		if (!PushRecord(vRecords, {
			vDirection,
			pPlayer->m_flSimulationTime(),
			GetMoveMode(pPlayer),
			vVelocity,
			vOrigin,
			{},
			pPlayer->GetEyeAngles().y
		}))
			continue;

		HandleMovement(pPlayer, pLastRecord, vRecords.front(), vRecords);
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		auto& vSimTimes = m_mSimTimes[pPlayer->entindex()];
		if (pEntity->entindex() == iLocalIdx || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
		{
			vSimTimes.clear();
			continue;
		}

		float flDeltaTime = H::Entities.GetDeltaTime(pPlayer->entindex());
		if (!flDeltaTime)
			continue;

		vSimTimes.push_front(flDeltaTime);
		if (vSimTimes.size() > Vars::Aimbot::Projectile::DeltaCount.Value)
			vSimTimes.pop_back();
	}
}

void CMovementSimulation::StorePlayer(CTFPlayer* pPlayer, CMoveData& tMoveData, float flTime)
{
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsDormant() || pPlayer->m_vecVelocity().IsZero())
	{
		vRecords.clear();
		return;
	}

	Vec3 vVelocity = tMoveData.m_vecVelocity;
	Vec3 vOrigin = tMoveData.m_vecAbsOrigin;
	Vec3 vDirection = Math::RotatePoint({ tMoveData.m_flForwardMove, -tMoveData.m_flSideMove, tMoveData.m_flUpMove }, {}, { 0, tMoveData.m_vecViewAngles.y, 0 });

	MoveData* pLastRecord = !vRecords.empty() ? &vRecords.front() : nullptr;
	if (!PushRecord(vRecords, {
		vDirection,
		flTime,
		GetMoveMode(pPlayer),
		vVelocity,
		vOrigin,
		{},
		tMoveData.m_vecViewAngles.y
	}))
		return;

	HandleMovement(pPlayer, pLastRecord, vRecords.front(), vRecords);
}

bool CMovementSimulation::Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance, bool bStrafe, bool bPredict, const Vec3* pvOrigin)
{
	PROJ_ZONE(MoveInit);

	if (!pEntity || !pEntity->IsPlayer() || !pEntity->As<CTFPlayer>()->IsAlive())
	{
		tMoveStorage.m_bInitFailed = tMoveStorage.m_bFailed = true;
		return false;
	}

	auto pPlayer = pEntity->As<CTFPlayer>();
	tMoveStorage.m_pPlayer = pPlayer;

	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		auto it = m_mTrajectoryCache.find(pPlayer->entindex());
		// The cached trajectory is only reusable for an identical request: Hitscan backtracks to a
		// different origin than the networked one, and Projectile may skip the choke prediction.
		if (it != m_mTrajectoryCache.end()
			&& it->second.m_iTickCount == I::GlobalVars->tickcount
			&& it->second.m_flSimulationTime == pPlayer->m_flSimulationTime()
			&& it->second.m_bPredict == bPredict
			&& it->second.m_bStrafe == bStrafe
			&& it->second.m_bHitchance == bHitchance
			&& it->second.m_bRequestedOrigin == (pvOrigin != nullptr)
			&& (!pvOrigin || it->second.m_vRequestedOrigin == *pvOrigin)
			&& (it->second.m_bFailed || !it->second.m_vTicks.empty()))
		{
			tMoveStorage.m_bCached = true;
			tMoveStorage.m_iTick = 0;
			tMoveStorage.m_bFailed = it->second.m_bFailed;
			tMoveStorage.m_flAverageYaw = it->second.m_flAverageYaw;
			tMoveStorage.m_tGroundInput = it->second.m_tGroundInput;
			tMoveStorage.m_tAirInput = it->second.m_tAirInput;
			tMoveStorage.m_flPredictedDelta = it->second.m_flPredictedDelta;
			tMoveStorage.m_flSimTime = pPlayer->m_flSimulationTime();
			tMoveStorage.m_flPredictedSimTime = it->second.m_flPredictedSimTime;
			tMoveStorage.m_bDirectMove = it->second.m_bDirectMove;
			tMoveStorage.m_MoveData.m_vecAbsOrigin = it->second.m_vInitialOrigin;
			tMoveStorage.m_MoveData.m_vecVelocity = it->second.m_vInitialVelocity;
			tMoveStorage.m_vPredictedOrigin = it->second.m_vInitialOrigin;
			tMoveStorage.m_vPath = { it->second.m_vInitialOrigin };
			if (it->second.m_bFailed)
				return it->second.m_bInitResult;

			for (int i = 0; i < it->second.m_iInitTicks; i++)
				RunTick(tMoveStorage);
			return true;
		}
	}

	tMoveStorage.m_bCached = false;
	tMoveStorage.m_bFailed = false;
	tMoveStorage.m_iTick = 0;
	tMoveStorage.m_iSubstepCounter = 0;

	m_bOldInPrediction = I::Prediction->m_bInPrediction;
	m_bOldFirstTimePredicted = I::Prediction->m_bFirstTimePredicted;
	m_flOldFrametime = I::GlobalVars->frametime;

	Store(tMoveStorage);

	I::MoveHelper->SetHost(pPlayer);
	pPlayer->m_pCurrentCommand() = &G::DummyCmd;
	pPlayer->m_nPlayerState() = TF_STATE_ACTIVE;
	if (pPlayer->m_MoveType() == MOVETYPE_NONE || pPlayer->m_MoveType() == MOVETYPE_OBSERVER)
		pPlayer->m_MoveType() = MOVETYPE_WALK;

	if (auto pAvgVelocity = H::Entities.GetAvgVelocity(pPlayer->entindex()))
		pPlayer->m_vecVelocity() = *pAvgVelocity;

	pPlayer->m_bDucked() = pPlayer->IsDucking() && !pPlayer->IsDormant();
	pPlayer->m_fFlags() &= ~FL_DUCKING;
	pPlayer->m_flDucktime() = 0.f;
	pPlayer->m_flDuckJumpTime() = 0.f;
	pPlayer->m_bDucking() = false;
	pPlayer->m_bInDuckJump() = false;

	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		pPlayer->m_vecBaseVelocity() = Vec3();
		if (pPlayer->IsOnGround() && !pPlayer->IsDormant())
			pPlayer->m_vecVelocity().z = std::min(pPlayer->m_vecVelocity().z, 0.f);
		else
			pPlayer->m_hGroundEntity() = nullptr;
	}
	else if (Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
		tMoveStorage.m_bBunnyHop = true;

	SetupMoveData(tMoveStorage, pvOrigin);
	if (CheckStuck(tMoveStorage))
	{
		tMoveStorage.m_bFailed = true;
		if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
		{
			auto& cache = m_mTrajectoryCache[pPlayer->entindex()];
			cache.m_iTickCount = I::GlobalVars->tickcount;
			cache.m_flSimulationTime = pPlayer->m_flSimulationTime();
			cache.m_vRequestedOrigin = pvOrigin ? *pvOrigin : Vec3();
			cache.m_bRequestedOrigin = pvOrigin != nullptr;
			cache.m_bPredict = bPredict;
			cache.m_bStrafe = bStrafe;
			cache.m_bHitchance = bHitchance;
			cache.m_bFailed = true;
			cache.m_bInitResult = true;
		}
		return true;
	}

	if (!StrafePrediction(tMoveStorage, bStrafe, bHitchance))
	{
		tMoveStorage.m_bFailed = true;
		if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
		{
			auto& cache = m_mTrajectoryCache[pPlayer->entindex()];
			cache.m_iTickCount = I::GlobalVars->tickcount;
			cache.m_flSimulationTime = pPlayer->m_flSimulationTime();
			cache.m_vRequestedOrigin = pvOrigin ? *pvOrigin : Vec3();
			cache.m_bRequestedOrigin = pvOrigin != nullptr;
			cache.m_bPredict = bPredict;
			cache.m_bStrafe = bStrafe;
			cache.m_bHitchance = bHitchance;
			cache.m_bFailed = true;
			cache.m_bInitResult = false;
		}
		return false;
	}

	tMoveStorage.m_vPath = { tMoveStorage.m_MoveData.m_vecAbsOrigin };
	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		auto& cache = m_mTrajectoryCache[pPlayer->entindex()];
		cache.m_iTickCount = I::GlobalVars->tickcount;
		cache.m_flSimulationTime = pPlayer->m_flSimulationTime();
		cache.m_vRequestedOrigin = pvOrigin ? *pvOrigin : Vec3();
		cache.m_bRequestedOrigin = pvOrigin != nullptr;
		cache.m_bPredict = bPredict;
		cache.m_bStrafe = bStrafe;
		cache.m_bHitchance = bHitchance;
		cache.m_bInitResult = true;
		cache.m_iInitTicks = bPredict ? GetChoke(pPlayer->entindex()) : 0;
		cache.m_vInitialOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
		cache.m_vInitialVelocity = tMoveStorage.m_MoveData.m_vecVelocity;
		cache.m_bFailed = false;
		cache.m_flAverageYaw = tMoveStorage.m_flAverageYaw;
		cache.m_tGroundInput = tMoveStorage.m_tGroundInput;
		cache.m_tAirInput = tMoveStorage.m_tAirInput;
		cache.m_flPredictedDelta = tMoveStorage.m_flPredictedDelta;
		cache.m_flPredictedSimTime = tMoveStorage.m_flPredictedSimTime;
		cache.m_bDirectMove = tMoveStorage.m_bDirectMove;
		cache.m_vTicks.clear();
	}

	if (bPredict)
	{
		for (int i = 0, n = GetChoke(pPlayer->entindex()); i < n; i++)
			RunTick(tMoveStorage);
	}

	return true;
}

void CMovementSimulation::SetupMoveData(MoveStorage& tMoveStorage, const Vec3* pvOrigin)
{
	tMoveStorage.m_MoveData.m_bFirstRunOfFunctions = false;
	tMoveStorage.m_MoveData.m_bGameCodeMovedPlayer = false;
	tMoveStorage.m_MoveData.m_nPlayerHandle = tMoveStorage.m_pPlayer->As<IHandleEntity>()->GetRefEHandle();

	tMoveStorage.m_MoveData.m_vecAbsOrigin = tMoveStorage.m_pPlayer->m_vecOrigin();
	tMoveStorage.m_MoveData.m_vecVelocity = tMoveStorage.m_pPlayer->m_vecVelocity();
	if (pvOrigin)
		tMoveStorage.m_MoveData.m_vecAbsOrigin = *pvOrigin;
	tMoveStorage.m_MoveData.m_flMaxSpeed = GetPlayerMaxSpeed(tMoveStorage.m_pPlayer);
	tMoveStorage.m_MoveData.m_flClientMaxSpeed = tMoveStorage.m_MoveData.m_flMaxSpeed;

	if (!tMoveStorage.m_MoveData.m_vecVelocity.To2D().IsZero())
	{
		int iIndex = tMoveStorage.m_pPlayer->entindex();
		if (iIndex == I::EngineClient->GetLocalPlayer() && G::CurrentUserCmd)
			tMoveStorage.m_MoveData.m_vecViewAngles = G::CurrentUserCmd->viewangles;
		else
		{
			if (!tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
				tMoveStorage.m_MoveData.m_vecViewAngles = { 0.f, Math::VectorAngles(tMoveStorage.m_MoveData.m_vecVelocity).y, 0.f };
			else
				tMoveStorage.m_MoveData.m_vecViewAngles = H::Entities.GetEyeAngles(iIndex);
		}

		const auto& vRecords = m_mRecords[iIndex];
		if (!vRecords.empty())
		{
			auto& tRecord = vRecords.front();
			// Store derives the press from the velocity, whose magnitude is the speed the player
			// ended up at rather than the magnitude they pressed. WalkMove clamps the wish speed to
			// m_flMaxSpeed and AirAccelerate scales by the uncapped one, so only a full press
			// reproduces the game; a sub-maxspeed velocity would systematically under-accelerate.
			Vec3 vDirection = tRecord.m_vDirection;
			if (float flLength = vDirection.Length2D(); flLength > 0.f && tMoveStorage.m_MoveData.m_flMaxSpeed > 0.f)
				vDirection *= tMoveStorage.m_MoveData.m_flMaxSpeed / flLength;
			if (!vDirection.IsZero())
			{
				G::DummyCmd.forwardmove = vDirection.x;
				G::DummyCmd.sidemove = -vDirection.y;
				G::DummyCmd.upmove = vDirection.z;
				SDK::FixMovement(&G::DummyCmd, {}, tMoveStorage.m_MoveData.m_vecViewAngles);
				tMoveStorage.m_MoveData.m_flForwardMove = G::DummyCmd.forwardmove;
				tMoveStorage.m_MoveData.m_flSideMove = G::DummyCmd.sidemove;
				tMoveStorage.m_MoveData.m_flUpMove = G::DummyCmd.upmove;
			}
		}
	}

	tMoveStorage.m_MoveData.m_vecAngles = tMoveStorage.m_MoveData.m_vecOldAngles = tMoveStorage.m_MoveData.m_vecViewAngles;
	if (auto pConstraintEntity = tMoveStorage.m_pPlayer->m_hConstraintEntity().Get())
		tMoveStorage.m_MoveData.m_vecConstraintCenter = pConstraintEntity->GetAbsOrigin();
	else
		tMoveStorage.m_MoveData.m_vecConstraintCenter = tMoveStorage.m_pPlayer->m_vecConstraintCenter();
	tMoveStorage.m_MoveData.m_flConstraintRadius = tMoveStorage.m_pPlayer->m_flConstraintRadius();
	tMoveStorage.m_MoveData.m_flConstraintWidth = tMoveStorage.m_pPlayer->m_flConstraintWidth();
	tMoveStorage.m_MoveData.m_flConstraintSpeedFactor = tMoveStorage.m_pPlayer->m_flConstraintSpeedFactor();

	tMoveStorage.m_flPredictedDelta = GetPredictedDelta(tMoveStorage.m_pPlayer);
	tMoveStorage.m_flSimTime = tMoveStorage.m_pPlayer->m_flSimulationTime();
	tMoveStorage.m_flPredictedSimTime = tMoveStorage.m_flSimTime + tMoveStorage.m_flPredictedDelta;
	tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	tMoveStorage.m_bDirectMove = tMoveStorage.m_pPlayer->IsOnGround() || tMoveStorage.m_pPlayer->IsSwimming();
}

bool CMovementSimulation::CheckStuck(MoveStorage& tMoveStorage)
{
	I::GameMovement->player = I::GameMovement->m_pTFPlayer = tMoveStorage.m_pPlayer, I::GameMovement->mv = &tMoveStorage.m_MoveData;
	SetBounds(tMoveStorage.m_pPlayer);
	bool bReturn = I::GameMovement->CheckStuck();
	RestoreBounds(tMoveStorage.m_pPlayer);
	return bReturn;
}

static inline float GetFrictionScale(float flVelocityXY, float flTurn, float flVelocityZ, float flMin = 50.f, float flMax = 150.f)
{
	if (0.f >= flVelocityZ || flVelocityZ > 250.f)
		return 1.f;

	static auto sv_airaccelerate = H::ConVars.FindVar("sv_airaccelerate");
	float flScale = std::max(sv_airaccelerate->GetFloat(), 1.f);
	flMin *= flScale, flMax *= flScale;

	return Math::RemapVal(fabsf(flVelocityXY * flTurn), flMin, flMax, 1.f, 0.25f);
}

#ifdef VISUALIZE_RECORDS
static inline void VisualizeRecords(MoveData& tRecord1, MoveData& tRecord2, Color_t tColor, float flStraightFuzzyValue)
{
	static int iStaticTickcount = I::GlobalVars->tickcount;
	if (I::GlobalVars->tickcount != iStaticTickcount)
	{
		G::LineStorage.clear();
		iStaticTickcount = I::GlobalVars->tickcount;
	}

	const float flYaw1 = Math::VectorAngles(tRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(tRecord2.m_vDirection).y;
	const float flTime1 = tRecord1.m_flSimTime, flTime2 = tRecord2.m_flSimTime;
	const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);
	const float flYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
	const bool bStraight = fabsf(flYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue;

	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord2.m_vOrigin), I::GlobalVars->curtime + 5.f, tColor);
	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord1.m_vOrigin + Vec3(0, 0, 5)), I::GlobalVars->curtime + 5.f, tColor);
	if (!bStraight && flYaw)
	{
		Vec3 vVelocity = tRecord1.m_vVelocity.Normalized2D() * 5;
		vVelocity = Math::RotatePoint(vVelocity, {}, { 0, flYaw > 0 ? 90.f : -90.f, 0 });
		if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::CalculateIncrease && tRecord1.m_iMode == MoveEnum::Air)
			vVelocity /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), flYaw, tRecord1.m_vVelocity.z + SDK::GetGravity() * TICK_INTERVAL, 0.f, 56.f);
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord1.m_vOrigin + vVelocity), I::GlobalVars->curtime + 5.f, tColor);
	}
}
#endif

struct YawHistory
{
	int m_iChanges = 0;
	int m_iStart = 0;
	int m_iSign = 0;
	bool m_bZero = false;
};

static inline bool GetYawDifference(MoveData& tRecord1, MoveData& tRecord2, bool bStart, float* pYaw, float flStraightFuzzyValue, YawHistory& tHistory, int iMaxChanges = 0, int iMaxChangeTime = 0, float flMaxSpeed = 0.f)
{
	const float flYaw1 = Math::VectorAngles(tRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(tRecord2.m_vDirection).y;
	const float flTime1 = tRecord1.m_flSimTime, flTime2 = tRecord2.m_flSimTime;
	const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

	*pYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
	if (flMaxSpeed && tRecord1.m_iMode != MoveEnum::Air)
		*pYaw *= std::clamp(tRecord1.m_vVelocity.Length2D() / flMaxSpeed, 0.f, 1.f);
	if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::CalculateIncrease && tRecord1.m_iMode == MoveEnum::Air)
		*pYaw /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), *pYaw, tRecord1.m_vVelocity.z + SDK::GetGravity() * TICK_INTERVAL, 0.f, 56.f);
	if (fabsf(*pYaw) > 45.f)
		return false;

	const int iLastSign = tHistory.m_iSign;
	const int iCurrSign = tHistory.m_iSign = *pYaw ? sign(*pYaw) : tHistory.m_iSign;

	const bool iLastZero = tHistory.m_bZero;
	const bool iCurrZero = tHistory.m_bZero = !*pYaw;

	const bool bChanged = iCurrSign != iLastSign || iCurrZero && iLastZero;
	const bool bStraight = fabsf(*pYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue;

	if (bStart)
	{
		tHistory.m_iChanges = 0, tHistory.m_iStart = TIME_TO_TICKS(flTime1);
		if (bStraight && ++tHistory.m_iChanges > iMaxChanges)
			return false;
		return true;
	}
	else
	{
		if ((bChanged || bStraight) && ++tHistory.m_iChanges > iMaxChanges)
			return false;
		return tHistory.m_iChanges && tHistory.m_iStart - TIME_TO_TICKS(flTime2) > iMaxChangeTime ? false : true;
	}
}

struct InputSample
{
	float m_flAlpha = 0.f;
	float m_flKeyOffset = 0.f;
	float m_flGap = 1.f;
};

// Circular mean and resultant length, so an averaged angle never wraps the wrong way.
static inline void AddAngle(float flAngle, float flWeight, float& flSin, float& flCos, float& flWeightSum)
{
	const float flRad = Math::Deg2Rad(flAngle);
	flSin += sinf(flRad) * flWeight;
	flCos += cosf(flRad) * flWeight;
	flWeightSum += flWeight;
}

static inline bool GetMeanAngle(float flSin, float flCos, float flWeightSum, float* pAngle, float* pResultant)
{
	if (flWeightSum <= 0.f)
		return false;

	*pAngle = Math::Rad2Deg(atan2f(flSin, flCos));
	*pResultant = sqrtf(flSin * flSin + flCos * flCos) / flWeightSum;
	return true;
}

// Inverts one recorded tick of the real movement code.
//
// AirMove applies nothing to the 2D velocity except AirAccelerate, which adds a single scalar
// times the wish direction, so the velocity delta's direction is exactly the wish direction.
// WalkMove applies Friction first (a uniform scale, so it preserves direction and can be undone
// exactly) and Accelerate second, then rescales the whole 2D velocity to m_flMaxSpeed when it
// overshoots; that rescale is uniform, so it divides back out only while the pair was not
// accelerating past the cap. A player whose speed after friction already exceeds m_flMaxSpeed
// (just landed from a jump, or an explosive jump) has no acceleration headroom at all: the clamp
// is then the only thing acting on the velocity, it rotates the residual away from the press
// direction, and inverting it yields a press up to 180 degrees wrong. Those pairs are rejected.
//
// The wish speed itself is not identifiable here: Accelerate multiplies by the uncapped wish speed
// while AirAccelerate and WalkMove both clamp the amount added, so every full press produces the
// same delta. A full press is assumed, which is what the clamp means in practice.
static bool GetInputFromPair(const MoveData& tCurRecord, const MoveData& tPrevRecord, float flMaxSpeed, InputSample* pSample)
{
	Vec3 vPrev = tPrevRecord.m_vVelocity.To2D();
	const float flPrevSpeed = vPrev.Length2D();
	if (flPrevSpeed < 1.f || flMaxSpeed <= 0.f)
		return false;

	static auto sv_friction = H::ConVars.FindVar("sv_friction");
	static auto sv_stopspeed = H::ConVars.FindVar("sv_stopspeed");
	if (!sv_friction || !sv_stopspeed)
		return false;

	const float flFriction = sv_friction->GetFloat(), flStopSpeed = sv_stopspeed->GetFloat();

	const float flGap = float(std::max(TIME_TO_TICKS(tCurRecord.m_flSimTime - tPrevRecord.m_flSimTime), 1));
	const Vec3 vCur = tCurRecord.m_vVelocity.To2D();
	const float flCurSpeed = vCur.Length2D();

	Vec3 vDelta;
	if (tPrevRecord.m_iMode == MoveEnum::Air)
		vDelta = vCur - vPrev;
	else
	{
		// Undo the friction this many ticks would have taken off, then undo the maxspeed rescale.
		float flAfterFriction = flPrevSpeed;
		for (int i = 0; i < int(flGap); i++)
		{
			const float flDrop = (flAfterFriction < flStopSpeed ? flStopSpeed : flAfterFriction) * flFriction * TICK_INTERVAL;
			flAfterFriction = std::max(flAfterFriction - flDrop, 0.f);
		}

		if (flAfterFriction > flMaxSpeed + 0.5f)
			return false;

		const float flScale = flCurSpeed >= flMaxSpeed - 0.5f && flCurSpeed > 0.f ? flMaxSpeed / flCurSpeed : 1.f;
		vDelta = vCur / flScale - vPrev * (flAfterFriction / flPrevSpeed);
	}

	const float flDeltaLength = vDelta.Length2D();
	if (flDeltaLength < 1e-3f)
	{
		pSample->m_flAlpha = 0.f;
		pSample->m_flKeyOffset = 0.f;
		pSample->m_flGap = flGap;
		return false;
	}

	// Across a multi-tick gap the sampled delta is the chord of the rotation the player performed,
	// which sits at the midpoint of the gap; rotate it back to the first tick's press.
	const float flTurn = Math::NormalizeAngle(GetHeading(vCur) - GetHeading(vPrev));
	const float flChordOffset = flTurn * (flGap - 1.f) / (2.f * flGap);
	const float flWishYaw = GetHeading(vDelta) - flChordOffset;

	pSample->m_flAlpha = Math::NormalizeAngle(flWishYaw - GetHeading(vPrev));
	// The press that produced this delta was executed under the view the player had at the end of
	// the tick, which is the eye yaw of the newer sample, not the older one.
	pSample->m_flKeyOffset = Math::NormalizeAngle(flWishYaw - tCurRecord.m_flEyeYaw);
	pSample->m_flGap = flGap;
	return true;
}

// Least-squares view rotation from the networked eye yaw. Returns false when the history does not
// span enough ticks; a player who is not turning is indistinguishable from one we cannot measure,
// so the caller must treat "no answer" as zero rotation rather than borrowing the velocity heading.
static bool GetViewRate(const std::deque<MoveData>& vRecords, int iMode, int iSamples, float* pRate)
{
	float flSum = 0.f, flTicks = 0.f;
	iSamples = std::min(iSamples, int(vRecords.size()));
	int iRunStart = 0;
	while (iRunStart < iSamples && vRecords[iRunStart].m_iMode != iMode)
		iRunStart++;
	int iStart = iRunStart + 1;

	for (int i = iStart; i < iSamples; i++)
	{
		const auto& tCur = vRecords[i - 1];
		const auto& tPrev = vRecords[i];
		if (tCur.m_iMode != tPrev.m_iMode)
			break;

		const float flGap = float(std::max(TIME_TO_TICKS(tCur.m_flSimTime - tPrev.m_flSimTime), 1));
		const float flDelta = Math::NormalizeAngle(tCur.m_flEyeYaw - tPrev.m_flEyeYaw);
		if (fabsf(flDelta) / flGap > 45.f)
			continue;

		flSum += flDelta;
		flTicks += flGap;
	}

	if (flTicks < 4.f)
		return false;

	*pRate = flSum / flTicks;
	return true;
}

static inline float SnapKeyOffset(float flOffset, float* pSnapped)
{
	static constexpr float aOffsets[] = { 0.f, 90.f, -90.f, 180.f };
	float flBest = 0.f, flBestDelta = 1e9f;
	for (float flOffsetCandidate : aOffsets)
	{
		const float flDelta = fabsf(Math::NormalizeAngle(flOffset - flOffsetCandidate));
		if (flDelta < flBestDelta)
			flBestDelta = flDelta, flBest = flOffsetCandidate;
	}

	*pSnapped = flBest;
	return flBestDelta;
}

InputEstimate CMovementSimulation::GetInputEstimate(MoveStorage& tMoveStorage, int iMode, int iSamples)
{
	InputEstimate tEstimate = {};
	auto pPlayer = tMoveStorage.m_pPlayer;
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.size() < 2)
		return tEstimate;

	const float flMaxSpeed = GetPlayerMaxSpeed(pPlayer);
	if (flMaxSpeed <= 0.f)
		return tEstimate;

	const bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();
	const bool bAir = iMode == MoveEnum::Air;
	iSamples = std::min(iSamples, int(vRecords.size()));

	// Local players get their real command, so guessing would only make it worse.
	if (bLocal)
		return tEstimate;

	const int iMaxChanges = bAir ? Vars::Aimbot::Projectile::AirMaxChanges.Value : Vars::Aimbot::Projectile::GroundMaxChanges.Value;

	float flAlphaSin = 0.f, flAlphaCos = 0.f, flAlphaWeight = 0.f;
	float flKeySin = 0.f, flKeyCos = 0.f, flKeyWeight = 0.f;
	int iChanges = 0, iLastSign = 0, iUsed = 0;

	// The current mode's newest run is not necessarily at the front: a player who has just jumped
	// or landed still needs the other frame for the ticks after the transition, so skip forward to
	// the last run recorded in the requested mode instead of giving up.
	int iRunStart = 0;
	while (iRunStart < iSamples && vRecords[iRunStart].m_iMode != iMode)
		iRunStart++;
	int iStart = iRunStart + 1;
	if (iStart >= iSamples)
		return tEstimate;

	for (int i = iStart; i < iSamples; i++)
	{
		const auto& tCurRecord = vRecords[i - 1];
		const auto& tPrevRecord = vRecords[i];
		if (tPrevRecord.m_iMode != iMode)
			break;

		InputSample tSample;
		if (!GetInputFromPair(tCurRecord, tPrevRecord, flMaxSpeed, &tSample))
			continue;

		const int iSign = sign(tSample.m_flAlpha);
		if (iSign && iLastSign && iSign != iLastSign && ++iChanges > iMaxChanges)
			break;
		if (iSign)
			iLastSign = iSign;

		AddAngle(tSample.m_flAlpha, tSample.m_flGap, flAlphaSin, flAlphaCos, flAlphaWeight);
		AddAngle(tSample.m_flKeyOffset, tSample.m_flGap, flKeySin, flKeyCos, flKeyWeight);
		iUsed++;
	}

	if (iUsed < 2)
		return tEstimate;

	float flAlpha = 0.f, flAlphaResultant = 0.f, flKey = 0.f, flKeyResultant = 0.f;
	if (!GetMeanAngle(flAlphaSin, flAlphaCos, flAlphaWeight, &flAlpha, &flAlphaResultant)
		|| !GetMeanAngle(flKeySin, flKeyCos, flKeyWeight, &flKey, &flKeyResultant))
		return tEstimate;

	// The eye yaw is only a usable frame when the press sits on a key relative to it; the velocity
	// frame is always well defined because it is derived from the velocity itself.
	float flSnapped = 0.f;
	const bool bKey = !bAir && flKeyResultant >= 0.5f && SnapKeyOffset(flKey, &flSnapped) <= 25.f;
	// On the ground the key frame also has to be preferred to the velocity frame, or a player
	// strafing with a rotating view would be modelled as holding a key they are not holding.
	const bool bUseKey = bKey && flKeyResultant > flAlphaResultant;

	if (!bUseKey && fabsf(flAlpha) < 0.36f)
		return tEstimate;

	float flRate = 0.f;
	GetViewRate(vRecords, iMode, iSamples, &flRate);

	tEstimate.m_bValid = true;
	tEstimate.m_flAlpha = flAlpha;
	tEstimate.m_flWishSpeed = flMaxSpeed;
	// Anchor to the newest networked eye yaw rather than the first sample of the run: when the
	// player is airborne the ground run ended at take-off, and the facing at take-off is not the
	// facing they will land with. For a grounded run the two are the same record.
	tEstimate.m_flEyeYaw = vRecords.front().m_flEyeYaw;
	tEstimate.m_flViewRate = flRate;
	tEstimate.m_flKeyOffset = bUseKey ? flSnapped : 0.f;
	tEstimate.m_bKeyInput = bUseKey;

	if (Vars::Debug::Logging.Value)
		SDK::Output("MovementSimulation", std::format("input alpha {} ({}%) key {} ({}%) rate {} -> {}{}", flAlpha, int(flAlphaResultant * 100), flKey, int(flKeyResultant * 100), flRate, bUseKey ? "key" : "alpha", bLocal ? " (local)" : "").c_str(), { 100, 255, 150 }, Vars::Debug::Logging.Value);
	return tEstimate;
}

void CMovementSimulation::GetAverageYaw(MoveStorage& tMoveStorage, int iSamples)
{
	auto pPlayer = tMoveStorage.m_pPlayer;
	bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.empty())
		return;

	bool bGround = tMoveStorage.m_bDirectMove; int iMinimumStrafes = 4;
	float flMaxSpeed = GetPlayerMaxSpeed(tMoveStorage.m_pPlayer);
	float flLowMinimumDistance = bGround ? Vars::Aimbot::Projectile::GroundLowMinimumDistance.Value : Vars::Aimbot::Projectile::AirLowMinimumDistance.Value;
	float flLowMinimumSamples = bGround ? Vars::Aimbot::Projectile::GroundLowMinimumSamples.Value : Vars::Aimbot::Projectile::AirLowMinimumSamples.Value;
	float flHighMinimumDistance = bGround ? Vars::Aimbot::Projectile::GroundHighMinimumDistance.Value : Vars::Aimbot::Projectile::AirHighMinimumDistance.Value;
	float flHighMinimumSamples = bGround ? Vars::Aimbot::Projectile::GroundHighMinimumSamples.Value : Vars::Aimbot::Projectile::AirHighMinimumSamples.Value;

	float flAverageYaw = 0.f; int iTicks = 0, iSkips = 0;
	YawHistory tHistory = {};
	iSamples = std::min(iSamples, int(vRecords.size()));
	size_t i = 1; for (; i < iSamples; i++)
	{
		auto& tRecord1 = vRecords[i - 1];
		auto& tRecord2 = vRecords[i];
		if (tRecord1.m_iMode != tRecord2.m_iMode)
		{
			iSkips++;
			continue;
		}

		bGround = tRecord1.m_iMode != MoveEnum::Air;
		float flStraightFuzzyValue = bGround ? Vars::Aimbot::Projectile::GroundStraightFuzzyValue.Value : Vars::Aimbot::Projectile::AirStraightFuzzyValue.Value;
		int iMaxChanges = bLocal ? 0 : bGround ? Vars::Aimbot::Projectile::GroundMaxChanges.Value : Vars::Aimbot::Projectile::AirMaxChanges.Value;
		int iMaxChangeTime = bLocal ? 0 : bGround ? Vars::Aimbot::Projectile::GroundMaxChangeTime.Value : Vars::Aimbot::Projectile::AirMaxChangeTime.Value;
		iMinimumStrafes = 4 + iMaxChanges;
#ifdef VISUALIZE_RECORDS
		VisualizeRecords(tRecord1, tRecord2, { 255, 0, 0 }, flStraightFuzzyValue);
#endif

		float flYaw = 0.f;
		bool bResult = GetYawDifference(tRecord1, tRecord2, !iTicks, &flYaw, flStraightFuzzyValue, tHistory, iMaxChanges, iMaxChangeTime, flMaxSpeed);
		if (Vars::Debug::Logging.Value)
			SDK::Output("GetYawDifference", std::format("{} ({}): {}, {}", i, iTicks, flYaw, bResult).c_str(), { 50, 127, 75 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		if (!bResult)
			break;

		flAverageYaw += flYaw;
		iTicks += std::max(TIME_TO_TICKS(tRecord1.m_flSimTime - tRecord2.m_flSimTime), 1);
	}
#ifdef VISUALIZE_RECORDS
	size_t i2 = i; for (; i2 < iSamples; i2++)
	{
		auto& tRecord1 = vRecords[i2 - 1];
		auto& tRecord2 = vRecords[i2];

		float flStraightFuzzyValue = bGround ? Vars::Aimbot::Projectile::GroundStraightFuzzyValue.Value : Vars::Aimbot::Projectile::AirStraightFuzzyValue.Value;
		VisualizeRecords(tRecord1, tRecord2, { 0, 0, 0 }, flStraightFuzzyValue);
	}

#endif
	if (i <= size_t(iMinimumStrafes + iSkips))
		return;

	int iMinimum = flLowMinimumSamples;
	if (!bLocal)
	{
		float flDistance = 0.f;
		if (auto pLocal = H::Entities.GetLocal())
			flDistance = pLocal->m_vecOrigin().DistTo(tMoveStorage.m_pPlayer->m_vecOrigin());
		iMinimum = flDistance < flLowMinimumDistance ? flLowMinimumSamples : Math::RemapVal(flDistance, flLowMinimumDistance, flHighMinimumDistance, flLowMinimumSamples + 1, flHighMinimumSamples);
	}

	flAverageYaw /= std::max(iTicks, 1);
	if (fabsf(flAverageYaw) < 0.36f)
		return;

	tMoveStorage.m_flAverageYaw = flAverageYaw;
	if (Vars::Debug::Logging.Value)
		SDK::Output("MovementSimulation", std::format("flAverageYaw calculated to {} from {} ({}){}", flAverageYaw, iTicks, iMinimum, bLocal ? " (local)" : "").c_str(), { 100, 255, 150 }, Vars::Debug::Logging.Value);
}

bool CMovementSimulation::StrafePrediction(MoveStorage& tMoveStorage, bool bStrafe, bool bHitchance)
{
	PROJ_ZONE(MoveStrafe);

	tMoveStorage.m_tGroundInput = tMoveStorage.m_tAirInput = {};
	tMoveStorage.m_flViewYaw = 0.f;

	if (bStrafe && Vars::Aimbot::Projectile::StrafePrediction.Value & (tMoveStorage.m_bDirectMove ? Vars::Aimbot::Projectile::StrafePredictionEnum::Ground : Vars::Aimbot::Projectile::StrafePredictionEnum::Air))
	{
		const int iBaseSamples = tMoveStorage.m_bDirectMove
			? Vars::Aimbot::Projectile::GroundSamples.Value
			: Vars::Aimbot::Projectile::AirSamples.Value;
		const int iStrafeSamples = GetStrafeSamples(iBaseSamples);

		// Recover both frames up front: the simulation switches between them as the player lands
		// and jumps, and by then the history no longer holds samples for the other mode.
		tMoveStorage.m_tGroundInput = GetInputEstimate(tMoveStorage, MoveEnum::Ground, iStrafeSamples);
		tMoveStorage.m_tAirInput = GetInputEstimate(tMoveStorage, MoveEnum::Air, iStrafeSamples);
		if (!tMoveStorage.m_tGroundInput.m_bValid && !tMoveStorage.m_tAirInput.m_bValid)
			GetAverageYaw(tMoveStorage, iStrafeSamples);
		else
		{
			// Both estimates extrapolate the same networked eye yaw, so one anchor serves either
			// mode across a landing or a jump. Anchor to the newest record, not to the start of
			// whichever run the estimate was recovered from.
			const auto& vRecords = m_mRecords[tMoveStorage.m_pPlayer->entindex()];
			tMoveStorage.m_flViewYaw = vRecords.empty() ? 0.f : vRecords.front().m_flEyeYaw;
		}

		if (bHitchance && !tMoveStorage.m_pPlayer->m_vecVelocity().IsZero() && Vars::Aimbot::Projectile::HitChance.Value)
		{
			const auto& vRecords = m_mRecords[tMoveStorage.m_pPlayer->entindex()];
			const auto iSamples = vRecords.size();

			// The check asks whether the strafe history is consistent: every window has to agree
			// with the history as a whole. Reference the whole history rather than whichever
			// estimator happens to be driving the simulation, so the test means the same thing
			// regardless of which model was selected.
			float flReferenceYaw = 0.f, flReferenceTicks = 0.f;
			for (size_t i = 0; i + 2 < iSamples; i++)
			{
				const auto& pRecord1 = vRecords[i], & pRecord2 = vRecords[i + 1];
				const int iTicks = std::max(TIME_TO_TICKS(pRecord1.m_flSimTime - pRecord2.m_flSimTime), 1);
				flReferenceYaw += Math::NormalizeAngle(Math::VectorAngles(pRecord1.m_vDirection).y - Math::VectorAngles(pRecord2.m_vDirection).y) / iTicks;
				flReferenceTicks += 1.f;
			}
			if (flReferenceTicks > 0.f)
				flReferenceYaw /= flReferenceTicks;

			float flCurrentChance = 1.f, flAverageYaw = 0.f;
			for (size_t i = 0; i < iSamples; i++)
			{
				if (vRecords.size() <= i + 2)
					break;

				const auto& pRecord1 = vRecords[i], & pRecord2 = vRecords[i + 1];
				const float flYaw1 = Math::VectorAngles(pRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(pRecord2.m_vDirection).y;
				const float flTime1 = pRecord1.m_flSimTime, flTime2 = pRecord2.m_flSimTime;
				const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

				float flYaw = Math::NormalizeAngle(flYaw1 - flYaw2) / iTicks;
				flAverageYaw += flYaw;
				if (tMoveStorage.m_MoveData.m_flMaxSpeed)
					flYaw *= std::clamp(pRecord1.m_vVelocity.Length2D() / tMoveStorage.m_MoveData.m_flMaxSpeed, 0.f, 1.f);

				if ((i + 1) % iStrafeSamples == 0 || i == iSamples - 1)
				{
					flAverageYaw /= i % iStrafeSamples + 1;
					if (fabsf(flReferenceYaw - flAverageYaw) > 0.5f)
						flCurrentChance -= 1.f / ((iSamples - 1) / float(iStrafeSamples) + 1);
					flAverageYaw = 0.f;
				}
			}

			if (flCurrentChance < Vars::Aimbot::Projectile::HitChance.Value / 100)
			{
				if (Vars::Debug::Logging.Value)
					SDK::Output("MovementSimulation", std::format("Hitchance ({}% < {}%)", flCurrentChance * 100, Vars::Aimbot::Projectile::HitChance.Value).c_str(), { 80, 200, 120 }, Vars::Debug::Logging.Value);
				return false;
			}
		}
	}

	if (!tMoveStorage.m_bDirectMove && !tMoveStorage.m_tAirInput.m_bValid)
	{
		if (!tMoveStorage.m_flAverageYaw)
			tMoveStorage.m_MoveData.m_flForwardMove = tMoveStorage.m_MoveData.m_flSideMove = 0.f;
		else
		{
			const auto& vRecords = m_mRecords[tMoveStorage.m_pPlayer->entindex()];
			if (!vRecords.empty())
			{
				auto& tRecord = vRecords.front();
				Vec3 vDirection = tRecord.m_vVelocity;
				if (!vDirection.IsZero() && tRecord.m_bInputDirection)
				{
					int iSign = sign(tMoveStorage.m_flAverageYaw);
					float flForward = tMoveStorage.m_MoveData.m_flForwardMove, flSide = tMoveStorage.m_MoveData.m_flSideMove;
					tMoveStorage.m_MoveData.m_flForwardMove = -flSide * iSign, tMoveStorage.m_MoveData.m_flSideMove = flForward * iSign;
				}
			}
		}
	}

	return true;
}

bool CMovementSimulation::SetDuck(MoveStorage& tMoveStorage, bool bDuck)
{
	if (bDuck == tMoveStorage.m_pPlayer->m_bDucked())
		return true;

	auto pGameRules = I::TFGameRules();
	auto pViewVectors = pGameRules ? pGameRules->GetViewVectors() : nullptr;
	float flScale = tMoveStorage.m_pPlayer->m_flModelScale();

	if (!tMoveStorage.m_pPlayer->IsOnGround())
	{
		Vec3 vHullMins = (pViewVectors ? pViewVectors->m_vHullMin : Vec3(-24, -24, 0)) * flScale;
		Vec3 vHullMaxs = (pViewVectors ? pViewVectors->m_vHullMax : Vec3(24, 24, 82)) * flScale;
		Vec3 vDuckHullMins = (pViewVectors ? pViewVectors->m_vDuckHullMin : Vec3(-24, -24, 0)) * flScale;
		Vec3 vDuckHullMaxs = (pViewVectors ? pViewVectors->m_vDuckHullMax : Vec3(24, 24, 62)) * flScale;

		if (bDuck)
			tMoveStorage.m_MoveData.m_vecAbsOrigin += (vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins);
		else
		{
			Vec3 vOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin - ((vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins));

			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter(tMoveStorage.m_pPlayer);
			SDK::TraceHull(vOrigin, vOrigin, vHullMins, vHullMaxs, tMoveStorage.m_pPlayer->SolidMask(), &filter, &trace);
			if (trace.DidHit())
				return false;

			tMoveStorage.m_MoveData.m_vecAbsOrigin = vOrigin;
		}
	}
	tMoveStorage.m_pPlayer->m_bDucked() = bDuck;

	return true;
}

void CMovementSimulation::SetBounds(CTFPlayer* pPlayer)
{
	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
		return;

	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pViewVectors = pGameRules->GetViewVectors())
		{
			pViewVectors->m_vHullMin = Vec3(-24, -24, 0) + PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vHullMax = Vec3(24, 24, 82) - PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0) + PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62) - PLAYER_ORIGIN_COMPRESSION;
		}
	}
}

void CMovementSimulation::RestoreBounds(CTFPlayer* pPlayer)
{
	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
		return;

	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pViewVectors = pGameRules->GetViewVectors())
		{
			pViewVectors->m_vHullMin = Vec3(-24, -24, 0);
			pViewVectors->m_vHullMax = Vec3(24, 24, 82);
			pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0);
			pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62);
		}
	}
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback* pCallback)
{
	if (tMoveStorage.m_bFailed || !tMoveStorage.m_pPlayer || !tMoveStorage.m_pPlayer->IsPlayer())
		return;

	if (tMoveStorage.m_bCached)
	{
		auto it = m_mTrajectoryCache.find(tMoveStorage.m_pPlayer->entindex());
		if (it != m_mTrajectoryCache.end() && !it->second.m_vTicks.empty())
		{
			auto& cache = it->second;
			if (tMoveStorage.m_iTick < static_cast<int>(cache.m_vTicks.size()))
			{
				const auto& rec = cache.m_vTicks[tMoveStorage.m_iTick];
				tMoveStorage.m_MoveData.m_vecAbsOrigin = rec.m_vOrigin;
				tMoveStorage.m_MoveData.m_vecVelocity = rec.m_vVelocity;
				tMoveStorage.m_vPredictedOrigin = rec.m_vPredictedOrigin;
				tMoveStorage.m_flSimTime = rec.m_flSimTime;
				tMoveStorage.m_bPredictNetworked = rec.m_bPredictNetworked;
				tMoveStorage.m_bDirectMove = rec.m_bDirectMove;
			}
			else
			{
				const auto& last = cache.m_vTicks.back();
				int iExtraTicks = tMoveStorage.m_iTick - static_cast<int>(cache.m_vTicks.size()) + 1;
				tMoveStorage.m_MoveData.m_vecAbsOrigin = last.m_vOrigin + last.m_vVelocity * (TICK_INTERVAL * iExtraTicks);
				if (!last.m_bDirectMove)
					tMoveStorage.m_MoveData.m_vecAbsOrigin.z -= 0.5f * SDK::GetGravity() * powf(TICK_INTERVAL * iExtraTicks, 2.f);
				tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
				tMoveStorage.m_flSimTime = ROUND_TO_TICKS(last.m_flSimTime + TICK_INTERVAL * iExtraTicks);
			}
			tMoveStorage.m_iTick++;
			PROJ_COUNT(MoveTicksCached);
			if (bPath)
				tMoveStorage.m_vPath.push_back(tMoveStorage.m_MoveData.m_vecAbsOrigin);
			if (pCallback)
				(*pCallback)(tMoveStorage.m_MoveData);
			return;
		}
	}

	PROJ_ZONE(MoveTick);
	PROJ_COUNT(MoveTicks);

	const bool bInputRecovered = tMoveStorage.m_tGroundInput.m_bValid || tMoveStorage.m_tAirInput.m_bValid;
	// Skipping ProcessMovement integrates the velocity linearly, which throws away the turn that
	// Accelerate/AirAccelerate produce from a recovered press; a recovered input must run the real
	// movement code on every tick or the whole point of recovering it is lost.
	const int iSubstep = bInputRecovered ? 1 : GetSubstep(tMoveStorage.m_iTick);
	const bool bSkipProcessMovement = (iSubstep > 1 && (tMoveStorage.m_iSubstepCounter++ % iSubstep) != 0);

	if (bSkipProcessMovement)
	{
		tMoveStorage.m_MoveData.m_vecAbsOrigin += tMoveStorage.m_MoveData.m_vecVelocity * TICK_INTERVAL;
		if (!tMoveStorage.m_bDirectMove)
			tMoveStorage.m_MoveData.m_vecVelocity.z -= SDK::GetGravity() * TICK_INTERVAL;

		if (tMoveStorage.m_flAverageYaw)
		{
			tMoveStorage.m_MoveData.m_vecViewAngles.y += tMoveStorage.m_flAverageYaw;
			tMoveStorage.m_MoveData.m_vecVelocity = Math::RotatePoint(tMoveStorage.m_MoveData.m_vecVelocity, {}, { 0, tMoveStorage.m_flAverageYaw, 0 });
		}

		tMoveStorage.m_flSimTime = ROUND_TO_TICKS(tMoveStorage.m_flSimTime + TICK_INTERVAL);
		tMoveStorage.m_bPredictNetworked = TIME_TO_TICKS(tMoveStorage.m_flSimTime) >= TIME_TO_TICKS(tMoveStorage.m_flPredictedSimTime);
		if (tMoveStorage.m_bPredictNetworked)
		{
			tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
			tMoveStorage.m_flPredictedSimTime += tMoveStorage.m_flPredictedDelta;
		}

		if (pCallback)
			(*pCallback)(tMoveStorage.m_MoveData);
	}
	else
	{

		I::Prediction->m_bInPrediction = true;
		I::Prediction->m_bFirstTimePredicted = false;
		I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
		SetBounds(tMoveStorage.m_pPlayer);

		if (tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			static auto tf_demoman_charge_drain_time = H::ConVars.FindVar("tf_demoman_charge_drain_time");

			float flDrainTime = SDK::AttribHookValue(tf_demoman_charge_drain_time->GetFloat(), "mod_charge_time", tMoveStorage.m_pPlayer);
			tMoveStorage.m_pPlayer->m_flChargeMeter() -= TICK_INTERVAL * 100.f / flDrainTime;

			if (tMoveStorage.m_pPlayer->m_flChargeMeter() <= 0.f)
			{
				tMoveStorage.m_pPlayer->RemoveCond(TF_COND_SHIELD_CHARGE);
				tMoveStorage.m_MoveData.m_flMaxSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed = GetPlayerMaxSpeed(tMoveStorage.m_pPlayer);
				tMoveStorage.m_pPlayer->m_flMaxspeed() = tMoveStorage.m_MoveData.m_flMaxSpeed;
			}
		}

		InputEstimate& tInput = tMoveStorage.m_bDirectMove ? tMoveStorage.m_tGroundInput : tMoveStorage.m_tAirInput;
		const bool bUseInput = tInput.m_bValid && tMoveStorage.m_MoveData.m_vecVelocity.Length2D() > 0.f;
		// Only the ground frame is anchored to the view: WalkMove's back-speed clamp and its
		// acceleration both key off the view, and grounded players steer that way. In the air
		// the invariant that actually holds is the offset from the velocity, so the view is put
		// on the velocity heading and alpha is applied to it.
		const bool bUseKey = bUseInput && tMoveStorage.m_bDirectMove && tInput.m_bKeyInput;

		float flCorrection = 0.f;
		if (bUseInput)
		{
			// The recovered input drives the press directly; the turn then falls out of
			// Accelerate/AirAccelerate rather than being imposed on the velocity.
			tMoveStorage.m_flViewYaw = Math::NormalizeAngle(tMoveStorage.m_flViewYaw + tInput.m_flViewRate);

			// The view fed to the movement code is the player's real facing, extrapolated at their
			// observed mouse rate, in both modes: AirMove reads it for the forward-pull attribute
			// even though the press direction itself comes from alpha. The press is then chosen so
			// that the resulting wish direction matches the recovered frame.
			const float flViewYaw = tMoveStorage.m_flViewYaw;
			tMoveStorage.m_MoveData.m_vecViewAngles = tMoveStorage.m_MoveData.m_vecAngles = tMoveStorage.m_MoveData.m_vecOldAngles = { 0.f, flViewYaw, 0.f };

			const float flWishYaw = Math::NormalizeAngle(bUseKey
				? flViewYaw + tInput.m_flKeyOffset
				: GetHeading(tMoveStorage.m_MoveData.m_vecVelocity) + tInput.m_flAlpha);
			const float flDelta = Math::Deg2Rad(flViewYaw - flWishYaw);
			tMoveStorage.m_MoveData.m_flForwardMove = cosf(flDelta) * tInput.m_flWishSpeed;
			tMoveStorage.m_MoveData.m_flSideMove = sinf(flDelta) * tInput.m_flWishSpeed;
			tMoveStorage.m_MoveData.m_flUpMove = 0.f;
		}
		else if (tMoveStorage.m_flAverageYaw)
		{
			float flMult = 1.f;
			if (!tMoveStorage.m_bDirectMove && !tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
			{
				flCorrection = 90.f * sign(tMoveStorage.m_flAverageYaw);
				if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::RunReduce)
					flMult = GetFrictionScale(tMoveStorage.m_MoveData.m_vecVelocity.Length2D(), tMoveStorage.m_flAverageYaw, tMoveStorage.m_MoveData.m_vecVelocity.z + SDK::GetGravity() * TICK_INTERVAL);
			}
			tMoveStorage.m_MoveData.m_vecViewAngles.y += tMoveStorage.m_flAverageYaw * flMult + flCorrection;
		}

		float flOldSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed;
		if (tMoveStorage.m_pPlayer->m_bDucked() && tMoveStorage.m_pPlayer->IsOnGround() && !tMoveStorage.m_pPlayer->IsSwimming())
			tMoveStorage.m_MoveData.m_flClientMaxSpeed /= 3;

		if (tMoveStorage.m_bBunnyHop && tMoveStorage.m_pPlayer->IsOnGround() && !tMoveStorage.m_pPlayer->m_bDucked())
		{
			tMoveStorage.m_MoveData.m_nOldButtons = 0;
			tMoveStorage.m_MoveData.m_nButtons |= IN_JUMP;
		}

		I::GameMovement->ProcessMovement(tMoveStorage.m_pPlayer, &tMoveStorage.m_MoveData);
		if (pCallback)
			(*pCallback)(tMoveStorage.m_MoveData);

		tMoveStorage.m_MoveData.m_flClientMaxSpeed = flOldSpeed;

		tMoveStorage.m_flSimTime = ROUND_TO_TICKS(tMoveStorage.m_flSimTime + TICK_INTERVAL);
		tMoveStorage.m_bPredictNetworked = TIME_TO_TICKS(tMoveStorage.m_flSimTime) >= TIME_TO_TICKS(tMoveStorage.m_flPredictedSimTime);
		if (tMoveStorage.m_bPredictNetworked)
		{
			tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
			tMoveStorage.m_flPredictedSimTime += tMoveStorage.m_flPredictedDelta;
		}
		bool bLastbDirectMove = tMoveStorage.m_bDirectMove;
		tMoveStorage.m_bDirectMove = tMoveStorage.m_pPlayer->IsOnGround() || tMoveStorage.m_pPlayer->IsSwimming();

		if (!bInputRecovered && tMoveStorage.m_flAverageYaw)
			tMoveStorage.m_MoveData.m_vecViewAngles.y -= flCorrection;

		if (tMoveStorage.m_bDirectMove && !bLastbDirectMove)
		{
			if (bUseInput)
			{
				// Re-anchor the grounded key frame to the facing the player will actually land
				// with. The ground estimate was recovered from history that ends at take-off.
				const bool bGroundKey = tMoveStorage.m_tGroundInput.m_bValid && tMoveStorage.m_tGroundInput.m_bKeyInput;
				if (bGroundKey)
					tMoveStorage.m_flViewYaw = tMoveStorage.m_tGroundInput.m_flEyeYaw;
			}
			if ((!bUseInput && !bInputRecovered) && tMoveStorage.m_flAverageYaw)
				tMoveStorage.m_flAverageYaw *= -1;
			else if ((!bUseInput && !bInputRecovered) && !tMoveStorage.m_MoveData.m_flForwardMove && !tMoveStorage.m_MoveData.m_flSideMove
				&& tMoveStorage.m_MoveData.m_vecVelocity.Length2D() > tMoveStorage.m_MoveData.m_flMaxSpeed * 0.015f)
			{
				Vec3 vDirection = tMoveStorage.m_MoveData.m_vecVelocity.Normalized2D() * 520.f;
				G::DummyCmd.forwardmove = vDirection.x, G::DummyCmd.sidemove = -vDirection.y;
				SDK::FixMovement(&G::DummyCmd, {}, tMoveStorage.m_MoveData.m_vecViewAngles);
				tMoveStorage.m_MoveData.m_flForwardMove = G::DummyCmd.forwardmove, tMoveStorage.m_MoveData.m_flSideMove = G::DummyCmd.sidemove;
			}
		}

		RestoreBounds(tMoveStorage.m_pPlayer);
	}

	if (bPath)
		tMoveStorage.m_vPath.push_back(tMoveStorage.m_MoveData.m_vecAbsOrigin);

	if (tMoveStorage.m_pPlayer && tMoveStorage.m_pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		auto& cache = m_mTrajectoryCache[tMoveStorage.m_pPlayer->entindex()];
		if (cache.m_iTickCount == I::GlobalVars->tickcount)
		{
			cache.m_vTicks.push_back({
				tMoveStorage.m_MoveData.m_vecAbsOrigin,
				tMoveStorage.m_MoveData.m_vecVelocity,
				tMoveStorage.m_vPredictedOrigin,
				tMoveStorage.m_flSimTime,
				tMoveStorage.m_bPredictNetworked,
				tMoveStorage.m_bDirectMove
			});
		}
	}
	tMoveStorage.m_iTick++;
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback fCallback)
{
	RunTick(tMoveStorage, bPath, &fCallback);
}

void CMovementSimulation::Restore(MoveStorage& tMoveStorage)
{
	PROJ_ZONE(MoveRestore);

	if (tMoveStorage.m_bInitFailed || !tMoveStorage.m_pPlayer)
		return;

	if (tMoveStorage.m_bCached)
		return;

	I::MoveHelper->SetHost(nullptr);
	tMoveStorage.m_pPlayer->m_pCurrentCommand() = nullptr;

	Reset(tMoveStorage);

	I::Prediction->m_bInPrediction = m_bOldInPrediction;
	I::Prediction->m_bFirstTimePredicted = m_bOldFirstTimePredicted;
	I::GlobalVars->frametime = m_flOldFrametime;
}

float CMovementSimulation::GetPredictedDelta(CBaseEntity* pEntity)
{
	auto& vSimTimes = m_mSimTimes[pEntity->entindex()];
	if (!vSimTimes.empty())
	{
		switch (Vars::Aimbot::Projectile::DeltaMode.Value)
		{
		case 0: return std::reduce(vSimTimes.begin(), vSimTimes.end()) / vSimTimes.size();
		case 1: return *std::max_element(vSimTimes.begin(), vSimTimes.end());
		}
	}
	return TICK_INTERVAL;
}

float CMovementSimulation::GetAccuracy() const
{
	return std::clamp(Vars::Aimbot::General::MoveSimAccuracy.Value / 100.f, 0.f, 1.f);
}

float CMovementSimulation::GetMaxSimulationTime(float flDesiredMaxTime) const
{
	const float flAccuracy = GetAccuracy();
	const float flFactor = Math::RemapVal(flAccuracy, 0.f, 1.f, 0.40f, 1.0f);
	return std::max(0.4f, flDesiredMaxTime * flFactor);
}

int CMovementSimulation::GetStrafeSamples(int iBaseSamples) const
{
	const float flAccuracy = GetAccuracy();
	const float flFactor = Math::RemapVal(flAccuracy, 0.f, 1.f, 0.25f, 1.0f);
	return std::max(6, static_cast<int>(iBaseSamples * flFactor));
}

int CMovementSimulation::GetSubstep(int iCurrentSimTick) const
{
	const float flAccuracy = GetAccuracy();
	if (flAccuracy >= 0.85f)
		return 1;
	if (flAccuracy >= 0.50f)
		return (iCurrentSimTick <= 20) ? 1 : 2;
	if (flAccuracy >= 0.25f)
		return (iCurrentSimTick <= 10) ? 1 : ((iCurrentSimTick <= 30) ? 2 : 3);
	return (iCurrentSimTick <= 4) ? 1 : 3;
}

int CMovementSimulation::GetMaxTargetCandidates(int iConfiguredMax) const
{
	const float flAccuracy = GetAccuracy();
	if (flAccuracy >= 0.60f)
		return iConfiguredMax;
	if (flAccuracy >= 0.30f)
		return std::min(iConfiguredMax, 2);
	return 1;
}

int CMovementSimulation::GetChoke(int iIndex) const
{
	return std::min(H::Entities.GetChoke(iIndex), MaxChokeTicks);
}

int CMovementSimulation::GetLatency(int iLeadTicks, int iChoke) const
{
	return std::clamp(iLeadTicks - iChoke, 0, MaxLatencyTicks);
}

void CMovementSimulation::ClearCache()
{
	m_mTrajectoryCache.clear();
}
