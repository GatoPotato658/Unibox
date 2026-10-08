#include "AimbotProjectile.h"

#include "../Aimbot.h"


namespace
{
	constexpr int kSimSubsteps = 8;
	constexpr float kSimStep = 1.f / 16.f / kSimSubsteps;
	constexpr int kSimSegments = 100;
	constexpr float kLockTan = 0.1f;
	constexpr float kAskDistanceScale = 50.f;
	constexpr int kThrowIdle = 0, kThrowCharging = 1, kThrowCharged = 2;
	constexpr int kGoalFlagDisableBallScore = 2;
	constexpr int kIdlePlanInterval = 4;
	constexpr int kReplanInterval = 2;

	struct BallThrow_t
	{
		float m_flSpeed = 0.f;
		float m_flArc = 0.f;
		float m_flVelocityScale = 0.f;
		float m_flDamping = 0.f;
		float m_flGravity = 0.f;
		float m_flRadius = 0.f;
	};

	struct Goal_t
	{
		CBaseEntity* m_pEntity = nullptr;
		Vec3 m_vMins = {};
		Vec3 m_vMaxs = {};
		Vec3 m_vCenter = {};
		int m_iPoints = 1;
		bool m_bTower = false;
	};

	struct Env_t
	{
		std::vector<Goal_t> m_vGoals = {};
		float m_flPassRange = FLT_MAX;
	} s_tEnv;

	ConVar* FindVar(const char* sName)
	{
		return H::ConVars.FindVar(sName);
	}

	float GetVar(const char* sName)
	{
		auto pVar = FindVar(sName);
		return pVar ? pVar->GetFloat() : 0.f;
	}

	ConVar* GetClassVar(const char* sPrefix, int iClass, ConVar* (&aCache)[TF_CLASS_COUNT_ALL])
	{
		static constexpr const char* aSuffix[TF_CLASS_COUNT_ALL] = { "scout", "scout", "sniper", "soldier", "demoman", "medic", "heavy", "pyro", "spy", "engineer", "scout" };
		iClass = std::clamp(iClass, 0, int(TF_CLASS_COUNT_ALL) - 1);
		if (!aCache[iClass])
			aCache[iClass] = FindVar(std::format("{}{}", sPrefix, aSuffix[iClass]).c_str());
		return aCache[iClass];
	}

	bool GetThrow(CTFPlayer* pLocal, BallThrow_t& tOut)
	{
		static ConVar* aSpeed[TF_CLASS_COUNT_ALL] = {}; static ConVar* aArc[TF_CLASS_COUNT_ALL] = {};

		const int iClass = pLocal->m_iClass();
		auto pSpeed = GetClassVar("tf_passtime_throwspeed_", iClass, aSpeed);
		auto pArc = GetClassVar("tf_passtime_throwarc_", iClass, aArc);
		if (!pSpeed || !pArc)
			return false;

		tOut = { pSpeed->GetFloat(), pArc->GetFloat(), GetVar("tf_passtime_throwspeed_velocity_scale"), GetVar("tf_passtime_ball_damping_scale"), SDK::GetGravity(), GetVar("tf_passtime_ball_sphere_radius") };
		return true;
	}

	int GetThrowState(CTFWeaponBase* pWeapon)
	{
		static const int iOffset = U::NetVars.GetNetVar("CPasstimeGun", "m_eThrowState");
		return iOffset ? *reinterpret_cast<int*>(uintptr_t(pWeapon) + iOffset) : -1;
	}

	float GetAskForBallTime(CTFPlayer* pPlayer)
	{
		static const int iOffset = U::NetVars.GetNetVar("CTFPlayer", "m_askForBallTime");
		return iOffset ? *reinterpret_cast<float*>(uintptr_t(pPlayer) + iOffset) : 0.f;
	}

	bool IsSegmentInBox(const Vec3& vStart, const Vec3& vEnd, const Vec3& vMins, const Vec3& vMaxs)
	{
		float flEnter = 0.f, flExit = 1.f;
		for (int i = 0; i < 3; i++)
		{
			const float flDelta = vEnd[i] - vStart[i];
			if (fabsf(flDelta) < 0.0001f)
			{
				if (vStart[i] < vMins[i] || vStart[i] > vMaxs[i])
					return false;
				continue;
			}

			float flNear = (vMins[i] - vStart[i]) / flDelta, flFar = (vMaxs[i] - vStart[i]) / flDelta;
			if (flNear > flFar)
				std::swap(flNear, flFar);
			flEnter = std::max(flEnter, flNear), flExit = std::min(flExit, flFar);
			if (flEnter > flExit)
				return false;
		}
		return true;
	}

	Vec3 GetLaunchVelocity(const BallThrow_t& tThrow, const Vec3& vForward, const Vec3& vOwnerVel)
	{
		const Vec3 vLaunch = (vForward * (1.f - tThrow.m_flArc) + Vec3(0.f, 0.f, tThrow.m_flArc)).Normalized();
		return vLaunch * tThrow.m_flSpeed + vForward * (tThrow.m_flVelocityScale * vForward.Dot(vOwnerVel));
	}

	bool GetAimFromLaunch(float flArc, const Vec3& vLaunch, Vec3& vOut)
	{
		if (flArc <= 0.f)
			return vOut = vLaunch, true;

		const float flRoot = flArc * flArc * (vLaunch.z * vLaunch.z - 1.f) + (1.f - flArc) * (1.f - flArc);
		if (flRoot < 0.f)
			return false;

		const float flScale = flArc * vLaunch.z + sqrtf(flRoot);
		vOut = (vLaunch * flScale - Vec3(0.f, 0.f, flArc)).Normalized();
		return true;
	}

	bool SolveBall(const Vec3& vEye, const Vec3& vTarget, const BallThrow_t& tThrow, const Vec3& vOwnerVel, bool bLob, Vec3& vForward, float& flTime)
	{
		vForward = (vTarget - vEye).Normalized();
		flTime = 1.f;
		for (int i = 0; i < 4; i++)
		{
			const float flDamp = 1.f - tThrow.m_flDamping * flTime / 2.f;
			const Vec3 vAdd = vForward * (tThrow.m_flVelocityScale * vForward.Dot(vOwnerVel));

			Vec3 vLaunch;
			if (!SolveLaunch(vTarget - vEye, tThrow.m_flSpeed * flDamp, vAdd * flDamp, tThrow.m_flGravity, bLob, vLaunch, flTime)
				|| !GetAimFromLaunch(tThrow.m_flArc, vLaunch, vForward))
				return false;
		}
		return true;
	}

	bool SimulateBall(CTFPlayer* pLocal, const Vec3& vEye, Vec3 vVelocity, const BallThrow_t& tThrow, const Goal_t& tGoal, float& flTime)
	{
		CTraceFilterWorldAndPropsOnly filter(pLocal);
		CGameTrace trace = {};
		Vec3 vPos = vEye;
		for (int iSegment = 0; iSegment < kSimSegments; iSegment++)
		{
			const Vec3 vStart = vPos;
			bool bEntered = false;
			for (int i = 0; i < kSimSubsteps; i++)
			{
				vVelocity.z -= tThrow.m_flGravity * kSimStep;
				vVelocity -= vVelocity * (tThrow.m_flDamping * kSimStep);
				vPos += vVelocity * kSimStep;
				if (vPos.x >= tGoal.m_vMins.x && vPos.x <= tGoal.m_vMaxs.x && vPos.y >= tGoal.m_vMins.y && vPos.y <= tGoal.m_vMaxs.y && vPos.z >= tGoal.m_vMins.z && vPos.z <= tGoal.m_vMaxs.z)
				{
					bEntered = true;
					flTime = (iSegment * kSimSubsteps + i + 1) * kSimStep;
					break;
				}
			}

			SDK::TraceHull(vStart, vPos, Vec3::Get(-tThrow.m_flRadius), Vec3::Get(tThrow.m_flRadius), MASK_PLAYERSOLID, &filter, &trace);
			if (trace.fraction < 1.f || trace.startsolid)
				return false;
			if (bEntered)
				return true;
		}
		return false;
	}

	bool IsPassTargetValid(CTFPlayer* pLocal, CTFPlayer* pTeammate)
	{
		return pTeammate != pLocal && !pTeammate->IsDormant() && pTeammate->IsAlive() && pTeammate->m_iTeamNum() == pLocal->m_iTeamNum()
			&& !pTeammate->m_bHasPasstimeBall() && !pTeammate->m_bCarryingObject()
			&& !pTeammate->InCond(TF_COND_DISGUISED) && !pTeammate->InCond(TF_COND_INVULNERABLE_WEARINGOFF) && !pTeammate->InCond(TF_COND_SELECTED_TO_TELEPORT)
			&& !pTeammate->IsInvulnerable() && !pTeammate->IsInvisible() && !pTeammate->IsTaunting() && !pTeammate->InCond(TF_COND_STUNNED);
	}

	bool IsLineBlocked(CTFPlayer* pLocal, const Vec3& vStart, const Vec3& vEnd, CBaseEntity* pIgnore, float flPad, bool bEnemiesOnly)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
		{
			if (pEntity == pLocal || pEntity == pIgnore || pEntity->IsDormant())
				continue;

			auto pPlayer = pEntity->As<CTFPlayer>();
			if (!pPlayer->IsAlive() || bEnemiesOnly && pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
				continue;

			const Vec3 vOrigin = pPlayer->GetAbsOrigin();
			if (IsSegmentInBox(vStart, vEnd, vOrigin + pPlayer->m_vecMins() - Vec3::Get(flPad), vOrigin + pPlayer->m_vecMaxs() + Vec3::Get(flPad)))
				return true;
		}
		return false;
	}

	bool HasLockLine(CTFPlayer* pLocal, const Vec3& vEye, CTFPlayer* pTeammate)
	{
		CTraceFilterWorldAndPropsOnly filter(pLocal);
		CGameTrace trace = {};
		const Vec3 vCenter = pTeammate->GetCenter();
		SDK::Trace(vEye, vCenter, MASK_PLAYERSOLID, &filter, &trace);
		return trace.fraction >= 1.f && !IsLineBlocked(pLocal, vEye, vCenter, pTeammate, 0.f, false);
	}

	float GetNearbyRange()
	{
		return GetVar("tf_passtime_pack_range");
	}

	float GetThreat(CTFPlayer* pLocal, const Vec3& vPos)
	{
		const float flRange = GetNearbyRange();
		float flScore = 0.f;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			if (pEntity->IsDormant() || !pEntity->As<CTFPlayer>()->IsAlive())
				continue;

			const float flDist = std::max(vPos.DistTo(pEntity->GetAbsOrigin()), 1.f);
			if (flDist < flRange)
				flScore += 1.f / flDist;
		}
		return flScore;
	}

	void BuildEnv(CTFPlayer* pLocal)
	{
		s_tEnv.m_vGoals.clear();
		s_tEnv.m_flPassRange = FLT_MAX;

		const int iTeam = pLocal->m_iTeamNum();
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
		{
			if (pEntity->IsDormant())
				continue;

			switch (pEntity->GetClassID())
			{
			case ETFClassID::CTFPasstimeLogic:
				if (const float flRange = pEntity->As<CTFPasstimeLogic>()->m_flMaxPassRange(); flRange > 0.f && flRange < FLT_MAX)
					s_tEnv.m_flPassRange = flRange;
				break;
			case ETFClassID::CFuncPasstimeGoal:
			{
				auto pGoal = pEntity->As<CFuncPasstimeGoal>();
				if (pGoal->m_bTriggerDisabled())
					break;

				const PasstimeMapGoalData_t* pMapGoal = nullptr;
				float flBest = FLT_MAX;
				for (const auto& tMapGoal : G::PasstimeGoalStorage)
				{
					if (const float flDist = tMapGoal.m_vOrigin.DistToSqr(pEntity->GetAbsOrigin()); flDist < flBest)
						pMapGoal = &tMapGoal, flBest = flDist;
				}
				if (pMapGoal && flBest > 1.f)
					pMapGoal = nullptr;

				int iGoalTeam = pEntity->m_iTeamNum();
				if (!iGoalTeam && pMapGoal)
					iGoalTeam = pMapGoal->m_iTeam;
				if (iGoalTeam != iTeam)
					break;

				if (pMapGoal ? pMapGoal->m_iSpawnflags & kGoalFlagDisableBallScore || pMapGoal->m_iPoints < 0 : pGoal->m_iGoalType() == CFuncPasstimeGoal::TYPE_ENDZONE)
					break;

				const Vec3 vOrigin = pEntity->GetAbsOrigin();
				Vec3 vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();
				if (vMins.IsZero() && vMaxs.IsZero())
					I::ModelInfoClient->GetModelBounds(pEntity->GetModel(), vMins, vMaxs);

				Goal_t tGoal = { pEntity, vOrigin + vMins, vOrigin + vMaxs };
				tGoal.m_vCenter = (tGoal.m_vMins + tGoal.m_vMaxs) / 2.f;
				tGoal.m_iPoints = pMapGoal ? pMapGoal->m_iPoints : 1;
				tGoal.m_bTower = pGoal->m_iGoalType() == CFuncPasstimeGoal::TYPE_TOWER;
				s_tEnv.m_vGoals.push_back(tGoal);
			}
			}
		}
	}

	bool GetGoalDistance(const Vec3& vFrom, float& flOut)
	{
		flOut = FLT_MAX;
		for (const auto& tGoal : s_tEnv.m_vGoals)
			flOut = std::min(flOut, vFrom.DistTo(tGoal.m_vCenter));
		return flOut != FLT_MAX;
	}

	bool WouldLockPass(CTFPlayer* pLocal, const Vec3& vEye, const Vec3& vAngles, float flTolerance)
	{
		Vec3 vForward, vRight, vUp; Math::AngleVectors(vAngles, &vForward, &vRight, &vUp);
		const float flNow = TICKS_TO_TIME(pLocal->m_nTickBase());
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
		{
			auto pTeammate = pEntity->As<CTFPlayer>();
			if (!IsPassTargetValid(pLocal, pTeammate))
				continue;

			const Vec3 vDelta = pTeammate->GetCenter() - vEye;
			const float flForward = vDelta.Dot(vForward);
			if (flForward <= 0.f || vDelta.Length() > s_tEnv.m_flPassRange)
				continue;

			float flLock = hypotf(vDelta.Dot(vRight), vDelta.Dot(vUp)) / flForward;
			if (GetAskForBallTime(pTeammate) > flNow)
				flLock /= kAskDistanceScale;
			if (flLock < kLockTan + flTolerance && HasLockLine(pLocal, vEye, pTeammate))
				return true;
		}
		return false;
	}

	bool IsBetter(const PasstimePlan_t& a, const PasstimePlan_t& b)
	{
		return std::tie(a.m_iTier, a.m_flKey1, a.m_flKey2) > std::tie(b.m_iTier, b.m_flKey1, b.m_flKey2);
	}
}

bool CAimbotProjectile::SolvePasstimeGoal(CTFPlayer* pLocal, CBaseEntity* pGoal, PasstimePlan_t& tPlan)
{
	BallThrow_t tThrow;
	if (!GetThrow(pLocal, tThrow))
		return false;

	const Goal_t* pInfo = nullptr;
	for (const auto& tGoal : s_tEnv.m_vGoals)
	{
		if (tGoal.m_pEntity == pGoal)
			pInfo = &tGoal;
	}
	if (!pInfo)
		return false;

	const Vec3 vEye = pLocal->GetEyePosition(), vOwnerVel = pLocal->m_vecVelocity();
	const float flMaxDist = Vars::Aimbot::Passtime::MaxGoalDistance.Value;
	if (flMaxDist && vEye.DistTo(pInfo->m_vCenter) > flMaxDist)
		return false;

	const Vec3 vInner = (pInfo->m_vMaxs - pInfo->m_vMins) / 2.f - Vec3::Get(tThrow.m_flRadius);
	std::vector<Vec3> vPoints = { pInfo->m_vCenter };
	for (int i = 0; i < 3; i++)
	{
		if (vInner[i] <= 0.f)
			continue;
		for (float flSign : { -1.f, 1.f })
		{
			Vec3 vPoint = pInfo->m_vCenter;
			vPoint[i] += flSign * vInner[i];
			vPoints.push_back(vPoint);
		}
	}

	const int iArc = Vars::Aimbot::Passtime::Arc.Value;
	for (bool bLob : { false, true })
	{
		if (bLob ? iArc == Vars::Aimbot::Passtime::ArcEnum::Flat : iArc == Vars::Aimbot::Passtime::ArcEnum::High)
			continue;

		for (const auto& vPoint : vPoints)
		{
			Vec3 vForward; float flTime;
			if (!SolveBall(vEye, vPoint, tThrow, vOwnerVel, bLob, vForward, flTime))
				continue;

			const float flDist = vEye.DistTo(vPoint);
			const float flError = Math::Rad2Deg(atan2f(tThrow.m_flRadius, flDist));
			const Vec3 vAngles = Math::VectorAngles(vForward);
			if (Math::CalcFov(I::EngineClient->GetViewAngles(), vAngles) > Vars::Aimbot::General::AimFOV.Value || WouldLockPass(pLocal, vEye, vAngles, tanf(Math::Deg2Rad(flError))))
				continue;

			bool bGood = true;
			for (int i = 0; i < 5 && bGood; i++)
			{
				Vec3 vTest = vAngles;
				if (i)
				{
					vTest.x += i == 1 ? flError : i == 2 ? -flError : 0.f;
					vTest.y += i == 3 ? flError : i == 4 ? -flError : 0.f;
				}

				Vec3 vTestForward; Math::AngleVectors(vTest, &vTestForward);
				float flTestTime;
				bGood = SimulateBall(pLocal, vEye, GetLaunchVelocity(tThrow, vTestForward, vOwnerVel), tThrow, *pInfo, flTestTime);
			}
			if (!bGood)
				continue;

			tPlan.m_iKind = PasstimeKindEnum::Goal;
			tPlan.m_iEntity = pGoal->entindex();
			tPlan.m_vAngle = vAngles;
			tPlan.m_vPoint = vPoint;
			tPlan.m_flTime = flTime;
			return true;
		}
	}
	return false;
}

bool CAimbotProjectile::SolvePasstimePass(CTFPlayer* pLocal, CTFPlayer* pTeammate, PasstimePlan_t& tPlan)
{
	if (!IsPassTargetValid(pLocal, pTeammate))
		return false;

	const Vec3 vEye = pLocal->GetEyePosition(), vCenter = pTeammate->GetCenter();
	if (vEye.DistTo(vCenter) > s_tEnv.m_flPassRange || !HasLockLine(pLocal, vEye, pTeammate))
		return false;

	const float flRadius = GetVar("tf_passtime_ball_sphere_radius");
	if (IsLineBlocked(pLocal, vEye, pTeammate->GetEyePosition(), pTeammate, flRadius, false))
		return false;

	const Vec3 vAngles = Math::CalcAngle(vEye, vCenter);
	if (Math::CalcFov(I::EngineClient->GetViewAngles(), vAngles) > Vars::Aimbot::General::AimFOV.Value)
		return false;

	tPlan.m_iKind = PasstimeKindEnum::Pass;
	tPlan.m_iEntity = pTeammate->entindex();
	tPlan.m_vAngle = vAngles;
	tPlan.m_vPoint = vCenter;
	tPlan.m_flTime = vEye.DistTo(vCenter) / std::max(GetVar("tf_passtime_mode_homing_speed"), 1.f);
	return true;
}

bool CAimbotProjectile::PlanPasstime(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, PasstimePlan_t& tPlan, int iForceKind, int iForceEntity)
{
	const int iMode = Vars::Aimbot::Passtime::Mode.Value;
	const bool bGoals = iMode == Vars::Aimbot::Passtime::ModeEnum::Auto || iMode == Vars::Aimbot::Passtime::ModeEnum::GoalOnly;
	const bool bPasses = iMode == Vars::Aimbot::Passtime::ModeEnum::Auto || iMode == Vars::Aimbot::Passtime::ModeEnum::PassOnly;

	BuildEnv(pLocal);
	const Vec3 vEye = pLocal->GetEyePosition();
	PasstimePlan_t tGoalPlan = {}, tPassPlan = {};

	if (bGoals && iForceKind != PasstimeKindEnum::Pass)
	{
		BallThrow_t tThrow;
		float flPrefer = Vars::Aimbot::Passtime::PreferGoalDistance.Value;
		if (!flPrefer && GetThrow(pLocal, tThrow))
			flPrefer = tThrow.m_flSpeed * tThrow.m_flSpeed / tThrow.m_flGravity;

		for (const auto& tGoal : s_tEnv.m_vGoals)
		{
			if (iForceEntity && tGoal.m_pEntity->entindex() != iForceEntity)
				continue;

			PasstimePlan_t tCurrent = {};
			if (!SolvePasstimeGoal(pLocal, tGoal.m_pEntity, tCurrent))
				continue;

			const float flDist = vEye.DistTo(tGoal.m_vCenter);
			tCurrent.m_iTier = flDist <= flPrefer ? 3 : 1;
			tCurrent.m_flKey1 = float(tGoal.m_iPoints);
			tCurrent.m_flKey2 = -flDist;
			if (tGoalPlan.m_iKind == PasstimeKindEnum::None || IsBetter(tCurrent, tGoalPlan))
				tGoalPlan = tCurrent;
		}
	}

	if (bPasses && iForceKind != PasstimeKindEnum::Goal)
	{
		float flLocalGoal = FLT_MAX;
		const bool bHasGoal = GetGoalDistance(pLocal->GetAbsOrigin(), flLocalGoal);
		const float flLocalThreat = GetThreat(pLocal, pLocal->GetAbsOrigin());
		const int iLocked = pLocal->m_hPasstimePassTarget().GetEntryIndex();
		const float flNow = TICKS_TO_TIME(pLocal->m_nTickBase());

		float flPressure = Vars::Aimbot::Passtime::PressureRange.Value;
		if (!flPressure)
			flPressure = GetNearbyRange();
		float flMinGain = Vars::Aimbot::Passtime::MinPassGain.Value;
		if (!flMinGain)
			flMinGain = GetVar("tf_passtime_ball_seek_range");

		bool bPressured = false;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			if (!pEntity->IsDormant() && pEntity->As<CTFPlayer>()->IsAlive() && pEntity->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) < flPressure)
				bPressured = true;
		}

		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
		{
			if (iForceEntity && pEntity->entindex() != iForceEntity)
				continue;
			if (pEntity->entindex() == m_tPasstime.m_iBadEntity && I::GlobalVars->curtime < m_tPasstime.m_flBadUntil)
				continue;

			auto pTeammate = pEntity->As<CTFPlayer>();
			PasstimePlan_t tCurrent = {};
			if (!SolvePasstimePass(pLocal, pTeammate, tCurrent))
				continue;

			float flGain = 0.f;
			if (bHasGoal)
			{
				float flTargetGoal;
				GetGoalDistance(pTeammate->GetAbsOrigin(), flTargetGoal);
				flGain = flLocalGoal - flTargetGoal;
			}
			if (!iForceEntity && !bPressured && flGain < flMinGain)
				continue;

			tCurrent.m_iTier = 2;
			switch (Vars::Aimbot::Passtime::PassPreference.Value)
			{
			case Vars::Aimbot::Passtime::PassPreferenceEnum::LowestThreat:
				tCurrent.m_flKey1 = flLocalThreat - GetThreat(pLocal, pTeammate->GetAbsOrigin());
				tCurrent.m_flKey2 = flGain;
				break;
			case Vars::Aimbot::Passtime::PassPreferenceEnum::GameTarget:
				tCurrent.m_flKey1 = pEntity->entindex() == iLocked || pTeammate->m_bIsTargetedForPasstimePass() || GetAskForBallTime(pTeammate) > flNow ? 1.f : 0.f;
				tCurrent.m_flKey2 = flGain;
				break;
			default:
				tCurrent.m_flKey1 = flGain;
				tCurrent.m_flKey2 = -vEye.DistTo(pTeammate->GetCenter());
			}
			if (tPassPlan.m_iKind == PasstimeKindEnum::None || IsBetter(tCurrent, tPassPlan))
				tPassPlan = tCurrent;
		}
	}

	if (tGoalPlan.m_iKind != PasstimeKindEnum::None && (tPassPlan.m_iKind == PasstimeKindEnum::None || IsBetter(tGoalPlan, tPassPlan)))
		tPlan = tGoalPlan;
	else if (tPassPlan.m_iKind != PasstimeKindEnum::None)
		tPlan = tPassPlan;
	return tPlan.m_iKind != PasstimeKindEnum::None;
}

bool CAimbotProjectile::RunPasstime(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	auto& tState = m_tPasstime;
	const int iTick = I::GlobalVars->tickcount;
	if (tState.m_iLastTick == iTick)
		return tState.m_bLastResult;

	tState.m_iLastTick = iTick;
	tState.m_bLastResult = false;
	if (!pLocal || !pWeapon || !pCmd || pWeapon->GetWeaponID() != TF_WEAPON_PASSTIME_GUN || !pLocal->m_bHasPasstimeBall() || !pLocal->IsAlive()
		|| Vars::Aimbot::Passtime::Mode.Value == Vars::Aimbot::Passtime::ModeEnum::Off || !Vars::Aimbot::General::AimType.Value)
	{
		tState.Reset(tState.m_flCooldownUntil);
		return false;
	}

	m_iWeaponID = TF_WEAPON_PASSTIME_GUN;
	const bool bAuto = Vars::Aimbot::Passtime::AutoThrow.Value;
	const bool bUserAttack = G::OriginalCmd.buttons & IN_ATTACK;
	const float flNow = I::GlobalVars->curtime;
	const int iThrowState = GetThrowState(pWeapon);

	static auto cl_updaterate = FindVar("cl_updaterate");
	const float flLockWait = F::Backtrack.GetReal() + 2.f / std::max(cl_updaterate ? cl_updaterate->GetFloat() : 0.f, 1.f) + TICK_INTERVAL;
	const int iLockTicks = TIME_TO_TICKS(flLockWait);
	const int iMaxFailTicks = TIME_TO_TICKS(F::Backtrack.GetReal()) + 1;

	auto fFinish = [&](bool bHandled) -> bool
		{
			if (bHandled)
				F::Aimbot.m_eRanType = EWeaponType::PROJECTILE;
			return tState.m_bLastResult = bHandled;
		};
	auto fShow = [&](const PasstimePlan_t& tPlan)
		{
			G::AimTarget = { tPlan.m_iEntity, iTick };
			G::AimPoint = { tPlan.m_vPoint, iTick };
		};

	if (!bAuto)
	{
		if (!bUserAttack)
		{
			tState.Reset(tState.m_flCooldownUntil);
			return false;
		}

		if (iTick - tState.m_iLastPlanTick >= kReplanInterval)
		{
			tState.m_bPlanned = PlanPasstime(pLocal, pWeapon, tState.m_tPlan);
			tState.m_iLastPlanTick = iTick;
		}
		if (!tState.m_bPlanned)
			return false;

		const PasstimePlan_t tPlan = tState.m_tPlan;
		fShow(tPlan);
		HoldAngles(pCmd, tPlan.m_vAngle);
		return fFinish(true);
	}

	if (tState.m_iPhase == PasstimePhaseEnum::Idle)
	{
		if (flNow < tState.m_flCooldownUntil || !G::CanPrimaryAttack || iThrowState != kThrowIdle && iThrowState != -1 || iTick < tState.m_iNextPlanTick)
			return false;

		PasstimePlan_t tPlan;
		if (!PlanPasstime(pLocal, pWeapon, tPlan)
			|| tPlan.m_iKind == PasstimeKindEnum::Goal && pLocal->m_hPasstimePassTarget().GetEntryIndex())
		{
			tState.m_iNextPlanTick = iTick + kIdlePlanInterval;
			return false;
		}

		tState.m_tPlan = tPlan;
		tState.m_bPlanned = true;
		tState.m_iLastPlanTick = iTick;
		tState.m_iKind = tPlan.m_iKind;
		tState.m_iEntity = tPlan.m_iEntity;
		tState.m_iTicks = 1;
		tState.m_iFailTicks = 0;
		tState.m_iPhase = tPlan.m_iKind == PasstimeKindEnum::Pass ? PasstimePhaseEnum::Aim : PasstimePhaseEnum::Hold;

		fShow(tPlan);
		HoldAngles(pCmd, tPlan.m_vAngle);
		if (tState.m_iPhase == PasstimePhaseEnum::Hold)
			pCmd->buttons |= IN_ATTACK;
		return fFinish(true);
	}

	if (tState.m_iKind != PasstimeKindEnum::Goal || iTick - tState.m_iLastPlanTick >= kReplanInterval)
	{
		PasstimePlan_t tNew;
		tState.m_bPlanned = PlanPasstime(pLocal, pWeapon, tNew, tState.m_iKind, tState.m_iEntity) && tNew.m_iEntity == tState.m_iEntity;
		tState.m_tPlan = tNew;
		tState.m_iLastPlanTick = iTick;
	}
	const PasstimePlan_t tPlan = tState.m_tPlan;
	const bool bPlanned = tState.m_bPlanned;
	tState.m_iFailTicks = bPlanned ? 0 : tState.m_iFailTicks + 1;
	tState.m_iTicks++;

	const int iLocked = pLocal->m_hPasstimePassTarget().GetEntryIndex();
	const bool bLockedOnTarget = iLocked == tState.m_iEntity;
	if (tState.m_iPhase == PasstimePhaseEnum::Aim)
	{
		if (tState.m_iFailTicks > iMaxFailTicks || tState.m_iTicks > iLockTicks)
		{
			if (tState.m_iTicks > iLockTicks)
				tState.m_iBadEntity = tState.m_iEntity, tState.m_flBadUntil = flNow + GetVar("tf_passtime_mode_homing_lock_sec");
			tState.Reset(flNow + flLockWait);
			return false;
		}

		if (bPlanned)
		{
			fShow(tPlan);
			HoldAngles(pCmd, tPlan.m_vAngle);
		}
		if (bLockedOnTarget && bPlanned)
		{
			tState.m_iPhase = PasstimePhaseEnum::Hold;
			tState.m_iTicks = 1;
			pCmd->buttons |= IN_ATTACK;
		}
		return fFinish(true);
	}

	const bool bBadLock = tState.m_iKind == PasstimeKindEnum::Goal ? iLocked != 0 : !bLockedOnTarget;
	const bool bCharging = iThrowState == kThrowCharging || iThrowState == kThrowCharged;
	if (tState.m_iFailTicks > iMaxFailTicks || bBadLock && bCharging)
	{
		if (bCharging)
			pCmd->buttons = (pCmd->buttons & ~IN_ATTACK) | IN_ATTACK2;
		else
			pCmd->buttons &= ~IN_ATTACK;
		tState.Reset(flNow + flLockWait);
		return fFinish(true);
	}

	if (bPlanned)
	{
		fShow(tPlan);
		HoldAngles(pCmd, tPlan.m_vAngle);
	}
	else
		HoldAngles(pCmd, I::EngineClient->GetViewAngles());

	const bool bReleased = iThrowState == -1 ? tState.m_iTicks > 1 : bCharging;
	if (!bReleased || bBadLock || !bPlanned)
	{
		pCmd->buttons |= IN_ATTACK;
		if (tState.m_iTicks > iLockTicks && !bCharging && iThrowState != -1)
			tState.Reset(flNow + flLockWait);
	}
	else
	{
		pCmd->buttons &= ~IN_ATTACK;
		tState.Reset(flNow + flLockWait);
	}
	return fFinish(true);
}
