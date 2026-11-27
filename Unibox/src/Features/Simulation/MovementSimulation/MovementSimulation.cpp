#include "MovementSimulation.h"

#include "../CartSim/CartSim.h"
#include "../../Aimbot/AimbotProjectile/ProjProfiler.h"
#include <array>
#include <numeric>

static constexpr float NonGroundVelocity = 250.f;
static constexpr float JumpVelocity = 289.f;
static constexpr float DuckSpeedCrop = 0.33333333f;
static constexpr float MovementMaxSpeed = 520.f;
static constexpr float EyeTurnFade = 0.79f;
static constexpr float EyeTurnMax = 12.f;
static constexpr float EyeTurnKept = 1.0f;
static constexpr float SweepFade = 0.97f;
static constexpr int MaxRecords = 66;
static constexpr int MaxRecordGap = 8;

struct FitSample_t
{
	Vec3 m_vOrigin = {};
	Vec3 m_vVelocity = {};
	int m_iTicks = 1;
	int m_iAge = 0;
	float m_flEyeYaw = 0.f;
	float m_flMaxSpeed = 0.f;
	bool m_bDucked = false;
	float m_flWeight = 1.f;
	float m_flEyeDelta = 0.f;
	Vec3 m_vEyeDir = {};
};

static inline float Heading(const Vec3& vVelocity)
{
	return Math::Rad2Deg(atan2f(vVelocity.y, vVelocity.x));
}

static inline Vec3 YawDirection(float flYaw)
{
	const float flRadians = Math::Deg2Rad(flYaw);
	return { cosf(flRadians), sinf(flRadians), 0.f };
}

static inline int GetMoveMode(CTFPlayer* pPlayer)
{
	if (pPlayer->m_nWaterLevel() >= 2)
		return MoveEnum::Swim;
	if (pPlayer->IsOnGround())
		return MoveEnum::Ground;
	return MoveEnum::Air;
}

static inline float GroundCap(float flMaxSpeed, bool bLostFooting, bool bCharging, float flChargeSpeed)
{
	const float flLimit = bCharging ? flChargeSpeed : MovementMaxSpeed;
	if (bLostFooting)
		return flLimit;
	return flMaxSpeed > 0.f ? std::min(flMaxSpeed, flLimit) : flLimit;
}

static inline float AirSurface(float flVelocityZ, float flGrounded, float flAirCurrent)
{
	return flVelocityZ > 0.f && flVelocityZ <= NonGroundVelocity ? 0.25f * flAirCurrent : flGrounded;
}

static inline void ClampVelocity(Vec3& vVelocity, float flMax)
{
	vVelocity.x = std::clamp(vVelocity.x, -flMax, flMax);
	vVelocity.y = std::clamp(vVelocity.y, -flMax, flMax);
	vVelocity.z = std::clamp(vVelocity.z, -flMax, flMax);
}

static inline void ClampSpeed2D(Vec3& vVelocity, float flMax)
{
	const float flSpeed = vVelocity.Length2D();
	if (flMax <= 0.f || flSpeed <= flMax)
		return;

	const float flScale = flMax / flSpeed;
	vVelocity.x *= flScale, vVelocity.y *= flScale;
}

static inline void ClampBackSpeed(Vec3& vVelocity, float flViewYaw, float flMax, const MovePhysics_t& tPhysics, const Vec3* pForward = nullptr)
{
	if (tPhysics.m_flClampBack >= 1.f || vVelocity.Length2D() <= tPhysics.m_flClampBackMin)
		return;

	const Vec3 vForward = pForward ? *pForward : YawDirection(flViewYaw);
	const float flForward = vVelocity.x * vForward.x + vVelocity.y * vForward.y;
	if (flForward >= 0.f)
		return;

	const float flRight = vVelocity.x * vForward.y - vVelocity.y * vForward.x;
	const float flBack = std::max(flForward, -flMax * tPhysics.m_flClampBack);
	vVelocity.x = vForward.x * flBack + vForward.y * flRight;
	vVelocity.y = vForward.y * flBack - vForward.x * flRight;
	ClampSpeed2D(vVelocity, flMax);
}

static inline void GroundFriction(Vec3& vVelocity, float flSurface, const MovePhysics_t& tPhysics)
{
	const float flSpeed = vVelocity.Length();
	if (flSpeed < 0.1f)
		return;

	const float flControl = std::max(flSpeed, tPhysics.m_flStopSpeed);
	const float flNewSpeed = std::max(0.f, flSpeed - flControl * tPhysics.m_flFriction * flSurface * TICK_INTERVAL);
	if (flNewSpeed != flSpeed)
		vVelocity *= flNewSpeed / flSpeed;
}

static inline void GroundAccelerate(Vec3& vVelocity, const Vec3& vWish, float flWishSpeed, float flSurface, bool bLostFooting, const MovePhysics_t& tPhysics)
{
	if (flWishSpeed <= 0.f)
		return;

	const float flMissing = flWishSpeed - vVelocity.Dot(vWish);
	if (flMissing <= 0.f)
		return;

	float flAccelerate = tPhysics.m_flAccelerate;
	const float flFriction = tPhysics.m_flFriction;
	if (flWishSpeed < 100.f * flFriction / flAccelerate && !bLostFooting)
		flAccelerate = std::max(vVelocity.Length(), tPhysics.m_flStopSpeed) * flFriction / flWishSpeed + 1.f;

	vVelocity += vWish * std::min(flAccelerate * flWishSpeed * TICK_INTERVAL * flSurface, flMissing);
}

static inline void AirAccelerate(Vec3& vVelocity, const Vec3& vWish, float flWishSpeed, float flCap, float flAccelerate, float flSurface)
{
	if (flWishSpeed <= 0.f)
		return;

	const float flMissing = std::min(flWishSpeed, flCap) - (vVelocity.x * vWish.x + vVelocity.y * vWish.y);
	if (flMissing <= 0.f)
		return;

	const float flGain = std::min(flAccelerate * flWishSpeed * TICK_INTERVAL * flSurface, flMissing);
	vVelocity.x += vWish.x * flGain, vVelocity.y += vWish.y * flGain;
}

static inline Vec3 ClipVelocity(const Vec3& vVelocity, const Vec3& vNormal, float flRedirect)
{
	Vec3 vOut = vVelocity - vNormal * vVelocity.Dot(vNormal);
	if (const float flAdjust = vOut.Dot(vNormal); flAdjust < 0.f)
		vOut -= vNormal * flAdjust;

	if (flRedirect > 0.f)
	{
		const float flLength = vOut.Length();
		if (flLength > 0.f)
			vOut *= (-vVelocity.Dot(vNormal) * flRedirect + flLength) / flLength;
	}
	return vOut;
}

static inline void TraceMove(const MoveStorage& tMoveStorage, const Vec3& vStart, const Vec3& vEnd, CGameTrace& trace, const Vec3* pMins = nullptr, const Vec3* pMaxs = nullptr)
{
	CTraceFilterCollideable filter(tMoveStorage.m_pPlayer);
	filter.m_iType = SKIP_CHECK;
	filter.m_iPlayer = PLAYER_NONE;
	filter.m_iObject = OBJECT_DEFAULT;
	filter.m_iTeam = tMoveStorage.m_iTeam;
	SDK::TraceHull(vStart, vEnd, pMins ? *pMins : tMoveStorage.m_vMins, pMaxs ? *pMaxs : tMoveStorage.m_vMaxs, tMoveStorage.m_nMask, &filter, &trace);
}

static inline int CheckWater(const MoveStorage& tMoveStorage)
{
	const Vec3& vOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	auto fSubmerged = [&](float flHeight)
		{
			return I::EngineTrace->GetPointContents({ vOrigin.x, vOrigin.y, flHeight }) & MASK_WATER;
		};

	if (!fSubmerged(vOrigin.z + tMoveStorage.m_vMins.z + 1.f))
		return 0;
	if (fSubmerged(vOrigin.z + tMoveStorage.m_vViewOffset.z))
		return 3;
	if (fSubmerged(vOrigin.z + (tMoveStorage.m_vMins.z + tMoveStorage.m_vMaxs.z) * 0.5f + 12.f))
		return 2;
	return 1;
}

static bool CheckStuck(MoveStorage& tMoveStorage)
{
	static constexpr float flNear = 0.125f, flFar = 2.f;
	static const Vec3 aOffsets[] = {
		{ 0.f, 0.f, flNear }, { 0.f, 0.f, -flNear }, { flNear, 0.f, 0.f }, { -flNear, 0.f, 0.f }, { 0.f, flNear, 0.f }, { 0.f, -flNear, 0.f },
		{ flNear, flNear, flNear }, { -flNear, flNear, flNear }, { flNear, -flNear, flNear }, { -flNear, -flNear, flNear },
		{ flNear, flNear, -flNear }, { -flNear, flNear, -flNear }, { flNear, -flNear, -flNear }, { -flNear, -flNear, -flNear },
		{ 0.f, 0.f, 1.f }, { 0.f, 0.f, 6.f }, { flFar, 0.f, 0.f }, { -flFar, 0.f, 0.f }, { 0.f, flFar, 0.f }, { 0.f, -flFar, 0.f },
		{ flFar, flFar, 1.f }, { -flFar, flFar, 1.f }, { flFar, -flFar, 1.f }, { -flFar, -flFar, 1.f },
		{ flFar, flFar, 6.f }, { -flFar, flFar, 6.f }, { flFar, -flFar, 6.f }, { -flFar, -flFar, 6.f }
	};

	Vec3& vOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;

	CGameTrace trace = {};
	TraceMove(tMoveStorage, vOrigin, vOrigin, trace);
	if (!trace.startsolid)
		return false;

	for (auto& vOffset : aOffsets)
	{
		TraceMove(tMoveStorage, vOrigin + vOffset, vOrigin + vOffset, trace);
		if (trace.startsolid)
			continue;

		vOrigin += vOffset;
		tMoveStorage.m_iWaterLevel = CheckWater(tMoveStorage);
		return false;
	}
	return true;
}

static bool TouchGroundInQuadrants(const MoveStorage& tMoveStorage, const Vec3& vStart, const Vec3& vEnd, CGameTrace& tGround)
{
	const Vec3& vMins = tMoveStorage.m_vMins, &vMaxs = tMoveStorage.m_vMaxs;
	const Vec3 aMins[4] = {
		vMins,
		{ std::max(0.f, vMins.x), std::max(0.f, vMins.y), vMins.z },
		{ vMins.x, std::max(0.f, vMins.y), vMins.z },
		{ std::max(0.f, vMins.x), vMins.y, vMins.z }
	};
	const Vec3 aMaxs[4] = {
		{ std::min(0.f, vMaxs.x), std::min(0.f, vMaxs.y), vMaxs.z },
		vMaxs,
		{ std::min(0.f, vMaxs.x), vMaxs.y, vMaxs.z },
		{ vMaxs.x, std::min(0.f, vMaxs.y), vMaxs.z }
	};

	for (int i = 0; i < 4; i++)
	{
		CGameTrace trace = {};
		TraceMove(tMoveStorage, vStart, vEnd, trace, &aMins[i], &aMaxs[i]);
		if (trace.m_pEnt && trace.plane.normal.z >= 0.7f)
		{
			const float flFraction = tGround.fraction;
			const Vec3 vEndPos = tGround.endpos;
			tGround = trace;
			tGround.fraction = flFraction, tGround.endpos = vEndPos;
			return true;
		}
	}
	return false;
}

static void CategorizePosition(MoveStorage& tMoveStorage)
{
	auto& tMoveData = tMoveStorage.m_MoveData;
	auto& tPhysics = tMoveStorage.m_tPhysics;
	Vec3& vOrigin = tMoveData.m_vecAbsOrigin;
	Vec3& vVelocity = tMoveData.m_vecVelocity;

	const bool bWasGround = tMoveStorage.m_bOnGround;
	tMoveStorage.m_flSurfaceFriction = 1.f;
	tMoveStorage.m_iWaterLevel = CheckWater(tMoveStorage);

	if (vVelocity.z > NonGroundVelocity)
	{
		tMoveStorage.m_bOnGround = false;
		return;
	}

	const bool bRetain = bWasGround && tMoveStorage.m_iWaterLevel < 3;
	const Vec3 vProbe = { vOrigin.x, vOrigin.y, vOrigin.z - 2.f - (bRetain ? tPhysics.m_flStepSize : 0.f) };

	CGameTrace trace = {};
	TraceMove(tMoveStorage, vOrigin, vProbe, trace);
	CGameTrace tGround = trace;
	const bool bWalkable = trace.fraction != 1.f && trace.plane.normal.z >= 0.7f;

	bool bInAir = false, bSteep = false;
	float flFrictionMult = 1.f;
	if (tMoveStorage.m_bLostFooting)
	{
		const float flAway = vVelocity.Dot(trace.plane.normal);
		if (flAway > 0.f)
			bInAir = true;
		else if ((vVelocity - trace.plane.normal * flAway).Length() >= tPhysics.m_flRestick)
			flFrictionMult = tPhysics.m_flLostFootingFriction;
		else
			tMoveStorage.m_bLostFooting = false;
	}

	if (!bInAir && !bWalkable && ((trace.fraction == 1.f && !trace.startsolid) || !TouchGroundInQuadrants(tMoveStorage, vOrigin, vProbe, tGround)))
		bInAir = bSteep = true;

	if (bInAir || !tGround.m_pEnt)
	{
		tMoveStorage.m_bOnGround = false;
		if (bSteep && vVelocity.z > 0.f)
			tMoveStorage.m_flSurfaceFriction = 0.25f;
		if (tMoveStorage.m_bAirCurrent)
			tMoveStorage.m_flSurfaceFriction *= tPhysics.m_flAirCurrentFriction;
		return;
	}

	tMoveStorage.m_bOnGround = true;
	tMoveStorage.m_bAirCurrent = false;
	tMoveStorage.m_flSurfaceFriction = flFrictionMult;
	vVelocity.z = 0.f;

	if (bRetain && bWalkable && trace.fraction > 0.f && trace.fraction < 1.f && !trace.startsolid
		&& fabsf(vOrigin.z - trace.endpos.z) > 0.5f / 16.f)
	{
		vOrigin.z = trace.endpos.z;
		tMoveStorage.m_iWaterLevel = CheckWater(tMoveStorage);
	}
}

static void TryPlayerMove(MoveStorage& tMoveStorage, float flRedirect = 0.f)
{
	static constexpr int iMaxBumps = 4, iMaxPlanes = 5;

	Vec3& vOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	Vec3& vVelocity = tMoveStorage.m_MoveData.m_vecVelocity;

	Vec3 aPlanes[iMaxPlanes] = {};
	int iPlanes = 0;
	const Vec3 vPrimal = vVelocity;
	Vec3 vOriginal = vVelocity;
	float flTimeLeft = TICK_INTERVAL, flAllFraction = 0.f;

	for (int iBump = 0; iBump < iMaxBumps; iBump++)
	{
		if (vVelocity.IsZero(0.00001f))
			break;

		CGameTrace trace = {};
		TraceMove(tMoveStorage, vOrigin, vOrigin + vVelocity * flTimeLeft, trace);
		if (trace.allsolid)
		{
			vVelocity = {};
			return;
		}

		flAllFraction += trace.fraction;
		if (trace.fraction > 0.f)
		{
			if (iBump > 0 && trace.fraction == 1.f)
			{
				CGameTrace tStuck = {};
				TraceMove(tMoveStorage, trace.endpos, trace.endpos, tStuck);
				if (tStuck.startsolid || tStuck.fraction != 1.f)
				{
					vVelocity = {};
					break;
				}
			}
			vOrigin = trace.endpos;
			vOriginal = vVelocity;
			iPlanes = 0;
		}
		if (trace.fraction == 1.f)
			break;

		flTimeLeft -= flTimeLeft * trace.fraction;
		if (iPlanes >= iMaxPlanes)
		{
			vVelocity = {};
			break;
		}

		aPlanes[iPlanes++] = trace.plane.normal;
		if (iPlanes == 1 && !tMoveStorage.m_bOnGround)
		{
			vVelocity = vOriginal = ClipVelocity(vOriginal, aPlanes[0], flRedirect);
			continue;
		}

		int i = 0;
		for (; i < iPlanes; i++)
		{
			vVelocity = ClipVelocity(vOriginal, aPlanes[i], flRedirect);

			int j = 0;
			for (; j < iPlanes; j++)
			{
				if (j != i && vVelocity.Dot(aPlanes[j]) < 0.f)
					break;
			}
			if (j == iPlanes)
				break;
		}

		if (i == iPlanes)
		{
			if (iPlanes != 2)
			{
				vVelocity = {};
				break;
			}

			const Vec3 vCrease = aPlanes[0].Cross(aPlanes[1]).Normalized();
			vVelocity = vCrease * vCrease.Dot(vVelocity);
		}

		if (vVelocity.Dot(vPrimal) <= 0.f)
		{
			vVelocity = {};
			break;
		}
	}

	if (!flAllFraction)
		vVelocity = {};
}

static void StepMove(MoveStorage& tMoveStorage)
{
	Vec3& vOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	Vec3& vVelocity = tMoveStorage.m_MoveData.m_vecVelocity;
	const float flStep = tMoveStorage.m_tPhysics.m_flStepSize + DIST_EPSILON;
	const Vec3 vStartOrigin = vOrigin, vStartVelocity = vVelocity;

	CGameTrace trace = {};
	TraceMove(tMoveStorage, vStartOrigin, vStartOrigin + Vec3(0.f, 0.f, flStep), trace);
	if (!trace.startsolid && !trace.allsolid)
		vOrigin = trace.endpos;

	TryPlayerMove(tMoveStorage);

	TraceMove(tMoveStorage, vOrigin, vOrigin - Vec3(0.f, 0.f, flStep), trace);
	if (!trace.startsolid && !trace.allsolid)
		vOrigin = trace.endpos;

	if (trace.fraction != 1.f && trace.plane.normal.z < 0.7f || vOrigin == vStartOrigin)
	{
		vOrigin = vStartOrigin, vVelocity = vStartVelocity;
		TryPlayerMove(tMoveStorage);
	}
}

static void GetWish(const MoveStorage& tMoveStorage, bool bGround, Vec3& vWish, float& flWishSpeed)
{
	const auto& tMoveData = tMoveStorage.m_MoveData;
	const auto& tPhysics = tMoveStorage.m_tPhysics;
	const auto& tController = bGround ? tMoveStorage.m_tGround : tMoveStorage.m_tAir;
	const float flCrop = bGround && tMoveStorage.m_bDucked ? DuckSpeedCrop : 1.f;
	const float flCap = bGround
		? GroundCap(tMoveData.m_flMaxSpeed, tMoveStorage.m_bLostFooting, tMoveStorage.m_bCharging, tPhysics.m_flChargeSpeed)
		: tMoveData.m_flMaxSpeed;

	vWish = {}, flWishSpeed = 0.f;
	switch (tController.m_iModel)
	{
	case MoveModelEnum::Input:
	{
		Vec3 vForward, vRight; Math::AngleVectors({ 0.f, tMoveData.m_vecViewAngles.y, 0.f }, &vForward, &vRight);
		const Vec3 vWishVel = vForward * tMoveData.m_flForwardMove + vRight * tMoveData.m_flSideMove;
		flWishSpeed = vWishVel.Length2D();
		if (flWishSpeed <= 0.f)
			return;
		vWish = { vWishVel.x / flWishSpeed, vWishVel.y / flWishSpeed, 0.f };
		flWishSpeed = std::min(flWishSpeed, tMoveData.m_flMaxSpeed) * flCrop;
		return;
	}
	case MoveModelEnum::World:
		vWish = YawDirection(tController.m_flYaw);
		flWishSpeed = tController.m_flScale * flCap * flCrop;
		return;
	case MoveModelEnum::View:
		vWish = YawDirection(tMoveStorage.m_flViewYaw + tController.m_flYaw);
		flWishSpeed = tController.m_flScale * flCap * flCrop;
		return;
	case MoveModelEnum::Sweep:
		vWish = YawDirection(Heading(tMoveData.m_vecVelocity) + tController.m_flYaw * tMoveStorage.m_flSweepKept);
		flWishSpeed = tController.m_flScale * flCap;
		return;
	case MoveModelEnum::Charge:
		vWish = YawDirection(tMoveStorage.m_flViewYaw);
		flWishSpeed = tPhysics.m_flChargeSpeed * flCrop;
		return;
	case MoveModelEnum::Neural:
	{
		const auto tKnot = tMoveStorage.m_tNeural.SampleAtTick(tMoveStorage.m_iNeuralStep + 1);
		Vec3 vForward, vRight;
		Math::AngleVectors({ 0.f, tMoveStorage.m_flViewYaw, 0.f }, &vForward, &vRight);
		const Vec3 vWishVel = vForward * tKnot.m_flWishFwd + vRight * tKnot.m_flWishSide;
		const float flMag = vWishVel.Length2D();
		if (flMag <= 1e-5f)
			return;
		vWish = { vWishVel.x / flMag, vWishVel.y / flMag, 0.f };
		const float flNeuralCrop = (bGround && (tMoveStorage.m_bDucked || tKnot.m_flDuckProb >= 0.5f)) ? DuckSpeedCrop : 1.f;
		flWishSpeed = std::min(flMag, 1.f) * flCap * flNeuralCrop;
		return;
	}
	}
}

static void WalkMove(MoveStorage& tMoveStorage)
{
	auto& tMoveData = tMoveStorage.m_MoveData;
	auto& tPhysics = tMoveStorage.m_tPhysics;
	Vec3& vVelocity = tMoveData.m_vecVelocity;

	Vec3 vWish; float flWishSpeed; GetWish(tMoveStorage, true, vWish, flWishSpeed);
	GroundAccelerate(vVelocity, vWish, flWishSpeed, tMoveStorage.m_flSurfaceFriction, tMoveStorage.m_bLostFooting, tPhysics);

	const float flCap = GroundCap(tMoveData.m_flMaxSpeed, tMoveStorage.m_bLostFooting, tMoveStorage.m_bCharging, tPhysics.m_flChargeSpeed);
	ClampSpeed2D(vVelocity, flCap);
	ClampBackSpeed(vVelocity, tMoveData.m_vecViewAngles.y, flCap, tPhysics);

	if (vVelocity.Length() < 1.f)
	{
		vVelocity = {};
		return;
	}

	Vec3& vOrigin = tMoveData.m_vecAbsOrigin;
	CGameTrace trace = {};
	TraceMove(tMoveStorage, vOrigin, { vOrigin.x + vVelocity.x * TICK_INTERVAL, vOrigin.y + vVelocity.y * TICK_INTERVAL, vOrigin.z }, trace);
	if (trace.fraction == 1.f)
	{
		vOrigin = trace.endpos;
		return;
	}

	StepMove(tMoveStorage);
}

static void AirMove(MoveStorage& tMoveStorage)
{
	auto& tPhysics = tMoveStorage.m_tPhysics;
	Vec3& vVelocity = tMoveStorage.m_MoveData.m_vecVelocity;

	Vec3 vWish; float flWishSpeed; GetWish(tMoveStorage, false, vWish, flWishSpeed);
	const float flAirCurrent = tMoveStorage.m_bAirCurrent ? tPhysics.m_flAirCurrentFriction : 1.f;
	const float flAccelerate = tPhysics.m_flAirAccelerate * (tMoveStorage.m_bAirCurrent ? tPhysics.m_flAirCurrentControl : 1.f);
	AirAccelerate(vVelocity, vWish, flWishSpeed, tPhysics.m_flAirCap, flAccelerate, tMoveStorage.m_flSurfaceFriction * flAirCurrent);

	TryPlayerMove(tMoveStorage, tMoveStorage.m_bAirCurrent ? std::clamp(1.f - tPhysics.m_flAirCurrentFriction, 0.f, 1.f) : 0.f);
}

static void WaterMove(MoveStorage& tMoveStorage)
{
	auto& tPhysics = tMoveStorage.m_tPhysics;
	Vec3& vVelocity = tMoveStorage.m_MoveData.m_vecVelocity;

	Vec3 vWish = {};
	float flWishSpeed = 0.f;
	const bool bUseGround = tMoveStorage.m_bOnGround || tMoveStorage.m_tAir.m_iModel == MoveModelEnum::None;
	GetWish(tMoveStorage, bUseGround, vWish, flWishSpeed);
	if (flWishSpeed > 0.f)
	{
		flWishSpeed = std::min(flWishSpeed, tMoveStorage.m_MoveData.m_flMaxSpeed) * 0.8f;
	}
	else if (tMoveStorage.m_bLocal && tMoveStorage.m_MoveData.m_flUpMove != 0.f)
	{
		vWish = { 0.f, 0.f, tMoveStorage.m_MoveData.m_flUpMove > 0.f ? 1.f : -1.f };
		flWishSpeed = std::min(fabsf(tMoveStorage.m_MoveData.m_flUpMove), tMoveStorage.m_MoveData.m_flMaxSpeed) * 0.8f;
	}
	else
	{
		vWish = { 0.f, 0.f, -1.f };
		flWishSpeed = 60.f * 0.8f;
	}

	float flNewSpeed = 0.f;
	const float flSpeed = vVelocity.Length();
	if (flSpeed > 0.f)
	{
		const float flDrag = TICK_INTERVAL * flSpeed * tPhysics.m_flFriction * tMoveStorage.m_flSurfaceFriction;
		flNewSpeed = std::max(0.f, flSpeed - flDrag);
		if (flNewSpeed < 0.1f)
			flNewSpeed = 0.f;
		vVelocity *= flNewSpeed / flSpeed;
	}

	if (flWishSpeed >= 0.1f)
	{
		const float flMissing = flWishSpeed - flNewSpeed;
		if (flMissing > 0.f)
			vVelocity += vWish * std::min(tPhysics.m_flAccelerate * flWishSpeed * TICK_INTERVAL * tMoveStorage.m_flSurfaceFriction, flMissing);
	}

	if (tMoveStorage.m_bOnGround)
		StepMove(tMoveStorage);
	else
		TryPlayerMove(tMoveStorage);
}

static void StepFit(Vec3& vPosition, Vec3& vVelocity, const MoveController_t& tController, const Vec3& vCtrlDir, bool bGround, float flEyeYaw, const Vec3* pEyeDir, float flMaxSpeed, bool bDucked, const MovePhysics_t& tPhysics, float flSweepKept = 1.f)
{
	const float flCap = bGround ? GroundCap(flMaxSpeed, false, false, 0.f) * (bDucked ? DuckSpeedCrop : 1.f) : flMaxSpeed;

	Vec3 vWish = {}; float flWishSpeed = 0.f;
	switch (tController.m_iModel)
	{
	case MoveModelEnum::World:
		vWish = vCtrlDir;
		flWishSpeed = tController.m_flScale * flCap;
		break;
	case MoveModelEnum::View:
		vWish = pEyeDir
			? Vec3{ pEyeDir->x * vCtrlDir.x - pEyeDir->y * vCtrlDir.y, pEyeDir->x * vCtrlDir.y + pEyeDir->y * vCtrlDir.x, 0.f }
			: YawDirection(flEyeYaw + tController.m_flYaw);
		flWishSpeed = tController.m_flScale * flCap;
		break;
	case MoveModelEnum::Sweep:
	{
		if (flSweepKept == 1.f)
		{
			const float flVel = vVelocity.Length2D();
			const Vec3 vVelDir = flVel > 0.f ? Vec3(vVelocity.x / flVel, vVelocity.y / flVel, 0.f) : Vec3(1.f, 0.f, 0.f);
			vWish = { vVelDir.x * vCtrlDir.x - vVelDir.y * vCtrlDir.y, vVelDir.x * vCtrlDir.y + vVelDir.y * vCtrlDir.x, 0.f };
		}
		else
			vWish = YawDirection(Heading(vVelocity) + tController.m_flYaw * flSweepKept);
		flWishSpeed = tController.m_flScale * flCap;
		break;
	}
	}

	if (bGround)
	{
		const float flGroundCap = GroundCap(flMaxSpeed, false, false, 0.f);
		GroundFriction(vVelocity, 1.f, tPhysics);
		GroundAccelerate(vVelocity, vWish, flWishSpeed, 1.f, false, tPhysics);
		ClampSpeed2D(vVelocity, flGroundCap);
		ClampBackSpeed(vVelocity, flEyeYaw, flGroundCap, tPhysics, pEyeDir);
		vPosition += vVelocity * TICK_INTERVAL;
		return;
	}

	vVelocity.z -= tPhysics.m_flGravity * TICK_INTERVAL * 0.5f;
	AirAccelerate(vVelocity, vWish, flWishSpeed, tPhysics.m_flAirCap, tPhysics.m_flAirAccelerate, AirSurface(vVelocity.z, 1.f, 1.f));
	vPosition += vVelocity * TICK_INTERVAL;
	vVelocity.z -= tPhysics.m_flGravity * TICK_INTERVAL * 0.5f;
}

static float EvaluateFit(const std::vector<FitSample_t>& vSamples, bool bGround, const MoveController_t& tController, const MovePhysics_t& tPhysics)
{
	const Vec3 vCtrlDir = YawDirection(tController.m_flYaw);
	Vec3 vVelocity = vSamples.front().m_vVelocity;
	if (bGround)
		vVelocity.z = 0.f;
	else
		vVelocity.z -= tPhysics.m_flGravity * TICK_INTERVAL * vSamples.front().m_iTicks * 0.5f;

	float flError = 0.f, flWeight = 0.f;
	float flSweepKept = 1.f;
	for (size_t i = 1; i < vSamples.size(); i++)
	{
		auto& tFrom = vSamples[i - 1], &tTo = vSamples[i];
		const float flEyeDelta = tTo.m_flEyeDelta;

		Vec3 vPosition = tFrom.m_vOrigin;
		for (int n = 1; n <= tTo.m_iTicks; n++)
		{
			StepFit(vPosition, vVelocity, tController, vCtrlDir, bGround, tFrom.m_flEyeYaw + flEyeDelta * n / tTo.m_iTicks, n == tTo.m_iTicks ? &tTo.m_vEyeDir : nullptr, tTo.m_flMaxSpeed, tTo.m_bDucked, tPhysics, flSweepKept);
			if (!bGround)
				flSweepKept *= SweepFade;
		}

		const float flSampleWeight = tTo.m_flWeight;
		flError += (vPosition - tTo.m_vOrigin).Length2DSqr() * flSampleWeight;
		flWeight += flSampleWeight;
	}
	return flWeight > 0.f ? flError / flWeight : std::numeric_limits<float>::max();
}

static constexpr int ChangeTicks = 3;
static constexpr float ChangeEvidence = 16.f;
static constexpr float ChiMedian = 1.3862944f;
static constexpr int KeyCount = 9;
static const MoveController_t aKeys[KeyCount] = {
	{ MoveModelEnum::View, 0.f }, { MoveModelEnum::View, 45.f }, { MoveModelEnum::View, 90.f }, { MoveModelEnum::View, 135.f },
	{ MoveModelEnum::View, 180.f }, { MoveModelEnum::View, -135.f }, { MoveModelEnum::View, -90.f }, { MoveModelEnum::View, -45.f },
	{ MoveModelEnum::None }
};

static float EvaluateGap(const FitSample_t& tFrom, const FitSample_t& tTo, bool bGround, const MoveController_t& tController, const Vec3& vCtrlDir, const MovePhysics_t& tPhysics)
{
	static constexpr float flQuantize = PLAYER_ORIGIN_COMPRESSION * PLAYER_ORIGIN_COMPRESSION / 12.f;

	Vec3 vVelocity = tFrom.m_vVelocity, vPosition = tFrom.m_vOrigin;
	if (bGround)
		vVelocity.z = 0.f;
	else
		vVelocity.z -= tPhysics.m_flGravity * TICK_INTERVAL * tFrom.m_iTicks * 0.5f;

	const float flEyeDelta = tTo.m_flEyeDelta;
	float flSweepKept = 1.f;
	for (int n = 1; n <= tTo.m_iTicks; n++)
	{
		StepFit(vPosition, vVelocity, tController, vCtrlDir, bGround, tFrom.m_flEyeYaw + flEyeDelta * n / tTo.m_iTicks, n == tTo.m_iTicks ? &tTo.m_vEyeDir : nullptr, tTo.m_flMaxSpeed, tTo.m_bDucked, tPhysics, flSweepKept);
		if (!bGround)
			flSweepKept *= SweepFade;
	}

	const float flRatio = float(tTo.m_iTicks) / tFrom.m_iTicks;
	return (vPosition - tTo.m_vOrigin).Length2DSqr() / (flQuantize * (1.f + (1.f + flRatio) * (1.f + flRatio) + flRatio * flRatio));
}

static void FitChange(const std::vector<FitSample_t>& vSamples, bool bGround, const MovePhysics_t& tPhysics, MoveController_t& tBest, float& flBest)
{
	static const Vec3 aKeyDirs[KeyCount] = {
		YawDirection(aKeys[0].m_flYaw), YawDirection(aKeys[1].m_flYaw), YawDirection(aKeys[2].m_flYaw),
		YawDirection(aKeys[3].m_flYaw), YawDirection(aKeys[4].m_flYaw), YawDirection(aKeys[5].m_flYaw),
		YawDirection(aKeys[6].m_flYaw), YawDirection(aKeys[7].m_flYaw), YawDirection(aKeys[8].m_flYaw)
	};
	const Vec3 vBestDir = YawDirection(tBest.m_flYaw);

	const size_t iLast = vSamples.size() - 1;
	std::vector<std::array<float, KeyCount + 1>> vSums(vSamples.size());
	for (size_t i = 1; i <= iLast; i++)
	{
		for (int k = 0; k < KeyCount; k++)
			vSums[i][k] = vSums[i - 1][k] + EvaluateGap(vSamples[i - 1], vSamples[i], bGround, aKeys[k], aKeyDirs[k], tPhysics);
		vSums[i][KeyCount] = vSums[i - 1][KeyCount] + EvaluateGap(vSamples[i - 1], vSamples[i], bGround, tBest, vBestDir, tPhysics);
	}
	auto fSum = [&](size_t iFrom, size_t iTo, int k) { return vSums[iTo][k] - vSums[iFrom - 1][k]; };

	std::vector<float> vResidual(iLast);
	for (size_t i = 1; i <= iLast; i++)
	{
		vResidual[i - 1] = fSum(i, i, 0);
		for (int k = 1; k < KeyCount; k++)
			vResidual[i - 1] = std::min(vResidual[i - 1], fSum(i, i, k));
	}
	std::nth_element(vResidual.begin(), vResidual.begin() + vResidual.size() / 2, vResidual.end());
	const float flThreshold = ChangeEvidence * std::max(vResidual[vResidual.size() / 2] / ChiMedian, 1.f);

	size_t iSince = 1, iInit = 1;
	for (int iTicks = vSamples[1].m_iTicks; iInit < iLast && iTicks < ChangeTicks;)
		iTicks += vSamples[++iInit].m_iTicks;

	int iKey = 0;
	for (int k = 1; k < KeyCount; k++)
	{
		if (fSum(1, iInit, k) < fSum(1, iInit, iKey))
			iKey = k;
	}

	int iSeen = 0;
	for (size_t i = iInit + 1; i <= iLast; i++)
	{
		int iTop = -1; size_t iTopFrom = i; float flTop = flThreshold;
		int iTicks = 0;
		for (size_t j = i; j > iSince && iTicks < ChangeTicks; j--)
		{
			iTicks += vSamples[j].m_iTicks;
			for (int k = 0; k < KeyCount; k++)
			{
				if (const float flEvidence = fSum(j, i, iKey) - fSum(j, i, k); flEvidence >= flTop)
					iTop = k, iTopFrom = j, flTop = flEvidence;
			}
		}
		if (iTop != -1)
			iSeen |= 1 << iKey, iKey = iTop, iSince = iTopFrom;
	}

	const float flSuffixMean = fSum(iSince, iLast, iKey) / float(iLast - iSince + 1);
	if (iSeen & 1 << iKey || flSuffixMean > 128.f || fSum(iSince, iLast, KeyCount) - fSum(iSince, iLast, iKey) < flThreshold)
		return;

	tBest = aKeys[iKey];
	if (bGround && tBest.m_iModel == MoveModelEnum::None)
		tBest.m_iModel = MoveModelEnum::Stop;
	flBest = std::min(flBest, flSuffixMean * (PLAYER_ORIGIN_COMPRESSION * PLAYER_ORIGIN_COMPRESSION * 0.5f));
}

static MoveController_t OptimizeFit(const std::vector<FitSample_t>& vSamples, bool bGround, std::initializer_list<MoveController_t> lSeeds, bool bScale, const MovePhysics_t& tPhysics, float& flError)
{
	MoveController_t tBest = *lSeeds.begin();
	flError = std::numeric_limits<float>::max();
	for (auto& tSeed : lSeeds)
	{
		const float flSeedError = EvaluateFit(vSamples, bGround, tSeed, tPhysics);
		if (flSeedError < flError)
			tBest = tSeed, flError = flSeedError;
	}

	for (float flStep : { 24.f, 12.f, 6.f, 3.f, 1.5f, 0.75f })
	{
		const float flScaleStep = flStep / 48.f;
		for (int iIteration = 0; iIteration < 4; iIteration++)
		{
			bool bImproved = false, bYawImproved = false, bScaleImproved = false;
			for (int iSign = -1; iSign <= 1; iSign += 2)
			{
				if (!bYawImproved)
				{
					MoveController_t tTry = tBest;
					tTry.m_flYaw = Math::NormalizeAngle(tTry.m_flYaw + flStep * iSign);
					if (const float flTry = EvaluateFit(vSamples, bGround, tTry, tPhysics); flTry < flError)
						tBest = tTry, flError = flTry, bImproved = true, bYawImproved = true;
				}

				if (!bScale || bScaleImproved)
					continue;

				MoveController_t tTry = tBest;
				tTry.m_flScale = std::clamp(tTry.m_flScale + flScaleStep * iSign, 0.f, 1.f);
				if (const float flTry = EvaluateFit(vSamples, bGround, tTry, tPhysics); flTry < flError)
					tBest = tTry, flError = flTry, bImproved = true, bScaleImproved = true;
			}
			if (!bImproved)
				break;
		}
	}
	return tBest;
}

static float CircularMean(const std::vector<std::pair<float, float>>& vYaws, float flFallback)
{
	float flX = 0.f, flY = 0.f;
	for (auto& [flYaw, flWeight] : vYaws)
	{
		const Vec3 vDirection = YawDirection(flYaw);
		flX += vDirection.x * flWeight, flY += vDirection.y * flWeight;
	}
	return flX || flY ? Math::Rad2Deg(atan2f(flY, flX)) : flFallback;
}

static inline float FitConfidence(float flError)
{
	return std::clamp(1.f - (sqrtf(flError) - 0.5f) / 6.f, 0.f, 1.f);
}

static bool AirVelocityBreak(const MoveData& tOlder, const MoveData& tNewer, const MovePhysics_t& tPhysics)
{
	const int iOldTicks = std::max(tOlder.m_iTicks, 1);
	const int iNewTicks = std::max(tNewer.m_iTicks, 1);
	const float flMidTicks = 0.5f * float(iOldTicks + iNewTicks);
	const float flQuantRate = (PLAYER_ORIGIN_COMPRESSION / TICK_INTERVAL) * (1.f / float(iOldTicks) + 1.f / float(iNewTicks));

	const float flMaxSpeed = std::max(tOlder.m_flMaxSpeed, tNewer.m_flMaxSpeed);
	const float flMaxAccelPerTick = std::max(tPhysics.m_flAirAccelerate * flMaxSpeed * TICK_INTERVAL, tPhysics.m_flAirCap);
	const float flMaxDv2D = flMaxAccelPerTick * (flMidTicks + 0.5f) + 1.415f * flQuantRate + 15.f;
	if ((tNewer.m_vVelocity - tOlder.m_vVelocity).Length2D() > flMaxDv2D)
		return true;

	const float flExpectedDz = -tPhysics.m_flGravity * flMidTicks * TICK_INTERVAL;
	if (fabsf((tNewer.m_vVelocity.z - tOlder.m_vVelocity.z) - flExpectedDz) > flQuantRate + 30.f)
		return true;

	return false;
}

static bool FitLine2D(const std::vector<FitSample_t>& vSamples, Vec3& vChordVel, float& flMaxDev, float& flRmsDev)
{
	const size_t n = vSamples.size();
	if (n < 2)
		return false;

	float flSumT = 0.f, flSumX = 0.f, flSumY = 0.f;
	float flT = 0.f;
	for (size_t i = 0; i < n; i++)
	{
		if (i > 0)
			flT += TICKS_TO_TIME(std::max(vSamples[i].m_iTicks, 1));
		flSumT += flT;
		flSumX += vSamples[i].m_vOrigin.x;
		flSumY += vSamples[i].m_vOrigin.y;
	}
	if (flT <= 0.f)
		return false;

	const float flInvN = 1.f / float(n);
	const float flMeanT = flSumT * flInvN;
	const float flMeanX = flSumX * flInvN;
	const float flMeanY = flSumY * flInvN;

	float flStt = 0.f, flStx = 0.f, flSty = 0.f;
	flT = 0.f;
	for (size_t i = 0; i < n; i++)
	{
		if (i > 0)
			flT += TICKS_TO_TIME(std::max(vSamples[i].m_iTicks, 1));
		const float flDt = flT - flMeanT;
		flStt += flDt * flDt;
		flStx += flDt * (vSamples[i].m_vOrigin.x - flMeanX);
		flSty += flDt * (vSamples[i].m_vOrigin.y - flMeanY);
	}
	if (flStt <= 1e-8f)
		return false;

	const float flVx = flStx / flStt;
	const float flVy = flSty / flStt;
	const float flX0 = flMeanX - flVx * flMeanT;
	const float flY0 = flMeanY - flVy * flMeanT;

	float flMaxSqr = 0.f, flSumSqr = 0.f;
	flT = 0.f;
	for (size_t i = 0; i < n; i++)
	{
		if (i > 0)
			flT += TICKS_TO_TIME(std::max(vSamples[i].m_iTicks, 1));
		const float flDx = vSamples[i].m_vOrigin.x - (flX0 + flVx * flT);
		const float flDy = vSamples[i].m_vOrigin.y - (flY0 + flVy * flT);
		const float flSqr = flDx * flDx + flDy * flDy;
		if (flSqr > flMaxSqr)
			flMaxSqr = flSqr;
		flSumSqr += flSqr;
	}

	vChordVel = { flVx, flVy, 0.f };
	flMaxDev = sqrtf(flMaxSqr);
	flRmsDev = sqrtf(flSumSqr * flInvN);
	return true;
}

static Vec3 EstimateVelocity(const std::deque<MoveData>& vRecords, bool bOnGround, bool bParachute, const MovePhysics_t& tPhysics)
{
	const auto& tNewest = vRecords.front();
	Vec3 vVelocity = tNewest.m_vVelocity;
	if (!bOnGround)
		vVelocity.z -= tPhysics.m_flGravity * TICK_INTERVAL * tNewest.m_iTicks * 0.5f;

	if (bOnGround && tNewest.m_iMode == MoveEnum::Ground)
	{
		int aBackTicks[8] = {};
		int iTotalTicks = 0;
		for (size_t k = 1; k < vRecords.size() && k < 8; k++)
		{
			if (vRecords[k].m_iMode != MoveEnum::Ground || vRecords[k - 1].m_bWall || vRecords[k - 1].m_iTicks > MaxRecordGap
				|| vRecords[k].m_bDucked != tNewest.m_bDucked || fabsf(vRecords[k].m_flMaxSpeed - tNewest.m_flMaxSpeed) > 1.f)
				break;

			iTotalTicks += std::max(vRecords[k - 1].m_iTicks, 1);
			if (iTotalTicks > 4)
				break;
			aBackTicks[k] = iTotalTicks;

			bool bCollinear = true;
			const Vec3 vSpan = vRecords[k].m_vOrigin - tNewest.m_vOrigin;
			for (size_t j = 1; j < k; j++)
			{
				const float flFrac = float(aBackTicks[j]) / float(iTotalTicks);
				const Vec3 vExpected = tNewest.m_vOrigin + vSpan * flFrac;
				if ((vRecords[j].m_vOrigin - vExpected).Length2DSqr() > 0.177f * 0.177f || fabsf(vRecords[j].m_vOrigin.z - vExpected.z) > 2.f)
				{
					bCollinear = false;
					break;
				}
			}
			if (!bCollinear)
				break;

			vVelocity = (tNewest.m_vOrigin - vRecords[k].m_vOrigin) / TICKS_TO_TIME(iTotalTicks);
			vVelocity.z = 0.f;
		}
		return vVelocity;
	}

	if (!bOnGround && tNewest.m_iMode == MoveEnum::Air)
	{
		int aBackTicks[8] = {};
		size_t iMaxK = 0;
		int iTotalTicks = 0;
		for (size_t k = 1; k < vRecords.size() && k < 8; k++)
		{
			if (vRecords[k].m_iMode != MoveEnum::Air || vRecords[k - 1].m_bWall || vRecords[k - 1].m_iTicks > MaxRecordGap
				|| vRecords[k].m_bDucked != tNewest.m_bDucked || fabsf(vRecords[k].m_flMaxSpeed - tNewest.m_flMaxSpeed) > 1.f)
				break;
			if (k >= 2 && AirVelocityBreak(vRecords[k - 1], vRecords[k - 2], tPhysics))
				break;

			iTotalTicks += std::max(vRecords[k - 1].m_iTicks, 1);
			if (iTotalTicks > 6)
				break;
			aBackTicks[k] = iTotalTicks;
			iMaxK = k;
		}

		for (size_t k = 2; k <= iMaxK; k++)
		{
			bool bCollinear = true;
			const Vec3 vSpan = vRecords[k].m_vOrigin - tNewest.m_vOrigin;
			for (size_t j = 1; j < k; j++)
			{
				const float flFrac = float(aBackTicks[j]) / float(aBackTicks[k]);
				const Vec3 vExpected = tNewest.m_vOrigin + vSpan * flFrac;
				if ((vRecords[j].m_vOrigin - vExpected).Length2DSqr() > 0.177f * 0.177f)
				{
					bCollinear = false;
					break;
				}
			}
			if (!bCollinear)
				break;

			const float flInvDt = 1.f / TICKS_TO_TIME(aBackTicks[k]);
			vVelocity.x = (tNewest.m_vOrigin.x - vRecords[k].m_vOrigin.x) * flInvDt;
			vVelocity.y = (tNewest.m_vOrigin.y - vRecords[k].m_vOrigin.y) * flInvDt;
		}

		if (!bParachute)
		{
			for (size_t k = 2; k <= iMaxK; k++)
			{
				const float flInvCount = 1.f / float(k + 1);
				float flSumT = 0.f, flSumH = 0.f;
				float aT[8], aH[8];
				for (size_t j = 0; j <= k; j++)
				{
					aT[j] = -TICKS_TO_TIME(aBackTicks[j]);
					aH[j] = vRecords[j].m_vOrigin.z + 0.5f * tPhysics.m_flGravity * aT[j] * aT[j];
					flSumT += aT[j];
					flSumH += aH[j];
				}
				const float flMeanT = flSumT * flInvCount;
				const float flMeanH = flSumH * flInvCount;
				float flStt = 0.f, flSth = 0.f;
				for (size_t j = 0; j <= k; j++)
				{
					const float flDt = aT[j] - flMeanT;
					flStt += flDt * flDt;
					flSth += flDt * (aH[j] - flMeanH);
				}
				if (flStt <= 1e-8f)
					break;

				const float flVz = flSth / flStt;
				const float flIntercept = flMeanH - flVz * flMeanT;
				bool bValid = true;
				for (size_t j = 0; j <= k; j++)
				{
					if (fabsf(aH[j] - (flIntercept + flVz * aT[j])) > 0.25f)
					{
						bValid = false;
						break;
					}
				}
				if (!bValid)
					break;

				vVelocity.z = flVz;
			}
		}
	}

	return vVelocity;
}

static bool GatherSegment(const std::deque<MoveData>& vRecords, int iMode, int iWindow, const MovePhysics_t& tPhysics, std::vector<FitSample_t>& vOut, bool& bCurrent)
{
	vOut.clear();

	size_t i = 0;
	while (i < vRecords.size() && vRecords[i].m_iMode != iMode)
		i++;
	bCurrent = !i;

	const float flNewest = !vRecords.empty() ? vRecords.front().m_flSimTime : 0.f;
	for (int iTicks = 0; i + 1 < vRecords.size() && iTicks < iWindow; i++)
	{
		auto& tRecord = vRecords[i];
		auto& tPrev = vRecords[i + 1];
		if (tRecord.m_iMode != iMode || tPrev.m_iMode != iMode || tRecord.m_bWall || tRecord.m_iTicks > MaxRecordGap
			|| tRecord.m_bDucked != tPrev.m_bDucked || fabsf(tRecord.m_flMaxSpeed - tPrev.m_flMaxSpeed) > 1.f)
			break;

		vOut.push_back({ tRecord.m_vOrigin, tRecord.m_vVelocity, tRecord.m_iTicks, TIME_TO_TICKS(flNewest - tRecord.m_flSimTime), tRecord.m_flEyeYaw, tRecord.m_flMaxSpeed, tRecord.m_bDucked });
		iTicks += tRecord.m_iTicks;

		if (iMode == MoveEnum::Air && AirVelocityBreak(tPrev, tRecord, tPhysics))
			break;
	}
	std::reverse(vOut.begin(), vOut.end());
	for (size_t i = 0; i < vOut.size(); i++)
	{
		vOut[i].m_flWeight = expf(-vOut[i].m_iAge / 16.f);
		vOut[i].m_vEyeDir = YawDirection(vOut[i].m_flEyeYaw);
		if (i >= 1)
			vOut[i].m_flEyeDelta = Math::NormalizeAngle(vOut[i].m_flEyeYaw - vOut[i - 1].m_flEyeYaw);
	}
	return vOut.size() >= 3;
}

static MoveController_t FitGround(const std::vector<FitSample_t>& vSamples, bool bCurrent, const MovePhysics_t& tPhysics)
{
	int iTotalTicks = 0;
	for (size_t i = 1; i < vSamples.size(); i++)
		iTotalTicks += vSamples[i].m_iTicks;

	const auto& tNewest = vSamples.back();
	Vec3 vChordVel = {};
	float flMaxDev = 0.f, flRmsDev = 0.f;
	if (iTotalTicks >= 5 && FitLine2D(vSamples, vChordVel, flMaxDev, flRmsDev) && flMaxDev <= 0.25f && flRmsDev <= 0.14f)
	{
		const float flChordSpeed = vChordVel.Length2D();
		if (flChordSpeed < 5.f)
		{
			if (tNewest.m_vVelocity.Length2D() < 10.f)
				return { MoveModelEnum::Stop, 0.f, 1.f, 0.f, 1.f };
		}
		else
		{
			const Vec3 vChordDir = vChordVel / flChordSpeed;
			const float flLastProj = tNewest.m_vVelocity.x * vChordDir.x + tNewest.m_vVelocity.y * vChordDir.y;
			bool bChanged = flLastProj < flChordSpeed - std::max(12.f, flChordSpeed * 0.05f);

			if (!bChanged && vSamples.size() >= 3)
			{
				int iRecentTicks = 0;
				size_t iRecent = vSamples.size() - 1;
				while (iRecent > 0 && iRecentTicks < 2)
					iRecentTicks += std::max(vSamples[iRecent--].m_iTicks, 1);
				if (iRecentTicks >= 2)
				{
					const Vec3 vRecentVel = (tNewest.m_vOrigin - vSamples[iRecent].m_vOrigin).To2D() / TICKS_TO_TIME(iRecentTicks);
					const float flRecentProj = vRecentVel.x * vChordDir.x + vRecentVel.y * vChordDir.y;
					const float flRecentOrtho = fabsf(vRecentVel.x * vChordDir.y - vRecentVel.y * vChordDir.x);
					if (flRecentProj < flChordSpeed - std::max(8.f, flChordSpeed * 0.04f) || flRecentOrtho > std::max(12.f, flChordSpeed * 0.08f))
						bChanged = true;
				}
			}

			if (!bChanged)
			{
				const float flCap = GroundCap(tNewest.m_flMaxSpeed, false, false, 0.f) * (tNewest.m_bDucked ? DuckSpeedCrop : 1.f);
				const float flScale = flCap > 0.f ? std::clamp(flChordSpeed / flCap, 0.f, 1.f) : 1.f;
				return { MoveModelEnum::World, Heading(vChordVel), flScale, 0.f, 1.f };
			}
		}
	}

	std::vector<std::pair<float, float>> vWorld, vView;
	for (size_t i = 1; i < vSamples.size(); i++)
	{
		auto& tFrom = vSamples[i - 1], &tTo = vSamples[i];
		if (tTo.m_iTicks != 1)
			continue;

		Vec3 vPrevious = tFrom.m_vVelocity; vPrevious.z = 0.f;
		GroundFriction(vPrevious, 1.f, tPhysics);
		const Vec3 vDelta = (tTo.m_vVelocity - vPrevious).To2D();
		if (vDelta.Length2D() < 4.f)
			continue;

		const float flWeight = tTo.m_flWeight, flYaw = Heading(vDelta);
		vWorld.emplace_back(flYaw, flWeight);
		vView.emplace_back(Math::NormalizeAngle(flYaw - tTo.m_flEyeYaw), flWeight);
	}

	const float flHeading = Heading(tNewest.m_vVelocity);

	float flStop = EvaluateFit(vSamples, true, { MoveModelEnum::Stop }, tPhysics);
	MoveController_t tBest = { MoveModelEnum::Stop };
	float flBest = flStop;

	float flView;
	MoveController_t tView = OptimizeFit(vSamples, true, {
		{ MoveModelEnum::View, CircularMean(vView, Math::NormalizeAngle(flHeading - tNewest.m_flEyeYaw)) },
		{ MoveModelEnum::View, Math::NormalizeAngle(flHeading - tNewest.m_flEyeYaw) },
		{ MoveModelEnum::View, 0.f }
	}, true, tPhysics, flView);
	if (flView + 0.05f < flBest * 0.95f)
		tBest = tView, flBest = flView;

	if (bCurrent)
	{
		float flWorld;
		MoveController_t tWorld = OptimizeFit(vSamples, true, {
			{ MoveModelEnum::World, CircularMean(vWorld, flHeading) },
			{ MoveModelEnum::World, flHeading }
		}, true, tPhysics, flWorld);
		if (flWorld * 1.1f + 0.05f < flBest)
			tBest = tWorld, flBest = flWorld;
	}

	FitChange(vSamples, true, tPhysics, tBest, flBest);
	tBest.m_flConfidence = FitConfidence(flBest);
	return tBest;
}

static MoveController_t FitAir(const std::vector<FitSample_t>& vSamples, bool bCurrent, const MovePhysics_t& tPhysics)
{
	int iTotalTicks = 0;
	for (size_t i = 1; i < vSamples.size(); i++)
		iTotalTicks += vSamples[i].m_iTicks;
	if (vSamples.size() < 4 || iTotalTicks < 5)
		return { MoveModelEnum::None, 0.f, 1.f, 0.f, 1.f };

	Vec3 vChordVel = {};
	float flMaxDev = 0.f, flRmsDev = 0.f;
	if (FitLine2D(vSamples, vChordVel, flMaxDev, flRmsDev) && flMaxDev <= 0.25f && flRmsDev <= 0.14f)
		return { MoveModelEnum::None, 0.f, 1.f, 0.f, 1.f };

	std::vector<std::pair<float, float>> vWorld, vView, vSweep;
	for (size_t i = 1; i < vSamples.size(); i++)
	{
		auto& tFrom = vSamples[i - 1], &tTo = vSamples[i];
		const Vec3 vDelta = (tTo.m_vVelocity - tFrom.m_vVelocity).To2D();
		if (vDelta.Length2D() < 6.f)
			continue;

		const float flWeight = tTo.m_flWeight, flYaw = Heading(vDelta);
		vWorld.emplace_back(flYaw, flWeight);
		vView.emplace_back(Math::NormalizeAngle(flYaw - tTo.m_flEyeYaw), flWeight);
		vSweep.emplace_back(Math::NormalizeAngle(flYaw - Heading(tFrom.m_vVelocity)), flWeight);
	}

	MoveController_t tBest = { MoveModelEnum::None };
	float flBest = EvaluateFit(vSamples, false, tBest, tPhysics);

	auto fConsider = [&](const MoveController_t& tController, float flError, float flBias)
		{
			if (flError < 4.f && flError * flBias + 0.08f < flBest * 0.75f)
				tBest = tController, flBest = flError;
		};

	float flError;
	auto tSweep = OptimizeFit(vSamples, false, {
		{ MoveModelEnum::Sweep, CircularMean(vSweep, 90.f) },
		{ MoveModelEnum::Sweep, 90.f },
		{ MoveModelEnum::Sweep, -90.f }
	}, false, tPhysics, flError);
	fConsider(tSweep, flError, 1.f);

	auto tView = OptimizeFit(vSamples, false, {
		{ MoveModelEnum::View, CircularMean(vView, 0.f) },
		{ MoveModelEnum::View, 90.f },
		{ MoveModelEnum::View, -90.f }
	}, false, tPhysics, flError);
	fConsider(tView, flError, 1.f);

	if (bCurrent)
	{
		auto tWorld = OptimizeFit(vSamples, false, {
			{ MoveModelEnum::World, CircularMean(vWorld, Heading(vSamples.back().m_vVelocity)) }
		}, false, tPhysics, flError);
		fConsider(tWorld, flError, 1.1f);
	}

	tBest.m_flConfidence = FitConfidence(flBest);
	return tBest;
}

static void FitHop(const std::deque<MoveData>& vRecords, float flGravity, MoveFit_t& tFit)
{
	int aStays[16] = {}, iStays = 0, iRun = 0, iRunStart = -1, iCurrent = -1;
	int iElapsed = 0, iLatestHopAge = -1;
	for (int i = 0; i < int(vRecords.size()) && iStays < 16; i++)
	{
		auto& tRecord = vRecords[i];
		if (tRecord.m_iMode == MoveEnum::Ground)
		{
			if (!iRun)
				iRunStart = i;
			iRun += std::max(tRecord.m_iTicks, 1);
			iElapsed += std::max(tRecord.m_iTicks, 1);
			continue;
		}

		if (iRun)
		{
			if (!iRunStart)
				iCurrent = iRun;
			else
			{
				auto& tGround = vRecords[iRunStart];
				auto& tDeparture = vRecords[iRunStart - 1];
				const int iGap = tDeparture.m_iTicks;
				if (tDeparture.m_iMode == MoveEnum::Air && iGap >= 1 && iGap <= 3 && !tDeparture.m_bDucked && !tGround.m_bDucked && !tDeparture.m_bWall)
				{
					const float flExpectedRise = TICKS_TO_TIME(iGap) * (JumpVelocity - flGravity * TICK_INTERVAL * float(iGap + 1) * 0.5f);
					if (fabsf((tDeparture.m_vOrigin.z - tGround.m_vOrigin.z) - flExpectedRise) <= 0.5f)
					{
						if (iLatestHopAge < 0)
							iLatestHopAge = iElapsed - iRun;
						aStays[iStays++] = iRun;
					}
				}
			}
		}
		iRun = 0;
		iElapsed += std::max(tRecord.m_iTicks, 1);
	}
	if (iRun && !iRunStart)
		iCurrent = iRun;

	if (iStays < 2 || iLatestHopAge < 0 || iLatestHopAge > 45)
		return;

	std::sort(aStays, aStays + iStays);
	const int iHop = aStays[iStays / 2];
	if (iHop > 12 || iCurrent >= 0 && iCurrent > std::max(iHop + 4, iHop * 2))
		return;

	tFit.m_iHopTicks = iHop;
	tFit.m_iGroundTicks = std::max(iCurrent, 0);
	tFit.m_iMaxHops = iStays >= 3 ? 3 : 2;
}

void CMovementSimulation::SetupPhysics(MoveStorage& tMoveStorage)
{
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	static auto sv_friction = H::ConVars.FindVar("sv_friction");
	static auto sv_stopspeed = H::ConVars.FindVar("sv_stopspeed");
	static auto sv_accelerate = H::ConVars.FindVar("sv_accelerate");
	static auto sv_airaccelerate = H::ConVars.FindVar("sv_airaccelerate");
	static auto sv_maxvelocity = H::ConVars.FindVar("sv_maxvelocity");
	static auto sv_stepsize = H::ConVars.FindVar("sv_stepsize");
	static auto tf_parachute_maxspeed_xy = H::ConVars.FindVar("tf_parachute_maxspeed_xy");
	static auto tf_parachute_maxspeed_z = H::ConVars.FindVar("tf_parachute_maxspeed_z");
	static auto tf_parachute_aircontrol = H::ConVars.FindVar("tf_parachute_aircontrol");
	static auto tf_movement_aircurrent_friction_mult = H::ConVars.FindVar("tf_movement_aircurrent_friction_mult");
	static auto tf_movement_aircurrent_aircontrol_mult = H::ConVars.FindVar("tf_movement_aircurrent_aircontrol_mult");
	static auto tf_movement_lost_footing_restick = H::ConVars.FindVar("tf_movement_lost_footing_restick");
	static auto tf_movement_lost_footing_friction = H::ConVars.FindVar("tf_movement_lost_footing_friction");
	static auto tf_clamp_back_speed = H::ConVars.FindVar("tf_clamp_back_speed");
	static auto tf_clamp_back_speed_min = H::ConVars.FindVar("tf_clamp_back_speed_min");
	static auto tf_max_charge_speed = H::ConVars.FindVar("tf_max_charge_speed");

	auto pPlayer = tMoveStorage.m_pPlayer;
	auto& tPhysics = tMoveStorage.m_tPhysics;
	auto fGet = [](ConVar* pVar, float flDefault) { return pVar ? pVar->GetFloat() : flDefault; };

	const float flEntityGravity = pPlayer->m_flGravity() ? pPlayer->m_flGravity() : 1.f;
	tPhysics.m_flGravity = fGet(sv_gravity, 800.f) * flEntityGravity;
	tPhysics.m_flFriction = fGet(sv_friction, 4.f);
	tPhysics.m_flStopSpeed = fGet(sv_stopspeed, 100.f);
	tPhysics.m_flAccelerate = fGet(sv_accelerate, 10.f);
	tPhysics.m_flAirAccelerate = fGet(sv_airaccelerate, 10.f);
	tPhysics.m_flMaxVelocity = fGet(sv_maxvelocity, 3500.f);
	tPhysics.m_flStepSize = fGet(sv_stepsize, 18.f);
	tPhysics.m_flParachuteXY = fGet(tf_parachute_maxspeed_xy, 300.f);
	tPhysics.m_flParachuteZ = fGet(tf_parachute_maxspeed_z, -100.f);
	tPhysics.m_flAirCurrentFriction = fGet(tf_movement_aircurrent_friction_mult, 1.f);
	tPhysics.m_flAirCurrentControl = fGet(tf_movement_aircurrent_aircontrol_mult, 1.f);
	tPhysics.m_flRestick = fGet(tf_movement_lost_footing_restick, 200.f);
	tPhysics.m_flLostFootingFriction = fGet(tf_movement_lost_footing_friction, 0.1f);
	tPhysics.m_flClampBack = fGet(tf_clamp_back_speed, 0.9f);
	tPhysics.m_flClampBackMin = fGet(tf_clamp_back_speed_min, 100.f);
	tPhysics.m_flChargeSpeed = fGet(tf_max_charge_speed, 750.f);

	float flCap = 30.f;
	if (pPlayer->InCond(TF_COND_PARACHUTE_ACTIVE))
		flCap *= fGet(tf_parachute_aircontrol, 1.f);
	if (pPlayer->InCond(TF_COND_ROCKETPACK))
		flCap *= 0.5f;
	float flModifier = SDK::AttribHookValue(1.f, "mod_air_control", pPlayer);
	if (pPlayer->InCond(TF_COND_BLASTJUMPING))
		flModifier = SDK::AttribHookValue(flModifier, "mod_air_control_blast_jump", pPlayer);
	tPhysics.m_flAirCap = flCap * flModifier;
}

void CMovementSimulation::StoreRecord(CTFPlayer* pPlayer, std::deque<MoveData>& vRecords, MoveData tRecord)
{
	if (!vRecords.empty())
	{
		auto& tLast = vRecords.front();
		const int iTicks = std::max(TIME_TO_TICKS(tRecord.m_flSimTime - tLast.m_flSimTime), 1);

		Vec3 vOrigin = tRecord.m_vOrigin;
		if (tRecord.m_iMode == MoveEnum::Air && tLast.m_iMode == MoveEnum::Air && tRecord.m_bDucked != tLast.m_bDucked)
			vOrigin.z += (tRecord.m_bDucked ? -20.f : 20.f) * pPlayer->m_flModelScale();

		static auto sv_maxvelocity = H::ConVars.FindVar("sv_maxvelocity");
		const float flLimit = (sv_maxvelocity ? sv_maxvelocity->GetFloat() : 3500.f) * 1.8f * TICKS_TO_TIME(iTicks) + 32.f;
		if ((vOrigin - tLast.m_vOrigin).Length() > flLimit)
			vRecords.clear();
		else
		{
			tRecord.m_iTicks = iTicks;
			tRecord.m_vVelocity = (vOrigin - tLast.m_vOrigin) / TICKS_TO_TIME(iTicks);

			if (tLast.m_vVelocity.Length2D() > 60.f)
			{
				CGameTrace trace = {};
				CTraceFilterWorldAndPropsOnly filter(pPlayer);
				const Vec3 vMins = pPlayer->m_vecMins() + PLAYER_ORIGIN_COMPRESSION;
				const Vec3 vMaxs = pPlayer->m_vecMaxs() - PLAYER_ORIGIN_COMPRESSION;
				SDK::TraceHull(tLast.m_vOrigin, tLast.m_vOrigin + tLast.m_vVelocity * TICKS_TO_TIME(iTicks), vMins, vMaxs, pPlayer->SolidMask(), &filter, &trace);
				tRecord.m_bWall = trace.fraction < 1.f && trace.plane.normal.z < 0.7f && tLast.m_vVelocity.Dot(trace.plane.normal) < -6.f;

				if (tRecord.m_bWall && tRecord.m_iMode == MoveEnum::Ground && tLast.m_iMode == MoveEnum::Ground)
				{
					static auto sv_stepsize = H::ConVars.FindVar("sv_stepsize");
					const float flStepSize = sv_stepsize ? sv_stepsize->GetFloat() : 18.f;
					const float flDz = vOrigin.z - tLast.m_vOrigin.z;
					const Vec3 vStepVel(tLast.m_vVelocity.x, tLast.m_vVelocity.y, 0.f);
					if (flDz > 0.5f && flDz <= flStepSize * float(iTicks) + 1.f)
					{
						const float flStep = std::min(flDz + 0.5f, flStepSize * float(iTicks) + 1.f);
						CGameTrace stepTrace = {};
						SDK::TraceHull(tLast.m_vOrigin, tLast.m_vOrigin + Vec3(0.f, 0.f, flStep), vMins, vMaxs, pPlayer->SolidMask(), &filter, &stepTrace);
						if (!stepTrace.startsolid && !stepTrace.allsolid)
						{
							SDK::TraceHull(stepTrace.endpos, stepTrace.endpos + vStepVel * TICKS_TO_TIME(iTicks), vMins, vMaxs, pPlayer->SolidMask(), &filter, &stepTrace);
							tRecord.m_bWall = stepTrace.fraction < 1.f && stepTrace.plane.normal.z < 0.7f && vStepVel.Dot(stepTrace.plane.normal) < -6.f;
						}
					}
					else if (tLast.m_vVelocity.z < -60.f && flDz <= 0.5f)
					{
						CGameTrace stepTrace = {};
						SDK::TraceHull(tLast.m_vOrigin, tLast.m_vOrigin + vStepVel * TICKS_TO_TIME(iTicks), vMins, vMaxs, pPlayer->SolidMask(), &filter, &stepTrace);
						tRecord.m_bWall = stepTrace.fraction < 1.f && stepTrace.plane.normal.z < 0.7f && vStepVel.Dot(stepTrace.plane.normal) < -6.f;
					}
				}
			}
		}
	}

	vRecords.push_front(tRecord);
	if (vRecords.size() > MaxRecords)
		vRecords.pop_back();
}

void CMovementSimulation::Store()
{
	if (I::EngineClient->IsPlayingDemo())
	{
		m_vCache.clear();
		return;
	}

	const int iLocalIdx = I::EngineClient->GetLocalPlayer();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		const int iIndex = pPlayer->entindex();
		if (iIndex == iLocalIdx)
			continue;

		auto& vRecords = m_mRecords[iIndex];
		if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsDormant())
		{
			vRecords.clear();
			m_mFits.erase(iIndex);
			F::NeuralPred.ResetPlayer(iIndex);
			std::erase_if(m_vCache, [iIndex](const MoveCacheEntry_t& tEntry) { return tEntry.m_iEntIndex == iIndex; });
			continue;
		}

		const float flSimTime = pPlayer->m_flSimulationTime();
		if (!vRecords.empty() && TIME_TO_TICKS(flSimTime) <= TIME_TO_TICKS(vRecords.front().m_flSimTime))
			continue;

		std::erase_if(m_vCache, [iIndex, flSimTime](const MoveCacheEntry_t& tEntry) { return tEntry.m_iEntIndex == iIndex && tEntry.m_flSimTime != flSimTime; });

		MoveData tRecord = {};
		tRecord.m_flSimTime = flSimTime;
		tRecord.m_iMode = GetMoveMode(pPlayer);
		tRecord.m_vOrigin = pPlayer->m_vecOrigin();
		tRecord.m_vVelocity = pPlayer->m_vecVelocity();
		tRecord.m_vDirection = tRecord.m_vVelocity.To2D();
		tRecord.m_flEyeYaw = pPlayer->m_angEyeAngles().y;
		tRecord.m_flMaxSpeed = SDK::MaxSpeed(pPlayer);
		tRecord.m_bDucked = pPlayer->IsDucking();
		StoreRecord(pPlayer, vRecords, tRecord);

		const auto& tNewest = vRecords.front();
		NeuralBaseline_t tBaseline = {};
		if (auto itFit = m_mFits.find(iIndex); itFit != m_mFits.end())
		{
			const auto& tFit = itFit->second;
			tBaseline.m_iGroundModel = tFit.m_tGround.m_iModel;
			tBaseline.m_flGroundYaw = tFit.m_tGround.m_flYaw;
			tBaseline.m_flGroundScale = tFit.m_tGround.m_flScale;
			tBaseline.m_flGroundTurn = tFit.m_tGround.m_flTurn;
			tBaseline.m_iAirModel = tFit.m_tAir.m_iModel;
			tBaseline.m_flAirYaw = tFit.m_tAir.m_flYaw;
			tBaseline.m_flAirScale = tFit.m_tAir.m_flScale;
			tBaseline.m_flAirTurn = tFit.m_tAir.m_flTurn;
			tBaseline.m_iHopTicks = tFit.m_iHopTicks;
			tBaseline.m_iGroundTicks = tFit.m_iGroundTicks;
		}
		else if (tNewest.m_iMode == MoveEnum::Ground && tNewest.m_vVelocity.Length2D() > 1.f)
		{
			tBaseline.m_iGroundModel = MoveModelEnum::World;
			tBaseline.m_flGroundYaw = Heading(tNewest.m_vVelocity);
			tBaseline.m_flGroundScale = 1.f;
		}
		F::NeuralPred.OnPlayerUpdate(pPlayer, tNewest.m_vOrigin, tNewest.m_vVelocity, flSimTime, tNewest.m_flEyeYaw, tNewest.m_flMaxSpeed, tNewest.m_bDucked, tBaseline);
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

		const float flDeltaTime = H::Entities.GetDeltaTime(pPlayer->entindex());
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
	if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsDormant())
	{
		vRecords.clear();
		return;
	}

	MoveData tRecord = {};
	tRecord.m_flSimTime = flTime;
	tRecord.m_iMode = GetMoveMode(pPlayer);
	tRecord.m_vOrigin = tMoveData.m_vecAbsOrigin;
	tRecord.m_vVelocity = tMoveData.m_vecVelocity;
	tRecord.m_vDirection = Math::RotatePoint({ tMoveData.m_flForwardMove, -tMoveData.m_flSideMove, tMoveData.m_flUpMove }, {}, { 0, tMoveData.m_vecViewAngles.y, 0 });
	tRecord.m_bInputDirection = true;
	tRecord.m_flEyeYaw = tMoveData.m_vecViewAngles.y;
	tRecord.m_flMaxSpeed = tMoveData.m_flMaxSpeed;
	tRecord.m_bDucked = pPlayer->IsDucking();

	if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		tRecord.m_vDirection = YawDirection(tMoveData.m_vecViewAngles.y) * MovementMaxSpeed, tRecord.m_bInputDirection = false;
	else if (tRecord.m_iMode == MoveEnum::Ground && (Vars::Misc::Movement::AutoStrafe.Value || Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
		|| tRecord.m_iMode == MoveEnum::Air && (Vars::Misc::Movement::AutoStrafe.Value || tRecord.m_vDirection.To2D().IsZero()))
		tRecord.m_vDirection = tRecord.m_vVelocity.Normalized2D() * MovementMaxSpeed, tRecord.m_bInputDirection = false;

	vRecords.push_front(tRecord);
	if (vRecords.size() > MaxRecords)
		vRecords.pop_back();
}

const MoveFit_t& CMovementSimulation::GetFit(CTFPlayer* pPlayer, bool bStrafe)
{
	if (!bStrafe)
	{
		static const MoveFit_t tEmpty = {};
		return tEmpty;
	}

	auto& tFit = m_mFits[pPlayer->entindex()];
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.empty())
	{
		tFit = {};
		return tFit;
	}
	if (tFit.m_flSimTime == vRecords.front().m_flSimTime)
		return tFit;

	PROJ_ZONE(MoveStrafe);

	tFit = {};
	tFit.m_flSimTime = vRecords.front().m_flSimTime;

	MoveStorage tPhysics = {};
	tPhysics.m_pPlayer = pPlayer;
	SetupPhysics(tPhysics);

	std::vector<FitSample_t> vSamples;
	vSamples.reserve(MaxRecords);
	if (GatherSegment(vRecords, MoveEnum::Ground, Vars::Aimbot::Projectile::GroundSamples.Value, tPhysics.m_tPhysics, vSamples, tFit.m_bGroundCurrent))
		tFit.m_tGround = FitGround(vSamples, tFit.m_bGroundCurrent, tPhysics.m_tPhysics);
	if (GatherSegment(vRecords, MoveEnum::Air, Vars::Aimbot::Projectile::AirSamples.Value, tPhysics.m_tPhysics, vSamples, tFit.m_bAirCurrent))
		tFit.m_tAir = FitAir(vSamples, tFit.m_bAirCurrent, tPhysics.m_tPhysics);
	FitHop(vRecords, tPhysics.m_tPhysics.m_flGravity, tFit);

	if (Vars::Debug::Logging.Value)
		SDK::Output("MovementSimulation", std::format("{}: ground {} ({:.2f}, {:.0f}%), air {} ({:.2f}, {:.0f}%), hop {}",
			pPlayer->entindex(), tFit.m_tGround.m_iModel, tFit.m_tGround.m_flYaw, tFit.m_tGround.m_flConfidence * 100,
			tFit.m_tAir.m_iModel, tFit.m_tAir.m_flYaw, tFit.m_tAir.m_flConfidence * 100, tFit.m_iHopTicks).c_str(), { 100, 255, 150 }, Vars::Debug::Logging.Value);

	return tFit;
}

void CMovementSimulation::SetupMoveData(MoveStorage& tMoveStorage, const Vec3* pvOrigin)
{
	auto pPlayer = tMoveStorage.m_pPlayer;
	auto& tMoveData = tMoveStorage.m_MoveData;
	auto& tPhysics = tMoveStorage.m_tPhysics;
	const int iIndex = pPlayer->entindex();
	const bool bLocal = tMoveStorage.m_bLocal;
	const auto& vRecords = m_mRecords[iIndex];

	tMoveData = {};
	tMoveData.m_vecAbsOrigin = pvOrigin ? *pvOrigin : pPlayer->m_vecOrigin();
	tMoveData.m_flMaxSpeed = tMoveData.m_flClientMaxSpeed = SDK::MaxSpeed(pPlayer);

	tMoveStorage.m_bOnGround = pPlayer->IsOnGround();
	tMoveStorage.m_bDucked = pPlayer->IsDucking();
	tMoveStorage.m_nMask = pPlayer->SolidMask();
	tMoveStorage.m_iTeam = pPlayer->m_iTeamNum();
	tMoveStorage.m_vMins = pPlayer->m_vecMins(), tMoveStorage.m_vMaxs = pPlayer->m_vecMaxs();
	tMoveStorage.m_vViewOffset = bLocal ? pPlayer->m_vecViewOffset() : pPlayer->GetViewOffset();
	if (!bLocal)
		tMoveStorage.m_vMins += PLAYER_ORIGIN_COMPRESSION, tMoveStorage.m_vMaxs -= PLAYER_ORIGIN_COMPRESSION;

	tMoveStorage.m_iWaterLevel = CheckWater(tMoveStorage);
	tMoveStorage.m_bLostFooting = pPlayer->InCond(TF_COND_LOST_FOOTING);
	tMoveStorage.m_bAirCurrent = pPlayer->InCond(TF_COND_AIR_CURRENT);
	tMoveStorage.m_bParachute = pPlayer->InCond(TF_COND_PARACHUTE_ACTIVE);

	if (bLocal)
	{
		tMoveData.m_vecVelocity = pPlayer->m_vecVelocity();
		tMoveData.m_vecViewAngles = G::CurrentUserCmd ? G::CurrentUserCmd->viewangles : pPlayer->GetEyeAngles();
	}
	else if (pPlayer->IsDormant())
	{
		tMoveData.m_vecVelocity = {};
		tMoveData.m_vecViewAngles = { 0.f, pPlayer->m_angEyeAngles().y, 0.f };
	}
	else
	{
		const bool bRecord = !vRecords.empty() && vRecords.front().m_flSimTime == pPlayer->m_flSimulationTime() && vRecords.size() > 1 && vRecords.front().m_iTicks <= MaxRecordGap;
		const auto pOrigins = H::Entities.GetOrigins(iIndex);
		if (bRecord)
			tMoveData.m_vecVelocity = EstimateVelocity(vRecords, tMoveStorage.m_bOnGround, tMoveStorage.m_bParachute, tPhysics);
		else if (pOrigins && pOrigins->size() >= 2 && H::Entities.GetAvgVelocity(iIndex))
			tMoveData.m_vecVelocity = *H::Entities.GetAvgVelocity(iIndex);
		else
			tMoveData.m_vecVelocity = pPlayer->m_vecVelocity();
		tMoveData.m_vecViewAngles = { 0.f, pPlayer->m_angEyeAngles().y, 0.f };
	}
	if (tMoveStorage.m_bOnGround)
		tMoveData.m_vecVelocity.z = 0.f;
	tMoveStorage.m_flViewYaw = tMoveData.m_vecViewAngles.y;

	tMoveStorage.m_bCharging = (bLocal || !pPlayer->IsDormant()) && pPlayer->InCond(TF_COND_SHIELD_CHARGE) && tMoveStorage.m_iWaterLevel < 2;
	if (tMoveStorage.m_bCharging)
	{
		static auto tf_demoman_charge_drain_time = H::ConVars.FindVar("tf_demoman_charge_drain_time");
		const float flDuration = SDK::AttribHookValue(tf_demoman_charge_drain_time ? tf_demoman_charge_drain_time->GetFloat() : 1.5f, "mod_charge_time", pPlayer);
		tMoveStorage.m_flChargeMeter = std::max(pPlayer->m_flChargeMeter(), 0.f);
		tMoveStorage.m_flChargeDrain = flDuration > 0.f ? 100.f / flDuration * TICK_INTERVAL : 100.f;
		tMoveStorage.m_flChargeExitSpeed = pPlayer->CalculateMaxSpeed(true);
		tMoveStorage.m_flChargeExitCap = tPhysics.m_flAirCap;
		tPhysics.m_flAirCap = tPhysics.m_flChargeSpeed;
	}

	tMoveStorage.m_flPredictedDelta = GetPredictedDelta(pPlayer);
	tMoveStorage.m_flSimTime = pPlayer->m_flSimulationTime();
	tMoveStorage.m_flPredictedSimTime = tMoveStorage.m_flSimTime + tMoveStorage.m_flPredictedDelta;
	tMoveStorage.m_vPredictedOrigin = tMoveData.m_vecAbsOrigin;
}

static bool IsGroundDecelerating(const std::deque<MoveData>& vRecords, float flSpeed, float flCap)
{
	if (vRecords.size() < 2 || flSpeed >= flCap * 0.94f)
		return false;

	const auto& tNewest = vRecords.front();
	int iSpanTicks = 0;
	for (size_t k = 1; k < vRecords.size() && k < 4; k++)
	{
		const auto& tPrev = vRecords[k];
		if (vRecords[k - 1].m_bWall || vRecords[k - 1].m_iTicks > MaxRecordGap
			|| tPrev.m_bDucked != tNewest.m_bDucked || fabsf(tPrev.m_flMaxSpeed - tNewest.m_flMaxSpeed) > 1.f)
			break;

		iSpanTicks += std::max(vRecords[k - 1].m_iTicks, 1);
		const float flPrevSpeed = std::min(tPrev.m_vVelocity.Length2D(), flCap);
		if (flPrevSpeed - flSpeed > std::max(8.f, 4.f * float(iSpanTicks)))
			return true;

		if (tPrev.m_iMode != MoveEnum::Ground)
			break;
	}
	return false;
}

void CMovementSimulation::SetupController(MoveStorage& tMoveStorage, bool bStrafe)
{
	auto pPlayer = tMoveStorage.m_pPlayer;
	auto& tMoveData = tMoveStorage.m_MoveData;
	const auto& vRecords = m_mRecords[pPlayer->entindex()];

	if (tMoveStorage.m_bCharging)
	{
		tMoveStorage.m_tGround = tMoveStorage.m_tAir = { MoveModelEnum::Charge };
		if (!tMoveStorage.m_bLocal)
			return;
	}

	if (tMoveStorage.m_bLocal)
	{
		if (!tMoveStorage.m_bCharging)
			tMoveStorage.m_tGround = tMoveStorage.m_tAir = { MoveModelEnum::Input };

		if (!vRecords.empty() && !vRecords.front().m_vDirection.IsZero())
		{
			auto& tRecord = vRecords.front();
			G::DummyCmd.forwardmove = tRecord.m_vDirection.x;
			G::DummyCmd.sidemove = -tRecord.m_vDirection.y;
			G::DummyCmd.upmove = tRecord.m_vDirection.z;
			SDK::FixMovement(&G::DummyCmd, {}, tMoveData.m_vecViewAngles);
			tMoveData.m_flForwardMove = G::DummyCmd.forwardmove;
			tMoveData.m_flSideMove = G::DummyCmd.sidemove;
			tMoveData.m_flUpMove = G::DummyCmd.upmove;
		}

		if (Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
			tMoveStorage.m_bBunnyHop = true, tMoveStorage.m_iHopTicks = 0;
		return;
	}

	const bool bGround = bStrafe && Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::Ground;
	const bool bAir = bStrafe && Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::Air;
	static const MoveFit_t tEmpty = {};
	const auto& tFit = (bGround || bAir) ? GetFit(pPlayer, bStrafe) : tEmpty;

	const Vec3& vVelocity = tMoveData.m_vecVelocity;
	const float flCap = GroundCap(tMoveData.m_flMaxSpeed, tMoveStorage.m_bLostFooting, false, 0.f) * (tMoveStorage.m_bDucked ? DuckSpeedCrop : 1.f);
	if (bGround && tFit.m_bGroundCurrent && tFit.m_tGround.m_iModel != MoveModelEnum::None)
		tMoveStorage.m_tGround = tFit.m_tGround;
	else if (!tMoveStorage.m_bOnGround)
		tMoveStorage.m_tGround = { MoveModelEnum::None };
	else if (vVelocity.Length2D() > 1.f && !IsGroundDecelerating(vRecords, vVelocity.Length2D(), flCap))
		tMoveStorage.m_tGround = { MoveModelEnum::World, Heading(vVelocity), flCap > 0.f ? std::min(vVelocity.Length2D() / flCap, 1.f) : 1.f };
	else
		tMoveStorage.m_tGround = { MoveModelEnum::Stop };

	tMoveStorage.m_tAir = (bAir && (tFit.m_bAirCurrent || tFit.m_iHopTicks >= 0)) ? tFit.m_tAir : MoveController_t{ MoveModelEnum::None };

	if (bGround || bAir)
		tMoveStorage.m_iHopTicks = tFit.m_iHopTicks, tMoveStorage.m_iGroundTicks = tFit.m_iGroundTicks, tMoveStorage.m_iMaxHops = tFit.m_iMaxHops;

	if (tMoveStorage.m_tGround.m_iModel == MoveModelEnum::View && vRecords.size() > 1)
	{
		float flFallbackTurn = 0.f;
		bool bFound = false;
		for (size_t i = 1; i < vRecords.size(); i++)
		{
			const int iSpan = TIME_TO_TICKS(vRecords.front().m_flSimTime - vRecords[i].m_flSimTime);
			if (iSpan < 2)
				continue;

			const float flTurn = Math::NormalizeAngle(vRecords.front().m_flEyeYaw - vRecords[i].m_flEyeYaw) / float(iSpan);
			if (!bFound)
				flFallbackTurn = flTurn, bFound = true;
			if (iSpan >= 4)
			{
				flFallbackTurn = flTurn;
				break;
			}
		}
		if (bFound && fabsf(flFallbackTurn) >= 0.35f)
			tMoveStorage.m_tGround.m_flTurn = std::clamp(flFallbackTurn, -EyeTurnMax, EyeTurnMax) * EyeTurnKept;
	}

	if (pPlayer->InCond(TF_COND_STUNNED) && (pPlayer->m_iStunFlags() & TF_STUN_CONTROLS || pPlayer->m_iStunFlags() & TF_STUN_MOVEMENT && pPlayer->m_iMovementStunAmount() == 255))
	{
		tMoveStorage.m_tGround = { MoveModelEnum::Stop }, tMoveStorage.m_tAir = { MoveModelEnum::None };
		tMoveStorage.m_bNeuralGround = false;
		tMoveStorage.m_bNeuralAir = false;
		return;
	}

	tMoveStorage.m_bNeuralGround = false;
	tMoveStorage.m_bNeuralAir = false;
	tMoveStorage.m_iNeuralStep = 0;
	tMoveStorage.m_iNeuralStartTick = 0;
	tMoveStorage.m_flNeuralBaseYaw = tMoveStorage.m_flViewYaw;
	tMoveStorage.m_tNeural = {};

	const int iNeuralMode = Vars::Aimbot::Projectile::NeuralPrediction.Value;
	if ((bGround || bAir) && iNeuralMode != Vars::Aimbot::Projectile::NeuralPredictionEnum::Off)
	{
		uint32_t uSteamId32 = 0;
		player_info_t tPlayerInfo = {};
		if (I::EngineClient && I::EngineClient->GetPlayerInfo(pPlayer->entindex(), &tPlayerInfo))
			uSteamId32 = tPlayerInfo.friendsID;

		if (F::NeuralPred.GetPrediction(pPlayer->entindex(), uSteamId32, tMoveStorage.m_tNeural))
		{
			const int iAgeTicks = TIME_TO_TICKS(pPlayer->m_flSimulationTime() - tMoveStorage.m_tNeural.m_flSimTime);
			const float flMidSigma = tMoveStorage.m_tNeural.m_aKnots[6].m_flSigmaXY;
			const float flMaxUnc = std::max(Vars::Aimbot::Projectile::NeuralMaxUncertainty.Value, 1.f);
			bool bPassGate = false;
			if (iAgeTicks >= -1 && iAgeTicks <= 4)
			{
				if (iNeuralMode == Vars::Aimbot::Projectile::NeuralPredictionEnum::NeuralOnly)
					bPassGate = true;
				else if (iNeuralMode == Vars::Aimbot::Projectile::NeuralPredictionEnum::PreferNeural)
					bPassGate = !F::NeuralPred.ShouldFallback() && flMidSigma <= flMaxUnc * 1.5f;
				else
					bPassGate = !F::NeuralPred.ShouldFallback() && flMidSigma <= flMaxUnc;
			}

			if (bPassGate)
			{
				tMoveStorage.m_iNeuralStep = std::max(0, iAgeTicks);
				const float flConf = std::clamp(1.f - (flMidSigma / flMaxUnc) * 0.5f, 0.1f, 1.f);
				if (bGround)
				{
					tMoveStorage.m_bNeuralGround = true;
					tMoveStorage.m_tGround = { MoveModelEnum::Neural, 0.f, 1.f, 0.f, flConf };
				}
				if (bAir)
				{
					tMoveStorage.m_bNeuralAir = true;
					tMoveStorage.m_tAir = { MoveModelEnum::Neural, 0.f, 1.f, 0.f, flConf };
				}
			}
		}
	}
}

bool CMovementSimulation::Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance, bool bStrafe, bool bPredict, const Vec3* pvOrigin)
{
	PROJ_ZONE(MoveInit);

	if (I::GlobalVars->tickcount != m_iCacheTick)
		m_iCacheTick = I::GlobalVars->tickcount;

	tMoveStorage = {};
	if (!pEntity || !pEntity->IsPlayer() || !pEntity->As<CTFPlayer>()->IsAlive())
	{
		tMoveStorage.m_bInitFailed = tMoveStorage.m_bFailed = true;
		return false;
	}

	auto pPlayer = pEntity->As<CTFPlayer>();
	tMoveStorage.m_pPlayer = pPlayer;
	tMoveStorage.m_bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();
	if (!tMoveStorage.m_bLocal && pPlayer->IsDormant())
		bStrafe = false;

	const int iEntIndex = pPlayer->entindex();
	const float flSimTime = pPlayer->m_flSimulationTime();
	const Vec3 vStartOrigin = pvOrigin ? *pvOrigin : pPlayer->m_vecOrigin();
	const int iStrafeFlags = Vars::Aimbot::Projectile::StrafePrediction.Value;
	const int iChoke = bPredict ? H::Entities.GetChoke(iEntIndex) : 0;

	if (!tMoveStorage.m_bLocal)
	{
		for (int iSlot = 0; iSlot < int(m_vCache.size()); iSlot++)
		{
			const auto& tEntry = m_vCache[iSlot];
			if (tEntry.m_iEntIndex != iEntIndex || tEntry.m_flSimTime != flSimTime || tEntry.m_vStartOrigin != vStartOrigin
				|| tEntry.m_bStrafe != bStrafe || tEntry.m_bPredict != bPredict || tEntry.m_iStrafeFlags != iStrafeFlags || tEntry.m_iChoke != iChoke)
				continue;

			if (tEntry.m_bStuckFailed)
			{
				tMoveStorage = tEntry.m_tInitState;
				tMoveStorage.m_bCached = true;
				tMoveStorage.m_iCacheSlot = -1;
				tMoveStorage.m_iCacheStep = 0;
				return true;
			}

			if (bHitchance && bStrafe && Vars::Aimbot::Projectile::HitChance.Value
				&& tEntry.m_bFitted && tEntry.m_flConfidence * 100.f < Vars::Aimbot::Projectile::HitChance.Value)
			{
				if (Vars::Debug::Logging.Value)
					SDK::Output("MovementSimulation", std::format("Hitchance ({}% < {}%)", tEntry.m_flConfidence * 100, Vars::Aimbot::Projectile::HitChance.Value).c_str(), { 80, 200, 120 }, Vars::Debug::Logging.Value);
				tMoveStorage.m_bFailed = true;
				return false;
			}

			tMoveStorage = tEntry.m_tInitState;
			tMoveStorage.m_bCached = true;
			tMoveStorage.m_iCacheSlot = iSlot;
			tMoveStorage.m_iCacheStep = 0;
			tMoveStorage.m_vPath.reserve(256);
			return true;
		}
	}

	SetupPhysics(tMoveStorage);
	SetupMoveData(tMoveStorage, pvOrigin);
	if (CheckStuck(tMoveStorage))
	{
		tMoveStorage.m_bFailed = true;
		if (!tMoveStorage.m_bLocal)
		{
			MoveCacheEntry_t tEntry = {};
			tEntry.m_iEntIndex = iEntIndex;
			tEntry.m_flSimTime = flSimTime;
			tEntry.m_vStartOrigin = vStartOrigin;
			tEntry.m_bStrafe = bStrafe;
			tEntry.m_bPredict = bPredict;
			tEntry.m_iStrafeFlags = iStrafeFlags;
			tEntry.m_iChoke = iChoke;
			tEntry.m_bStuckFailed = true;
			tEntry.m_tInitState = tMoveStorage;
			m_vCache.push_back(std::move(tEntry));
		}
		return true;
	}

	SetupController(tMoveStorage, bStrafe);

	bool bFitted = false;
	float flConfidence = 0.f;
	if (!tMoveStorage.m_bLocal && bStrafe)
	{
		const auto& tController = tMoveStorage.m_bOnGround ? tMoveStorage.m_tGround : tMoveStorage.m_tAir;
		if (tController.m_iModel != MoveModelEnum::Neural)
		{
			if (auto it = m_mFits.find(iEntIndex); it != m_mFits.end())
			{
				const auto& tFit = it->second;
				bFitted = tMoveStorage.m_bOnGround ? tFit.m_tGround.m_iModel != MoveModelEnum::None && tFit.m_bGroundCurrent : tFit.m_tAir.m_iModel != MoveModelEnum::None && tFit.m_bAirCurrent;
			}
		}
		flConfidence = tController.m_flConfidence;
	}

	tMoveStorage.m_vPath = { tMoveStorage.m_MoveData.m_vecAbsOrigin };
	tMoveStorage.m_vPath.reserve(256);
	tMoveStorage.m_iCacheSlot = -1;
	tMoveStorage.m_iCacheStep = 0;
	tMoveStorage.m_bCached = false;
	if (bPredict)
	{
		for (int i = 0; i < H::Entities.GetChoke(iEntIndex); i++)
			RunTick(tMoveStorage);
	}

	if (!tMoveStorage.m_bLocal)
	{
		const int iSlot = int(m_vCache.size());
		MoveCacheEntry_t tEntry = {};
		tEntry.m_iEntIndex = iEntIndex;
		tEntry.m_flSimTime = flSimTime;
		tEntry.m_vStartOrigin = vStartOrigin;
		tEntry.m_bStrafe = bStrafe;
		tEntry.m_bPredict = bPredict;
		tEntry.m_iStrafeFlags = iStrafeFlags;
		tEntry.m_iChoke = iChoke;
		tEntry.m_bStuckFailed = false;
		tEntry.m_bFitted = bFitted;
		tEntry.m_flConfidence = flConfidence;
		tEntry.m_tInitState = tMoveStorage;
		tEntry.m_tFrontier = tMoveStorage;
		tEntry.m_tFrontier.m_iCacheSlot = -1;
		tEntry.m_tFrontier.m_vPath.clear();
		tEntry.m_vSteps.reserve(256);
		if (m_vCache.size() >= 128)
			m_vCache.erase(m_vCache.begin());
		m_vCache.push_back(std::move(tEntry));
		tMoveStorage.m_iCacheSlot = int(m_vCache.size()) - 1;
		tMoveStorage.m_iCacheStep = 0;
		tMoveStorage.m_bCached = false;
	}

	if (bHitchance && bStrafe && Vars::Aimbot::Projectile::HitChance.Value && !tMoveStorage.m_bLocal
		&& bFitted && flConfidence * 100.f < Vars::Aimbot::Projectile::HitChance.Value)
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("MovementSimulation", std::format("Hitchance ({}% < {}%)", flConfidence * 100, Vars::Aimbot::Projectile::HitChance.Value).c_str(), { 80, 200, 120 }, Vars::Debug::Logging.Value);
		tMoveStorage.m_bFailed = true;
		return false;
	}

	return true;
}

void CMovementSimulation::StepTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback* pCallback)
{
	PROJ_ZONE(MoveTick);
	PROJ_COUNT(MoveTicks);

	if (tMoveStorage.m_bFailed || !tMoveStorage.m_pPlayer)
		return;

	CartPoseGuard_t tCartGuard;
	if (F::CartSim.HasMovingCarts())
		F::CartSim.PoseCartsNear(TICKS_TO_TIME(tMoveStorage.m_iSimTicks + 1), tMoveStorage.m_MoveData.m_vecAbsOrigin, 160.f);

	auto& tMoveData = tMoveStorage.m_MoveData;
	auto& tPhysics = tMoveStorage.m_tPhysics;
	Vec3& vVelocity = tMoveData.m_vecVelocity;
	const float flHalfGravity = tPhysics.m_flGravity * TICK_INTERVAL * 0.5f;

	if (tMoveStorage.m_bCharging && (tMoveStorage.m_flChargeMeter -= tMoveStorage.m_flChargeDrain) <= 0.f)
	{
		tMoveStorage.m_bCharging = false;
		tMoveData.m_flMaxSpeed = tMoveData.m_flClientMaxSpeed = tMoveStorage.m_flChargeExitSpeed;
		tPhysics.m_flAirCap = tMoveStorage.m_flChargeExitCap;
		if (tMoveStorage.m_tGround.m_iModel == MoveModelEnum::Charge)
			tMoveStorage.m_tGround = { MoveModelEnum::View, 0.f, 1.f };
		if (tMoveStorage.m_tAir.m_iModel == MoveModelEnum::Charge)
			tMoveStorage.m_tAir = { MoveModelEnum::None };
	}

	const bool bNeuralStep = !tMoveStorage.m_bLocal && (tMoveStorage.m_bOnGround ? tMoveStorage.m_bNeuralGround : tMoveStorage.m_bNeuralAir) && tMoveStorage.m_tNeural.m_bValid;
	const NeuralKnot_t tNeuralKnot = bNeuralStep ? tMoveStorage.m_tNeural.SampleAtTick(tMoveStorage.m_iNeuralStep + 1) : NeuralKnot_t{};

	if (tMoveStorage.m_bOnGround && tMoveStorage.m_iWaterLevel <= 1 && !tMoveStorage.m_bDucked)
	{
		bool bDoJump = false;
		if (tMoveStorage.m_iHopTicks >= 0)
		{
			if (tMoveStorage.m_iGroundTicks >= tMoveStorage.m_iHopTicks)
				bDoJump = true;
			else
				tMoveStorage.m_iGroundTicks++;
		}
		else if (bNeuralStep && tNeuralKnot.m_flJumpProb >= 0.55f)
		{
			bDoJump = true;
		}
		if (bDoJump)
		{
			vVelocity.z -= flHalfGravity;
			const float flMax = tMoveData.m_flMaxSpeed * 1.2f, flSpeed = vVelocity.Length();
			if (flMax > 0.f && flSpeed > flMax)
				vVelocity *= flMax / flSpeed;
			vVelocity.z += JumpVelocity;
			tMoveStorage.m_bOnGround = false;
			tMoveStorage.m_iGroundTicks = 0;
			if (!tMoveStorage.m_bLocal && !tMoveStorage.m_bBunnyHop && tMoveStorage.m_iHopTicks >= 0)
			{
				tMoveStorage.m_iPredictedHops++;
				if (tMoveStorage.m_iMaxHops > 0 && tMoveStorage.m_iPredictedHops >= tMoveStorage.m_iMaxHops)
					tMoveStorage.m_iHopTicks = -1;
			}
		}
	}

	const bool bSwimming = tMoveStorage.m_iWaterLevel > 1;

	if (tMoveStorage.m_bParachute && !tMoveStorage.m_bOnGround && !bSwimming && vVelocity.z < 0.f)
	{
		vVelocity.z = std::max(vVelocity.z, tPhysics.m_flParachuteZ);
		const float flDrag = tPhysics.m_flParachuteXY;
		const float flReduceX = fabsf(vVelocity.x) > flDrag ? (fabsf(vVelocity.x) - flDrag) / 3.f - 10.f : 0.f;
		const float flReduceY = fabsf(vVelocity.y) > flDrag ? (fabsf(vVelocity.y) - flDrag) / 3.f - 10.f : 0.f;
		vVelocity.x = std::clamp(vVelocity.x, -flDrag - flReduceX, flDrag + flReduceX);
		vVelocity.y = std::clamp(vVelocity.y, -flDrag - flReduceY, flDrag + flReduceY);
	}

	if (!tMoveStorage.m_bOnGround && !bSwimming)
		vVelocity.z -= flHalfGravity;
	ClampVelocity(vVelocity, tPhysics.m_flMaxVelocity);

	if (!bSwimming && tMoveStorage.m_bOnGround)
	{
		vVelocity.z = 0.f;
		GroundFriction(vVelocity, tMoveStorage.m_flSurfaceFriction, tPhysics);
	}

	const bool bWasAirborne = !tMoveStorage.m_bOnGround;
	const float flPreMoveSpeed2D = vVelocity.Length2D();
	if (bSwimming)
		WaterMove(tMoveStorage);
	else if (tMoveStorage.m_bOnGround)
		WalkMove(tMoveStorage);
	else
		AirMove(tMoveStorage);

	CategorizePosition(tMoveStorage);
	if (bWasAirborne && tMoveStorage.m_bOnGround)
	{
		tMoveStorage.m_iGroundTicks = 1;
		if (!tMoveStorage.m_bLocal && !tMoveStorage.m_bCharging && tMoveStorage.m_tGround.m_iModel == MoveModelEnum::None)
		{
			const float flSpeed = vVelocity.Length2D();
			const float flCap = GroundCap(tMoveData.m_flMaxSpeed, tMoveStorage.m_bLostFooting, false, 0.f) * (tMoveStorage.m_bDucked ? DuckSpeedCrop : 1.f);
			const bool bDecelOrLow = flSpeed <= 1.f || tMoveStorage.m_bLostFooting
				|| flSpeed < flPreMoveSpeed2D - 10.f
				|| (tMoveStorage.m_tAir.m_iModel == MoveModelEnum::None && tMoveStorage.m_iHopTicks < 0 && flCap > 0.f && flSpeed < flCap * 0.25f);
			if (bDecelOrLow)
				tMoveStorage.m_tGround = { MoveModelEnum::Stop };
			else
				tMoveStorage.m_tGround = { MoveModelEnum::World, Heading(vVelocity), flCap > 0.f ? std::min(flSpeed / flCap, 1.f) : 1.f };
		}
	}

	if (!bSwimming)
		vVelocity.z -= flHalfGravity;
	if (tMoveStorage.m_bOnGround)
		vVelocity.z = 0.f;
	ClampVelocity(vVelocity, tPhysics.m_flMaxVelocity);

	if (!tMoveStorage.m_bLocal)
	{
		if (bNeuralStep)
		{
			tMoveStorage.m_flViewYaw += tNeuralKnot.m_flTurnRateDeg;
		}
		else
		{
			auto& tController = tMoveStorage.m_bOnGround ? tMoveStorage.m_tGround : tMoveStorage.m_tAir;
			tMoveStorage.m_flViewYaw += tController.m_flTurn;
			tMoveStorage.m_tGround.m_flTurn *= EyeTurnFade, tMoveStorage.m_tAir.m_flTurn *= EyeTurnFade;
			if (!tMoveStorage.m_bOnGround)
				tMoveStorage.m_flSweepKept *= SweepFade;
			if (tMoveStorage.m_bOnGround && !tMoveStorage.m_bCharging
				&& (tMoveStorage.m_tGround.m_iModel == MoveModelEnum::World || tMoveStorage.m_tGround.m_iModel == MoveModelEnum::View))
			{
				const float flConf = std::clamp(tMoveStorage.m_tGround.m_flConfidence, 0.f, 1.f);
				const int iSteadyTicks = 12 + int(11.f * flConf);
				if (tMoveStorage.m_iSimTicks >= iSteadyTicks)
				{
					tMoveStorage.m_tGround.m_flScale *= 0.980f + 0.016f * flConf;
					if (tMoveStorage.m_tGround.m_flScale < 0.02f)
						tMoveStorage.m_tGround.m_flScale = 0.f;
				}
			}
		}
		tMoveData.m_vecViewAngles.y = tMoveStorage.m_flViewYaw;
	}
	tMoveStorage.m_iSimTicks++;
	tMoveStorage.m_iNeuralStep++;

	if (pCallback)
		(*pCallback)(tMoveData);

	tMoveStorage.m_flSimTime = ROUND_TO_TICKS(tMoveStorage.m_flSimTime + TICK_INTERVAL);
	tMoveStorage.m_bPredictNetworked = TIME_TO_TICKS(tMoveStorage.m_flSimTime) >= TIME_TO_TICKS(tMoveStorage.m_flPredictedSimTime);
	if (tMoveStorage.m_bPredictNetworked)
	{
		tMoveStorage.m_vPredictedOrigin = tMoveData.m_vecAbsOrigin;
		tMoveStorage.m_flPredictedSimTime += tMoveStorage.m_flPredictedDelta;
	}

	if (bPath)
		tMoveStorage.m_vPath.push_back(tMoveData.m_vecAbsOrigin);
}

CMovementSimulation::MoveCacheStep_t CMovementSimulation::MakeCacheStep(const MoveStorage& tMoveStorage)
{
	return {
		tMoveStorage.m_MoveData.m_vecAbsOrigin,
		tMoveStorage.m_MoveData.m_vecVelocity,
		tMoveStorage.m_vPredictedOrigin,
		tMoveStorage.m_tGround,
		tMoveStorage.m_tAir,
		tMoveStorage.m_flViewYaw,
		tMoveStorage.m_flSweepKept,
		tMoveStorage.m_MoveData.m_flMaxSpeed,
		tMoveStorage.m_tPhysics.m_flAirCap,
		tMoveStorage.m_flChargeMeter,
		tMoveStorage.m_flSurfaceFriction,
		tMoveStorage.m_iWaterLevel,
		tMoveStorage.m_iHopTicks,
		tMoveStorage.m_iGroundTicks,
		tMoveStorage.m_iPredictedHops,
		tMoveStorage.m_iSimTicks,
		tMoveStorage.m_bOnGround,
		tMoveStorage.m_bLostFooting,
		tMoveStorage.m_bAirCurrent,
		tMoveStorage.m_bCharging,
		tMoveStorage.m_flSimTime,
		tMoveStorage.m_flPredictedSimTime,
		tMoveStorage.m_bPredictNetworked,
		tMoveStorage.m_iNeuralStep
	};
}

void CMovementSimulation::ApplyCacheStep(MoveStorage& tMoveStorage, const MoveCacheStep_t& tStep, bool bPath)
{
	tMoveStorage.m_MoveData.m_vecAbsOrigin = tStep.m_vAbsOrigin;
	tMoveStorage.m_MoveData.m_vecVelocity = tStep.m_vVelocity;
	tMoveStorage.m_vPredictedOrigin = tStep.m_vPredictedOrigin;
	tMoveStorage.m_tGround = tStep.m_tGround;
	tMoveStorage.m_tAir = tStep.m_tAir;
	tMoveStorage.m_flViewYaw = tStep.m_flViewYaw;
	tMoveStorage.m_MoveData.m_vecViewAngles.y = tStep.m_flViewYaw;
	tMoveStorage.m_flSweepKept = tStep.m_flSweepKept;
	tMoveStorage.m_MoveData.m_flMaxSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed = tStep.m_flMaxSpeed;
	tMoveStorage.m_tPhysics.m_flAirCap = tStep.m_flAirCap;
	tMoveStorage.m_flChargeMeter = tStep.m_flChargeMeter;
	tMoveStorage.m_flSurfaceFriction = tStep.m_flSurfaceFriction;
	tMoveStorage.m_iWaterLevel = tStep.m_iWaterLevel;
	tMoveStorage.m_iHopTicks = tStep.m_iHopTicks;
	tMoveStorage.m_iGroundTicks = tStep.m_iGroundTicks;
	tMoveStorage.m_iPredictedHops = tStep.m_iPredictedHops;
	tMoveStorage.m_iSimTicks = tStep.m_iSimTicks;
	tMoveStorage.m_bOnGround = tStep.m_bOnGround;
	tMoveStorage.m_bLostFooting = tStep.m_bLostFooting;
	tMoveStorage.m_bAirCurrent = tStep.m_bAirCurrent;
	tMoveStorage.m_bCharging = tStep.m_bCharging;
	tMoveStorage.m_flSimTime = tStep.m_flSimTime;
	tMoveStorage.m_flPredictedSimTime = tStep.m_flPredictedSimTime;
	tMoveStorage.m_bPredictNetworked = tStep.m_bPredictNetworked;
	tMoveStorage.m_iNeuralStep = tStep.m_iNeuralStep;
	if (bPath)
		tMoveStorage.m_vPath.push_back(tStep.m_vAbsOrigin);
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback* pCallback)
{
	if (tMoveStorage.m_bFailed || !tMoveStorage.m_pPlayer)
		return;

	if (!pCallback && tMoveStorage.m_iCacheSlot >= 0 && tMoveStorage.m_iCacheSlot < int(m_vCache.size()))
	{
		auto& tEntry = m_vCache[tMoveStorage.m_iCacheSlot];
		if (tEntry.m_iEntIndex == tMoveStorage.m_pPlayer->entindex())
		{
			if (tMoveStorage.m_iCacheStep < int(tEntry.m_vSteps.size()))
			{
				PROJ_COUNT(MoveTicks);
				PROJ_COUNT(MoveTicksCached);

				ApplyCacheStep(tMoveStorage, tEntry.m_vSteps[tMoveStorage.m_iCacheStep++], bPath);
				return;
			}

			StepTick(tEntry.m_tFrontier, false, nullptr);
			tEntry.m_vSteps.push_back(MakeCacheStep(tEntry.m_tFrontier));
			tMoveStorage.m_iCacheStep++;
			ApplyCacheStep(tMoveStorage, tEntry.m_vSteps.back(), bPath);
			return;
		}
		tMoveStorage.m_iCacheSlot = -1;
	}

	StepTick(tMoveStorage, bPath, pCallback);
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback fCallback)
{
	RunTick(tMoveStorage, bPath, &fCallback);
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

std::pair<float, float> CMovementSimulation::GetUncertaintyAtTick(const MoveStorage& tMoveStorage, int iTick) const
{
	if ((tMoveStorage.m_bNeuralGround || tMoveStorage.m_bNeuralAir) && tMoveStorage.m_tNeural.m_bValid)
	{
		const auto tKnot = tMoveStorage.m_tNeural.SampleAtTick(std::max(iTick, 1));
		return { tKnot.m_flSigmaXY, tKnot.m_flSigmaZ };
	}
	const auto& tController = tMoveStorage.m_bOnGround ? tMoveStorage.m_tGround : tMoveStorage.m_tAir;
	const float flConf = std::clamp(tController.m_flConfidence, 0.05f, 1.f);
	const float flScale = (1.f - flConf) * 48.f + float(std::max(iTick, 1)) * 0.6f;
	return { flScale, flScale * 0.5f };
}
