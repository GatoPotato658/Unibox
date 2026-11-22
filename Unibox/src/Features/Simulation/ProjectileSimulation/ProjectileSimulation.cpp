#include "ProjectileSimulation.h"

#include "../../EnginePrediction/EnginePrediction.h"
#include "../../NoSpread/NoSpreadProjectile/NoSpreadProjectile.h"
#include "../../CritHack/CritHack.h"
#include "../../Backtrack/Backtrack.h"
#include "../../Aimbot/AimbotProjectile/ProjProfiler.h"

constexpr double k_dDeltaPsi = 1.0 / 66.0;

static void QuatToMatrix3(const double q[4], double m[3][3])
{
	const double x2 = q[0] + q[0];
	const double y2 = q[1] + q[1];
	const double z2 = q[2] + q[2];

	const double xx = q[0] * x2;
	const double xy = q[0] * y2;
	const double xz = q[0] * z2;

	const double yy = q[1] * y2;
	const double yz = q[1] * z2;
	const double zz = q[2] * z2;

	const double wx = q[3] * x2;
	const double wy = q[3] * y2;
	const double wz = q[3] * z2;

	m[0][0] = 1.0 - (yy + zz);
	m[0][1] = xy - wz;
	m[0][2] = xz + wy;

	m[1][0] = xy + wz;
	m[1][1] = 1.0 - (xx + zz);
	m[1][2] = yz - wx;

	m[2][0] = xz - wy;
	m[2][1] = yz + wx;
	m[2][2] = 1.0 - (xx + yy);
}

static void Matrix3ToQuat(const double m[3][3], double q[4])
{
	const double dTrace = m[0][0] + m[1][1] + m[2][2];
	if (dTrace > 0.0)
	{
		double s = std::sqrt(dTrace + 1.0);
		q[3] = s * 0.5;
		s = 0.5 / s;
		q[0] = (m[2][1] - m[1][2]) * s;
		q[1] = (m[0][2] - m[2][0]) * s;
		q[2] = (m[1][0] - m[0][1]) * s;
	}
	else
	{
		static constexpr int aNext[3] = { 1, 2, 0 };
		int i = 0;
		if (m[1][1] > m[0][0])
			i = 1;
		if (m[2][2] > m[i][i])
			i = 2;
		const int j = aNext[i];
		const int k = aNext[j];

		double s = std::sqrt((m[i][i] - (m[j][j] + m[k][k])) + 1.0);
		q[i] = s * 0.5;
		if (s != 0.0)
			s = 0.5 / s;
		q[3] = (m[k][j] - m[j][k]) * s;
		q[j] = (m[j][i] + m[i][j]) * s;
		q[k] = (m[k][i] + m[i][k]) * s;
	}

	const double dNorm2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
	if (dNorm2 > 1e-19)
	{
		const double dInvNorm = 1.0 / std::sqrt(dNorm2);
		q[0] *= dInvNorm;
		q[1] *= dInvNorm;
		q[2] *= dInvNorm;
		q[3] *= dInvNorm;
	}
}

static void QuatMult(const double a[4], const double b[4], double out[4])
{
	const double x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	const double y = a[3] * b[1] + a[1] * b[3] + a[2] * b[0] - a[0] * b[2];
	const double z = a[3] * b[2] + a[2] * b[3] + a[0] * b[1] - a[1] * b[0];
	const double w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	out[0] = x;
	out[1] = y;
	out[2] = z;
	out[3] = w;
}

static void FastNormizeQuat(double q[4])
{
	const double dLen2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
	if (std::fabs(1.0 - dLen2) > 1e-12)
	{
		const double dInv = 1.0 / std::sqrt(dLen2);
		q[0] *= dInv;
		q[1] *= dInv;
		q[2] *= dInv;
		q[3] *= dInv;
	}
}

static void SetFastMultipleWithClip(const float aAngles[3], double dFactor, double q[4])
{
	const double dHalf = dFactor * 0.5;
	q[0] = std::sin(double(aAngles[0]) * dHalf);
	q[1] = std::sin(double(aAngles[1]) * dHalf);
	q[2] = std::sin(double(aAngles[2]) * dHalf);

	double dS2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
	if (dS2 > 1.0)
	{
		const double dScale = 0.999999999 / std::sqrt(dS2);
		q[0] *= dScale;
		q[1] *= dScale;
		q[2] *= dScale;
		dS2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
	}
	q[3] = std::sqrt(std::max(0.0, 1.0 - dS2));
}

static void InitPhysicsState(PhysicsObject_t& tObj, const Vec3& vPos, const QAngle& vAng, const Vec3& vVel, const Vec3& vAngVel, float flGravity, float flDrag, const Vec3& vDragBasis, const Vec3& vAngDragBasis, float flMaxVel, float flMaxAngVel)
{
	constexpr double dMetersPerInch = double(METERS_PER_INCH);
	constexpr float flInvMetersPerInch = 1.f / METERS_PER_INCH;

	tObj.m_vOrigin = vPos;

	tObj.m_adPosLastPsi[0] = double(vPos.x) * dMetersPerInch;
	tObj.m_adPosLastPsi[1] = -double(vPos.z) * dMetersPerInch;
	tObj.m_adPosLastPsi[2] = double(vPos.y) * dMetersPerInch;

	tObj.m_afVelLastPsi[0] = 0.f;
	tObj.m_afVelLastPsi[1] = 0.f;
	tObj.m_afVelLastPsi[2] = 0.f;

	tObj.m_afSpeed[0] = float(double(vVel.x) * dMetersPerInch);
	tObj.m_afSpeed[1] = float(-double(vVel.z) * dMetersPerInch);
	tObj.m_afSpeed[2] = float(double(vVel.y) * dMetersPerInch);

	tObj.m_afRotSpeed[0] = Math::Deg2Rad(vAngVel.x);
	tObj.m_afRotSpeed[1] = -Math::Deg2Rad(vAngVel.z);
	tObj.m_afRotSpeed[2] = Math::Deg2Rad(vAngVel.y);

	tObj.m_flMaxVelIVP = flMaxVel * METERS_PER_INCH;
	tObj.m_flMaxAngVelIVP = Math::Deg2Rad(flMaxAngVel);

	if (tObj.m_flMaxVelIVP > 0.f)
	{
		const double dVelLen = std::sqrt(double(tObj.m_afSpeed[0]) * tObj.m_afSpeed[0] + double(tObj.m_afSpeed[1]) * tObj.m_afSpeed[1] + double(tObj.m_afSpeed[2]) * tObj.m_afSpeed[2]);
		if (float(dVelLen) > tObj.m_flMaxVelIVP)
		{
			const double dScale = double(tObj.m_flMaxVelIVP) / dVelLen;
			tObj.m_afSpeed[0] = float(double(tObj.m_afSpeed[0]) * dScale);
			tObj.m_afSpeed[1] = float(double(tObj.m_afSpeed[1]) * dScale);
			tObj.m_afSpeed[2] = float(double(tObj.m_afSpeed[2]) * dScale);
		}
	}

	if (tObj.m_flMaxAngVelIVP > 0.f)
	{
		const float flMaxAngCheck = float(66.0 * double(tObj.m_flMaxAngVelIVP * float(k_dDeltaPsi)));
		const double dVelLen = std::sqrt(double(tObj.m_afSpeed[0]) * tObj.m_afSpeed[0] + double(tObj.m_afSpeed[1]) * tObj.m_afSpeed[1] + double(tObj.m_afSpeed[2]) * tObj.m_afSpeed[2]);
		if (float(dVelLen) > flMaxAngCheck)
		{
			const double dScale = double(flMaxAngCheck) / dVelLen;
			tObj.m_afRotSpeed[0] = float(double(tObj.m_afRotSpeed[0]) * dScale);
			tObj.m_afRotSpeed[1] = float(double(tObj.m_afRotSpeed[1]) * dScale);
			tObj.m_afRotSpeed[2] = float(double(tObj.m_afRotSpeed[2]) * dScale);
		}
	}

	tObj.m_vVelocity = {
		tObj.m_afSpeed[0] * flInvMetersPerInch,
		tObj.m_afSpeed[2] * flInvMetersPerInch,
		-tObj.m_afSpeed[1] * flInvMetersPerInch
	};

	Vec3 vForward, vRight, vUp;
	Math::AngleVectors(vAng, &vForward, &vRight, &vUp);
	const double m[3][3] = {
		{ double(vForward.x), double(-vUp.x), double(-vRight.x) },
		{ double(-vForward.z), double(vUp.z), double(vRight.z) },
		{ double(vForward.y), double(-vUp.y), double(-vRight.y) }
	};
	Matrix3ToQuat(m, tObj.m_adQuatNextPsi);

	tObj.m_dCurrentTime = 0.0;
	tObj.m_dLastPsiTime = 0.0;
	tObj.m_dNextPsiTime = 0.0;

	tObj.m_flGravityIVP = flGravity * METERS_PER_INCH;
	tObj.m_flDragCoeff = flDrag;
	tObj.m_vDragBasis = vDragBasis;
	tObj.m_vAngDragBasis = vAngDragBasis;
}

static void StepPhysicsPsi(PhysicsObject_t& tObj)
{
	double m[3][3];
	QuatToMatrix3(tObj.m_adQuatNextPsi, m);

	tObj.m_afSpeed[1] += tObj.m_flGravityIVP * float(k_dDeltaPsi);

	const float afLocal[3] = {
		float(m[0][0] * double(tObj.m_afSpeed[0]) + m[1][0] * double(tObj.m_afSpeed[1]) + m[2][0] * double(tObj.m_afSpeed[2])),
		float(m[0][1] * double(tObj.m_afSpeed[0]) + m[1][1] * double(tObj.m_afSpeed[1]) + m[2][1] * double(tObj.m_afSpeed[2])),
		float(m[0][2] * double(tObj.m_afSpeed[0]) + m[1][2] * double(tObj.m_afSpeed[1]) + m[2][2] * double(tObj.m_afSpeed[2]))
	};

	const float flLinDrag = tObj.m_flDragCoeff * std::fabs(afLocal[0] * tObj.m_vDragBasis.x)
		+ std::fabs(afLocal[1] * tObj.m_vDragBasis.y)
		+ std::fabs(afLocal[2] * tObj.m_vDragBasis.z);
	float flDragForce = float(-0.5 * double(flLinDrag) * double(AIR_DENSITY) * k_dDeltaPsi);
	if (flDragForce < -1.0f)
		flDragForce = -1.0f;
	if (flDragForce < 0.0f)
	{
		tObj.m_afSpeed[0] += tObj.m_afSpeed[0] * flDragForce;
		tObj.m_afSpeed[1] += tObj.m_afSpeed[1] * flDragForce;
		tObj.m_afSpeed[2] += tObj.m_afSpeed[2] * flDragForce;
	}

	const float flAngDrag = tObj.m_flDragCoeff * std::fabs(tObj.m_afRotSpeed[0] * tObj.m_vAngDragBasis.x)
		+ std::fabs(tObj.m_afRotSpeed[1] * tObj.m_vAngDragBasis.y)
		+ std::fabs(tObj.m_afRotSpeed[2] * tObj.m_vAngDragBasis.z);
	float flAngDragForce = float(-double(flAngDrag) * double(AIR_DENSITY) * k_dDeltaPsi);
	if (flAngDragForce < -1.0f)
		flAngDragForce = -1.0f;
	if (flAngDragForce < 0.0f)
	{
		tObj.m_afRotSpeed[0] += tObj.m_afRotSpeed[0] * flAngDragForce;
		tObj.m_afRotSpeed[1] += tObj.m_afRotSpeed[1] * flAngDragForce;
		tObj.m_afRotSpeed[2] += tObj.m_afRotSpeed[2] * flAngDragForce;
	}

	if (tObj.m_flMaxAngVelIVP > 0.f)
	{
		const double dRotLen2 = double(tObj.m_afRotSpeed[0]) * tObj.m_afRotSpeed[0]
			+ double(tObj.m_afRotSpeed[1]) * tObj.m_afRotSpeed[1]
			+ double(tObj.m_afRotSpeed[2]) * tObj.m_afRotSpeed[2];
		const double dMaxAng = double(tObj.m_flMaxAngVelIVP);
		if (dRotLen2 > dMaxAng * dMaxAng)
		{
			const float flScale = float((dMaxAng * 0.9) / std::sqrt(dRotLen2));
			tObj.m_afRotSpeed[0] *= flScale;
			tObj.m_afRotSpeed[1] *= flScale;
			tObj.m_afRotSpeed[2] *= flScale;
		}
	}

	if (tObj.m_flMaxVelIVP > 0.f)
	{
		const double dVelLen2 = double(tObj.m_afSpeed[0]) * tObj.m_afSpeed[0]
			+ double(tObj.m_afSpeed[1]) * tObj.m_afSpeed[1]
			+ double(tObj.m_afSpeed[2]) * tObj.m_afSpeed[2];
		const double dMaxVel = double(tObj.m_flMaxVelIVP);
		if (dVelLen2 > dMaxVel * dMaxVel)
		{
			const float flScale = float((dMaxVel * 0.99) / std::sqrt(dVelLen2));
			tObj.m_afSpeed[0] *= flScale;
			tObj.m_afSpeed[1] *= flScale;
			tObj.m_afSpeed[2] *= flScale;
		}
	}

	double qRot[4];
	SetFastMultipleWithClip(tObj.m_afRotSpeed, k_dDeltaPsi, qRot);

	const double dDeltaPos = tObj.m_dNextPsiTime - tObj.m_dLastPsiTime;
	tObj.m_dLastPsiTime = tObj.m_dNextPsiTime;
	tObj.m_adPosLastPsi[0] += double(tObj.m_afVelLastPsi[0]) * dDeltaPos;
	tObj.m_adPosLastPsi[1] += double(tObj.m_afVelLastPsi[1]) * dDeltaPos;
	tObj.m_adPosLastPsi[2] += double(tObj.m_afVelLastPsi[2]) * dDeltaPos;

	tObj.m_afVelLastPsi[0] = tObj.m_afSpeed[0];
	tObj.m_afVelLastPsi[1] = tObj.m_afSpeed[1];
	tObj.m_afVelLastPsi[2] = tObj.m_afSpeed[2];

	double qNext[4];
	QuatMult(tObj.m_adQuatNextPsi, qRot, qNext);
	FastNormizeQuat(qNext);
	tObj.m_adQuatNextPsi[0] = qNext[0];
	tObj.m_adQuatNextPsi[1] = qNext[1];
	tObj.m_adQuatNextPsi[2] = qNext[2];
	tObj.m_adQuatNextPsi[3] = qNext[3];

	tObj.m_dNextPsiTime += k_dDeltaPsi;
}

static void SimulatePhysicsTick(PhysicsObject_t& tObj, float flDeltaTime)
{
	constexpr float flInvMetersPerInch = 1.f / METERS_PER_INCH;

	const double dTarget = tObj.m_dCurrentTime + double(flDeltaTime);
	while (float(tObj.m_dNextPsiTime) < float(dTarget))
		StepPhysicsPsi(tObj);
	tObj.m_dCurrentTime = dTarget;

	const double dSubTime = std::max(0.0, tObj.m_dCurrentTime - tObj.m_dLastPsiTime);
	tObj.m_vOrigin = {
		float(tObj.m_adPosLastPsi[0] + double(tObj.m_afVelLastPsi[0]) * dSubTime) * flInvMetersPerInch,
		float(tObj.m_adPosLastPsi[2] + double(tObj.m_afVelLastPsi[2]) * dSubTime) * flInvMetersPerInch,
		-float(tObj.m_adPosLastPsi[1] + double(tObj.m_afVelLastPsi[1]) * dSubTime) * flInvMetersPerInch
	};
	tObj.m_vVelocity = {
		tObj.m_afSpeed[0] * flInvMetersPerInch,
		tObj.m_afSpeed[2] * flInvMetersPerInch,
		-tObj.m_afSpeed[1] * flInvMetersPerInch
	};
}

static const char* GetPasstimeThrowSpeedCvar(CTFPlayer* pPlayer)
	{
		switch (pPlayer ? pPlayer->m_iClass() : TF_CLASS_UNDEFINED)
		{
		case TF_CLASS_SCOUT: return "tf_passtime_throwspeed_scout";
		case TF_CLASS_SOLDIER: return "tf_passtime_throwspeed_soldier";
		case TF_CLASS_PYRO: return "tf_passtime_throwspeed_pyro";
		case TF_CLASS_DEMOMAN: return "tf_passtime_throwspeed_demoman";
		case TF_CLASS_HEAVY: return "tf_passtime_throwspeed_heavy";
		case TF_CLASS_ENGINEER: return "tf_passtime_throwspeed_engineer";
		case TF_CLASS_MEDIC: return "tf_passtime_throwspeed_medic";
		case TF_CLASS_SNIPER: return "tf_passtime_throwspeed_sniper";
		case TF_CLASS_SPY: return "tf_passtime_throwspeed_spy";
		default: return "tf_passtime_throwspeed_scout";
		}
	}

static const char* GetPasstimeThrowArcCvar(CTFPlayer* pPlayer)
	{
		switch (pPlayer ? pPlayer->m_iClass() : TF_CLASS_UNDEFINED)
		{
		case TF_CLASS_SCOUT: return "tf_passtime_throwarc_scout";
		case TF_CLASS_SOLDIER: return "tf_passtime_throwarc_soldier";
		case TF_CLASS_PYRO: return "tf_passtime_throwarc_pyro";
		case TF_CLASS_DEMOMAN: return "tf_passtime_throwarc_demoman";
		case TF_CLASS_HEAVY: return "tf_passtime_throwarc_heavy";
		case TF_CLASS_ENGINEER: return "tf_passtime_throwarc_engineer";
		case TF_CLASS_MEDIC: return "tf_passtime_throwarc_medic";
		case TF_CLASS_SNIPER: return "tf_passtime_throwarc_sniper";
		case TF_CLASS_SPY: return "tf_passtime_throwarc_spy";
		default: return "tf_passtime_throwarc_scout";
		}
	}

const SpecialProjectile_t* CProjectileSimulation::GetSpecial(int iSpecial)
{
	static constexpr const char* sNade = "models/player/infection/w_grenade_emp.mdl";
	static constexpr float flNoLifetime = std::numeric_limits<float>::max();
	static const SpecialProjectile_t aSpecials[] = {
		{ "", 0, "", 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f },
		{ "Sniper spit", FNV1A::Hash32Const("zi/sniper_spit"), sNade, 2000.f, 0.f, 50.f, 0.f, 12.f, flNoLifetime, 130.f - 50.f * 0.70710678f, 130.f - 50.f * 0.70710678f, 0.5f },
		{ "Spy EMP", FNV1A::Hash32Const("zi/spy_emp"), sNade, 1500.f, 0.f, -20.f, 0.f, 0.f, 1.75f, 900.f, 450.f, 0.f },
		{ "Pyro spew", FNV1A::Hash32Const("zi/pyro_spew"), sNade, 1200.f, 0.f, 32.f, 0.f, 12.f, 5.f, 0.f, 0.f, 0.f },
		{ "Heavy rock", FNV1A::Hash32Const("zi/heavy_rock"), sNade, 1100.f, 350.f, 40.f, -10.f, 12.f, 6.f, 150.f, 150.f, 1.9f },
	};
	return iSpecial > ProjSpecialEnum::None && iSpecial <= ProjSpecialEnum::HeavyRock ? &aSpecials[iSpecial] : nullptr;
}

float CProjectileSimulation::GetSpecialHull(const SpecialProjectile_t* pSpecial)
{
	if (pSpecial->m_flHull)
		return pSpecial->m_flHull;

	const int iIndex = I::ModelInfoClient->GetModelIndex(pSpecial->m_sModel);
	const model_t* pModel = iIndex > 0 ? I::ModelInfoClient->GetModel(iIndex) : nullptr;
	if (!pModel)
		return 0.f;

	Vec3 vMins, vMaxs; I::ModelInfoClient->GetModelBounds(pModel, vMins, vMaxs);
	return std::max({ fabsf(vMins.x), fabsf(vMins.y), fabsf(vMins.z), fabsf(vMaxs.x), fabsf(vMaxs.y), fabsf(vMaxs.z) });
}

float CProjectileSimulation::GetSpecialDrag(const SpecialProjectile_t* pSpecial)
{
	if (!pSpecial)
		return 0.f;
	if (!m_pEnv)
	{
		m_pEnv = I::Physics->CreateEnvironment();
		if (m_pEnv)
			m_pEnv->SetAirDensity(AIR_DENSITY);
	}

	const int iSpecial = int(pSpecial - GetSpecial(ProjSpecialEnum::SniperSpit)) + ProjSpecialEnum::SniperSpit;
	if (iSpecial <= ProjSpecialEnum::None || iSpecial > ProjSpecialEnum::HeavyRock)
		return 0.f;
	if (m_pSpecialCollide && m_aSpecialDragFor[iSpecial] == m_pSpecialCollide)
		return m_aSpecialDrag[iSpecial];

	ProjectileInfo* pOldCurrent = m_pCurrent;
	const bool bOldPhysics = m_bPhysics;
	const bool bOldAngDrag = m_bAngDrag;
	const bool bOldVPhysics = m_bVPhysics;
	const PhysicsObject_t tOldObj = m_tObj;
	const SpecialProjectile_t* pOldSpecial = m_pSpecial;
	m_pSpecial = pSpecial;

	float flDrag = 0.f;
	ProjectileInfo tInfo = {};
	tInfo.m_uType = pSpecial->m_uType;
	tInfo.m_flVelocity = pSpecial->m_flSpeed;
	tInfo.m_flGravity = 0.f;
	if (Initialize(tInfo) && m_bPhysics && m_pSpecialCollide)
	{
		const float flSpeed = GetVelocity().Length();
		constexpr float flTimes[] = { 0.5f, 1.f };
		float aFit[2] = {};
		for (int i = 0, iTicks = 0; i < 2; i++)
		{
			const int iTarget = TIME_TO_TICKS(flTimes[i]);
			for (; iTicks < iTarget; iTicks++)
				RunTick(tInfo, false);

			const float flNow = GetVelocity().Length();
			aFit[i] = flNow > 0.f ? (flSpeed / flNow - 1.f) / (flSpeed * TICKS_TO_TIME(iTarget)) : 0.f;
		}
		flDrag = aFit[0];
		m_aSpecialDragFor[iSpecial] = m_pSpecialCollide;
		m_aSpecialDrag[iSpecial] = flDrag;

		if (Vars::Debug::Logging.Value)
			SDK::Output("ProjSim", std::format("{}: drag c={:.3e} (0.5s) {:.3e} (1s), basis {:.5f} {:.5f} {:.5f}", pSpecial->m_sName, aFit[0], aFit[1], m_pObj->m_dragBasis.x, m_pObj->m_dragBasis.y, m_pObj->m_dragBasis.z).c_str(), { 120, 200, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	}

	m_pSpecial = pOldSpecial;
	m_pCurrent = pOldCurrent;
	m_bPhysics = bOldPhysics;
	m_bAngDrag = bOldAngDrag;
	m_bVPhysics = bOldVPhysics;
	m_tObj = tOldObj;
	return flDrag;
}

static float ReadSolidValue(const char* sText, const char* sKey, float flDefault)
{
	if (!sText)
		return flDefault;

	const std::string sNeedle = std::format("\"{}\" \"", sKey);
	const char* sFound = strstr(sText, sNeedle.c_str());
	return sFound ? strtof(sFound + sNeedle.size(), nullptr) : flDefault;
}

static IPhysicsObject* CreateSpecialObject(IPhysicsEnvironment* pEnv, const SpecialProjectile_t* pSpecial, const objectparams_t& tDefault, const CPhysCollide*& pCollideOut, IPhysicsObject*& pObject)
{
	const int iIndex = I::ModelInfoClient->GetModelIndex(pSpecial->m_sModel);
	vcollide_t* pVCollide = iIndex > 0 ? I::ModelInfoClient->GetVCollide(iIndex) : nullptr;
	if (!pVCollide || !pVCollide->solidCount || !pVCollide->solids || !pVCollide->solids[0])
		return nullptr;

	if (pObject && pCollideOut != pVCollide->solids[0])
	{
		pEnv->DestroyObject(pObject);
		pObject = nullptr;
	}
	if (!pObject)
	{
		objectparams_t tParams = tDefault;
		tParams.mass = ReadSolidValue(pVCollide->pKeyValues, "mass", tParams.mass);
		tParams.inertia = ReadSolidValue(pVCollide->pKeyValues, "inertia", tParams.inertia);
		tParams.damping = ReadSolidValue(pVCollide->pKeyValues, "damping", tParams.damping);
		tParams.rotdamping = ReadSolidValue(pVCollide->pKeyValues, "rotdamping", tParams.rotdamping);
		tParams.volume = ReadSolidValue(pVCollide->pKeyValues, "volume", tParams.volume);
		tParams.enableCollisions = false;

		pObject = pEnv->CreatePolyObject(pVCollide->solids[0], 0, {}, {}, &tParams);
		pCollideOut = pVCollide->solids[0];
	}
	return pObject;
}

bool CProjectileSimulation::GetInfoMain(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon, Vec3 vAngles, ProjectileInfo& tProjInfo, int iFlags, float flAutoCharge)
{
	if (!pWeapon || !pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsTaunting())
		return false;

	m_tFireSetup = {};

	float flGravity = SDK::GetGravity(); // vphysics projectiles affected by server start gravity
	bool bDucking = pPlayer->m_fFlags() & FL_DUCKING;

	if (m_pSpecial)
	{
		Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
		Vec3 vEye = iFlags & ProjSimEnum::Interp ? pPlayer->GetEyePosition() : pPlayer->GetShootPos();
		Vec3 vPos = vEye + vForward * m_pSpecial->m_flSpawnDist + Vec3(0.f, 0.f, m_pSpecial->m_flSpawnHeight);
		tProjInfo.Set(pPlayer, pWeapon, m_pSpecial->m_uType, vPos, vAngles, Vec3::Get(GetSpecialHull(m_pSpecial)), m_pSpecial->m_flSpeed, flGravity, m_pSpecial->m_flLifetime, 0, pPlayer->m_vecVelocity() + Vec3(0.f, 0.f, m_pSpecial->m_flLift));
		return true;
	}

	bool bRedirect = iFlags & ProjSimEnum::Redirect;
	bool bInterp = iFlags & ProjSimEnum::Interp;
	bool bMaxSpeed = iFlags & ProjSimEnum::MaxSpeed;
	bool bPredictCmdNum = iFlags & ProjSimEnum::PredictCmdNum;
	bool bNoRandomAngles = iFlags & ProjSimEnum::NoRandomAngles;
	bool bCorrectRandomAngles = iFlags & ProjSimEnum::CorrectRandomAngles;

	Vec3 vPos, vAngle;

	if (bCorrectRandomAngles)
	{
		int iOriginalAttacking = G::Attacking;
		bool bOriginalSilent = G::PSilentAngles;

		G::DummyCmd.viewangles = vAngles;
		G::DummyCmd.command_number = bPredictCmdNum ? F::CritHack.PredictCmdNum(pPlayer, pWeapon, G::CurrentUserCmd) : G::CurrentUserCmd->command_number;
		G::Attacking = 1;
		F::NoSpreadProjectile.Run(pPlayer, pWeapon, &G::DummyCmd);
		vAngles = G::DummyCmd.viewangles;

		G::Attacking = iOriginalAttacking;
		G::PSilentAngles = bOriginalSilent;
	}

	if (Vars::Visuals::Trajectory::Override.Value)
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { Vars::Visuals::Trajectory::OffsetX.Value, Vars::Visuals::Trajectory::OffsetY.Value, Vars::Visuals::Trajectory::OffsetZ.Value }, vPos, vAngle, bRedirect ? Vars::Visuals::Trajectory::ForwardRedirect.Value : 0.f, Vars::Visuals::Trajectory::ForwardCutoff.Value, bInterp, true, &m_tFireSetup);

		auto uType = FNV1A::Hash32Const("custom");
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { Vars::Visuals::Trajectory::Hull.Value, Vars::Visuals::Trajectory::Hull.Value, Vars::Visuals::Trajectory::Hull.Value }, Vars::Visuals::Trajectory::Speed.Value, Vars::Visuals::Trajectory::Gravity.Value, Vars::Visuals::Trajectory::LifeTime.Value);
		return true;
	}

	const int iWeaponID = pWeapon->GetWeaponID();
	const bool bUsesGrenadeRand = iWeaponID == TF_WEAPON_GRENADELAUNCHER || iWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || iWeaponID == TF_WEAPON_CANNON;

	if (!bInterp && G::CurrentUserCmd && (!bNoRandomAngles || bUsesGrenadeRand))
	{
		switch (iWeaponID)
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
		case TF_WEAPON_RAYGUN:
		case TF_WEAPON_DRG_POMSON:
		case TF_WEAPON_GRENADELAUNCHER:
		case TF_WEAPON_PIPEBOMBLAUNCHER:
		case TF_WEAPON_CANNON:
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_FLAREGUN_REVENGE:
		case TF_WEAPON_COMPOUND_BOW:
		case TF_WEAPON_CROSSBOW:
		case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
		case TF_WEAPON_SYRINGEGUN_MEDIC:
		case TF_WEAPON_GRAPPLINGHOOK:
		{
			const int iCmdNum = bPredictCmdNum ? F::CritHack.PredictCmdNum(pPlayer, pWeapon, G::CurrentUserCmd) : G::CurrentUserCmd->command_number;
			const int iTick = I::GlobalVars->tickcount;
			if (m_tSpreadCache.m_iTick != iTick
				|| m_tSpreadCache.m_iCmdNum != iCmdNum
				|| m_tSpreadCache.m_iWeaponID != iWeaponID
				|| m_tSpreadCache.m_pPlayer != pPlayer
				|| m_tSpreadCache.m_pWeapon != pWeapon)
			{
				const float flOldCurrentTime = I::GlobalVars->curtime;
				I::GlobalVars->curtime = TICKS_TO_TIME(pPlayer->m_nTickBase());

				SDK::RandomSeed(SDK::SeedFileLineHash(MD5_PseudoRandom(iCmdNum) & 0x7FFFFFFF, "SelectWeightedSequence", 0));
				for (int i = 0; i < 6; ++i)
					SDK::RandomFloat();

				Vec3 vAngAdd = pWeapon->GetSpreadAngles() - pPlayer->EyeAngles();
				if (iWeaponID == TF_WEAPON_SYRINGEGUN_MEDIC)
				{
					vAngAdd.x += SDK::RandomFloat(-1.5f, 1.5f);
					vAngAdd.y += SDK::RandomFloat(-1.5f, 1.5f);
				}

				m_tSpreadCache.m_iTick = iTick;
				m_tSpreadCache.m_iCmdNum = iCmdNum;
				m_tSpreadCache.m_iWeaponID = iWeaponID;
				m_tSpreadCache.m_pPlayer = pPlayer;
				m_tSpreadCache.m_pWeapon = pWeapon;
				m_tSpreadCache.m_vAngAdd = vAngAdd;
				m_tSpreadCache.m_bHasGrenadeRand = bUsesGrenadeRand;
				if (bUsesGrenadeRand)
				{
					m_tSpreadCache.m_flUpRand = SDK::RandomFloat(-10.f, 10.f);
					m_tSpreadCache.m_flRightRand = SDK::RandomFloat(-10.f, 10.f);
					m_tSpreadCache.m_iAngYawRand = SDK::RandomInt(-1200, 1200);
				}

				I::GlobalVars->curtime = flOldCurrentTime;
			}
			if (!bNoRandomAngles)
				vAngles += m_tSpreadCache.m_vAngAdd;
		}
		}
	}

	switch (iWeaponID)
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 23.5f, int(SDK::AttribHookValue(0, "centerfire_projectile", pWeapon)) == 1 ? 0.f : 12.f, bDucking ? 8.f : -3.f }, vPos, vAngle, bRedirect ? 2000.f : 0.f, 0.1f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/weapons/w_models/w_rocket.mdl");
		float flSpeed = pPlayer->InCond(TF_COND_RUNE_PRECISION) ? 3000.f : SDK::AttribHookValue(1100.f, "mult_projectile_speed", pWeapon);
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 0.f, 0.f, 0.f }, flSpeed, 0.f);
		return true;
	}
	case TF_WEAPON_LASER_POINTER:
	{
		auto pSentryGun = pPlayer->GetObjectOfType(OBJ_SENTRYGUN)->As<CObjectSentrygun>();
		if (pSentryGun && !pSentryGun->IsDormant() && pSentryGun->GetAttachment(pSentryGun->LookupAttachment("rocket_l"), vPos))
		{
			auto uType = FNV1A::Hash32Const("models/buildables/sentry3_rockets.mdl");
			float flSpeed = pPlayer->InCond(TF_COND_RUNE_PRECISION) ? 3000.f : SDK::AttribHookValue(1100.f, "mult_projectile_speed", pWeapon);

			if (auto pEnemy = pSentryGun->m_hAutoAimTarget().Get()) vAngle = Math::CalcAngle(vPos, pEnemy->GetCenter());
			else if (auto pLaserDot = H::Entities.GetLaserDot()) vAngle = Math::CalcAngle(vPos, pLaserDot->GetAbsOrigin());
			else break;

			tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 0.f, 0.f, 0.f }, flSpeed, 0.f);
			return true;
		}
		break;
	}
	case TF_WEAPON_MECHANICAL_ARM:
	{
		auto pOwner = pWeapon->m_hOwner().Get()->As<CTFPlayer>();
		if (pOwner && pOwner->IsPlayer() && pOwner->m_iMetalCount() >= 65)
		{
			auto uType = FNV1A::Hash32Const("models/weapons/w_models/w_drg_ball.mdl");
			SDK::GetProjectileFireSetup(pPlayer, vAngles, { 40.f, 15.f, -10.f }, vPos, vAngle, 0.f, 0.f, bInterp, true, &m_tFireSetup);
			tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, Vec3(), 700.f, 0.f, 1.2f);
			return true;
		}
		break;
	}
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_RAYGUN:
	case TF_WEAPON_DRG_POMSON:
	{
		bool bCowMangler = iWeaponID == TF_WEAPON_PARTICLE_CANNON;

		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 23.5f, -8.f, bDucking ? 8.f : -3.f }, vPos, vAngle, bRedirect ? 2000.f : 0.f, 0.1f, bInterp, true, &m_tFireSetup);
		if (iWeaponID == TF_WEAPON_DRG_POMSON)
			vPos.z -= 13.f, m_tFireSetup.m_flHeight = -13.f;

		auto uType = FNV1A::Hash32Const("models/weapons/w_models/w_drg_ball.mdl");
		Vec3 vHull = bCowMangler ? Vec3() : Vec3(1.f, 1.f, 1.f);
		float flSpeed = bCowMangler ? 1100.f : 1200.f;
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, vHull, flSpeed, 0.f);
		return true;
	}
	case TF_WEAPON_GRENADELAUNCHER:
	case TF_WEAPON_CANNON:
	{
		bool bCannon = iWeaponID == TF_WEAPON_CANNON;
		float flMortar = bCannon ? SDK::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon) : 0.f;

		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 16.f, 8.f, -6.f }, vPos, vAngle, 0.f, 0.f, bInterp, true, &m_tFireSetup);
		
		auto uType = bCannon ? FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl") : FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl");
		Vec3 vHull = bCannon ? Vec3(6.f, 6.f, 6.f) : !SDK::AttribHookValue(0, "grenade_no_spin", pWeapon) ? Vec3(5.f, 5.f, 5.f) : Vec3(4.f, 4.f, 4.f);
		float flSpeed = SDK::AttribHookValue(pPlayer->InCond(TF_COND_RUNE_PRECISION) ? 3000.f : SDK::AttribHookValue(1200.f, "mult_projectile_speed", pWeapon), "mult_projectile_range", pWeapon);
		float flLifeTime = flMortar
			? pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f ? pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime : flMortar
			: SDK::AttribHookValue(2.f, "fuse_mult", pWeapon);
		flLifeTime = ceilf(flLifeTime / GRENADE_CHECK_INTERVAL) * GRENADE_CHECK_INTERVAL + GetDesync();
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, vHull, flSpeed, DEFAULT_GRAVITY, flLifeTime);
		return true;
	}
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 16.f, 8.f, -6.f }, vPos, vAngle, 0.f, 0.f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl");
		float flCharge = flAutoCharge > 0.f && SDK::AttribHookValue(1, "mult_dmg", pWeapon)
			? SDK::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon) * flAutoCharge
			: (pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() : 0.f);
		float flSpeed = SDK::AttribHookValue(bMaxSpeed ? 2400.f : Math::RemapVal(flCharge, 0.f, SDK::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 900.f, 2400.f), "mult_projectile_range", pWeapon);
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 5.f, 5.f, 5.f }, flSpeed, DEFAULT_GRAVITY);
		return true;
	}
	case TF_WEAPON_FLAREGUN:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 23.5f, 12.f, bDucking ? 8.f : -3.f }, vPos, vAngle, bRedirect ? 2000.f : 0.f, 0.1f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/weapons/w_models/w_flaregun_shell.mdl");
		float flSpeed = SDK::AttribHookValue(2000.f, "mult_projectile_speed", pWeapon);
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 0.f, 0.f, 0.f }, flSpeed, 0.3f * flGravity * (flSpeed / 2000.f));
		return true;
	}
	case TF_WEAPON_FLAREGUN_REVENGE:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 23.5f, 12.f, bDucking ? 8.f : -3.f }, vPos, vAngle, bRedirect ? 2000.f : 0.f, 0.1f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/weapons/w_models/w_flaregun_shell.mdl");
		float flSpeed = SDK::AttribHookValue(2000.f, "mult_projectile_speed", pWeapon);
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 0.f, 0.f, 0.f }, flSpeed, 0.3f * flGravity * (flSpeed / 2000.f));
		return true;
	}
	case TF_WEAPON_COMPOUND_BOW:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 23.5f, -8.f, -3.f }, vPos, vAngle, bRedirect ? 2000.f : 0.f, 0.1f, bInterp, true, &m_tFireSetup);

		auto uType = FNV1A::Hash32Const("models/weapons/w_models/w_arrow.mdl");
		auto pBow = pWeapon->As<CTFPipebombLauncher>();
		float flCharge = pBow->m_flChargeBeginTime() != 0.f ? std::min(I::GlobalVars->curtime - pBow->m_flChargeBeginTime(), 1.f) : 0.f;
		float flChargeMaxTime = pBow->GetChargeMaxTime();
		float flRatio = flChargeMaxTime != 0.f ? std::clamp(flCharge / flChargeMaxTime, 0.f, 1.f) : flCharge < flChargeMaxTime ? 0.f : 1.f;
		float flSpeed = bMaxSpeed ? 2600.f : 1800.f + 800.f * flRatio;
		flGravity = (0.5f - 0.4f * flRatio) * flGravity;
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 1.f, 1.f, 1.f }, flSpeed, flGravity, 10.f);
		return true;
	}
	case TF_WEAPON_CROSSBOW:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
	{
		bool bCrossbow = iWeaponID == TF_WEAPON_CROSSBOW;

		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 23.5f, -8.f, -3.f }, vPos, vAngle, bRedirect ? 2000.f : 0.f, 0.1f, bInterp, true, &m_tFireSetup);
		
		auto uType = bCrossbow ? FNV1A::Hash32Const("models/weapons/w_models/w_syringe_proj.mdl") : FNV1A::Hash32Const("models/weapons/w_models/w_repair_claw.mdl");
		Vec3 vHull = bCrossbow ? Vec3(3.f, 3.f, 3.f) : Vec3(1.f, 1.f, 1.f);
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, vHull, 2400.f, 0.2f * flGravity, 10.f);
		return true;
	}
	case TF_WEAPON_SYRINGEGUN_MEDIC:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 16.f, 6.f, -8.f }, vPos, vAngle, bRedirect ? 2000.f : 0.f, 0.1f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/weapons/w_models/w_syringe_proj.mdl");
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 1.f, 1.f, 1.f }, 1000.f, 0.3f * flGravity);
		return true;
	}
	case TF_WEAPON_FLAMETHROWER:
	{
		static auto tf_flamethrower_boxsize = H::ConVars.FindVar("tf_flamethrower_boxsize");
		const float flHull = tf_flamethrower_boxsize->GetFloat();

		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 40.f, 5.f, 0.f }, vPos, vAngle, 0.f, 0.f, bInterp, false, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("particles/flamethrower.pcf");
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { flHull, flHull, flHull }, 1000.f, 0.f, 0.285f);
		return true;
	}
	case TF_WEAPON_FLAME_BALL:
	{
		static auto tf_fireball_speed = H::ConVars.FindVar("tf_fireball_speed");
		static auto tf_fireball_distance = H::ConVars.FindVar("tf_fireball_distance");
		static auto tf_fireball_max_lifetime = H::ConVars.FindVar("tf_fireball_max_lifetime");

		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 3.f, 7.f, -9.f }, vPos, vAngle, bRedirect ? tf_fireball_distance->GetFloat() : 0.f, 1.f, bInterp, true, &m_tFireSetup);

		auto uType = FNV1A::Hash32Const("models/weapons/c_models/c_flameball/c_flameball.mdl");
		float flSpeed = tf_fireball_speed->GetFloat();
		float flLifeTime = std::min(tf_fireball_distance->GetFloat() / flSpeed, tf_fireball_max_lifetime->GetFloat());
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 1.f, 1.f, 1.f }, flSpeed, 0.f, flLifeTime);
		return true;
	}
	case TF_WEAPON_CLEAVER:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 16.f, 8.f, -6.f }, vPos, vAngle, 0.f, 0.f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl");
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 1.f, 1.f, 10.f }, 3000.f, DEFAULT_GRAVITY, 2.2f);
		return true;
	}
	case TF_WEAPON_BAT_WOOD:
	case TF_WEAPON_BAT_GIFTWRAP:
	{
		static auto tf_scout_stunball_base_speed = H::ConVars.FindVar("tf_scout_stunball_base_speed");
		const bool bWrapAssassin = iWeaponID == TF_WEAPON_BAT_GIFTWRAP;
		
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 0.f, 0.f, 0.f }, vPos, vAngle, 0.f, 0.f, bInterp, true, &m_tFireSetup);
		
		auto uType = bWrapAssassin ? FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl") : FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl");
		const float flScale = pPlayer->m_flModelScale();
		const Vec3 vOrigin = bInterp ? pPlayer->GetAbsOrigin() : pPlayer->m_vecOrigin();
		Vec3 vForward; Math::AngleVectors(vAngle, &vForward);
		vPos = vOrigin + (Vec3(0, 0, 50) + vForward * 32.f) * flScale;
		m_tFireSetup = { vOrigin + Vec3(0.f, 0.f, 50.f * flScale), { 32.f * flScale, 0.f, 0.f }, 0.f, 0.f, 0.f, true };
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 3.f, 3.f, 3.f }, tf_scout_stunball_base_speed->GetFloat(), DEFAULT_GRAVITY, bWrapAssassin ? 2.3f : 100.f);
		return true;
	}
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 16.f, 8.f, -6.f }, vPos, vAngle, 0.f, 0.f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl");
		switch (pWeapon->m_iItemDefinitionIndex())
		{
		case Scout_s_MadMilk: uType = FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"); break;
		case Sniper_s_TheSelfAwareBeautyMark: uType = FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"); break;
		case Scout_s_MutatedMilk: uType = FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"); break;
		}
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 3.f, 3.f, 3.f }, 1000.f, DEFAULT_GRAVITY, 2.2f);
		return true;
	}
	case TF_WEAPON_JAR_GAS:
	{
		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 16.f, 8.f, -6.f }, vPos, vAngle, 0.f, 0.f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl");
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 3.f, 3.f, 3.f }, 2000.f, DEFAULT_GRAVITY, 2.2f);
		return true;
	}
	case TF_WEAPON_GRAPPLINGHOOK:
	{
		static auto tf_grapplinghook_projectile_speed = H::ConVars.FindVar("tf_grapplinghook_projectile_speed");
		static auto tf_grapplinghook_max_distance = H::ConVars.FindVar("tf_grapplinghook_max_distance");

		SDK::GetProjectileFireSetup(pPlayer, vAngles, { 23.5f, -8.f, -3.f }, vPos, vAngle, bRedirect ? 2000.f : 0.f, 0.1f, bInterp, true, &m_tFireSetup);
		
		auto uType = FNV1A::Hash32Const("models/weapons/c_models/c_grapple_proj/c_grapple_proj.mdl");
		float flSpeed = tf_grapplinghook_projectile_speed->GetFloat();
		if (pPlayer->InCond(TF_COND_RUNE_AGILITY))
		{
			switch (pPlayer->m_iClass())
			{
			case TF_CLASS_SOLDIER:
			case TF_CLASS_HEAVY: flSpeed = 2600.f; break;
			default: flSpeed = 3000.f;
			}
		}
		float flLifeTime = tf_grapplinghook_max_distance->GetFloat() / flSpeed;
		tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 1.2f, 1.2f, 1.2f }, flSpeed, 0.f, flLifeTime);
		return true;
	}
	case TF_WEAPON_LUNCHBOX:
	{
		switch (pWeapon->m_iItemDefinitionIndex())
		{
		case Heavy_s_RoboSandvich:
		case Heavy_s_Sandvich:
		case Heavy_s_FestiveSandvich:
		case Heavy_s_Fishcake:
		case Heavy_s_TheDalokohsBar:
		case Heavy_s_SecondBanana:
		{
			SDK::GetProjectileFireSetup(pPlayer, vAngles, { 0.f, 0.f, -8.f }, vPos, vAngle, 0.f, 0.f, bInterp, true, &m_tFireSetup);

			auto uType = FNV1A::Hash32Const("models/weapons/c_models/c_sandwich/c_sandwich.mdl");
			tProjInfo.Set(pPlayer, pWeapon, uType, vPos, vAngle, { 17.f, 17.f, 7.f }, 500.f, 1.f * flGravity);
			return true;
		}
		}
		break;
	}
	case TF_WEAPON_PASSTIME_GUN:
	{
		static auto tf_passtime_throwspeed_velocity_scale = H::ConVars.FindVar("tf_passtime_throwspeed_velocity_scale");
		const auto pszSpeed = GetPasstimeThrowSpeedCvar(pPlayer);
		const auto pszArc = GetPasstimeThrowArcCvar(pPlayer);
		auto pThrowSpeed = H::ConVars.FindVar(pszSpeed);
		auto pThrowArc = H::ConVars.FindVar(pszArc);
		if (!pThrowSpeed || !pThrowArc)
			return false;

		Vec3 vForward = {};
		Math::AngleVectors(vAngles, &vForward);
		const float flArc = pThrowArc->GetFloat();
		Vec3 vLaunch = (vForward * (1.f - flArc) + Vec3(0.f, 0.f, flArc)).Normalized();

		vPos = bInterp ? pPlayer->GetEyePosition() : pPlayer->GetShootPos();
		vAngle = Math::VectorAngles(vLaunch);
		float flScale = tf_passtime_throwspeed_velocity_scale ? tf_passtime_throwspeed_velocity_scale->GetFloat() : 0.f;

		static auto tf_passtime_ball_sphere_radius = H::ConVars.FindVar("tf_passtime_ball_sphere_radius");
		static auto tf_passtime_ball_reset_time = H::ConVars.FindVar("tf_passtime_ball_reset_time");
		tProjInfo.Set(pPlayer, pWeapon, FNV1A::Hash32Const("models/passtime/ball/passtime_ball.mdl"), vPos, vAngle, Vec3::Get(tf_passtime_ball_sphere_radius->GetFloat()), pThrowSpeed->GetFloat(), flGravity, tf_passtime_ball_reset_time->GetFloat(), 0, vForward * (vForward.Dot(pPlayer->m_vecVelocity()) * flScale));
		return true;
	}
	}

	return false;
}

bool CProjectileSimulation::GetFireSetup(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon, int iFlags, FireSetup_t& tSetup)
{
	tSetup = {};
	if (!pWeapon || m_pSpecial
		|| !(iFlags & ProjSimEnum::NoRandomAngles) || iFlags & ProjSimEnum::CorrectRandomAngles)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_PASSTIME_GUN:
		return false;
	}

	ProjectileInfo tProjInfo = {};
	if (!GetInfo(pPlayer, pWeapon, {}, tProjInfo, iFlags & ~ProjSimEnum::InitCheck))
		return false;

	tSetup = m_tFireSetup;
	return tSetup.m_bValid;
}

bool CProjectileSimulation::GetInfo(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon, const Vec3& vAngles, ProjectileInfo& tProjInfo, int iFlags, float flAutoCharge)
{
	PROJ_ZONE(ProjGetInfo);

	bool bInitCheck = iFlags & ProjSimEnum::InitCheck;
	bool bInterp = iFlags & ProjSimEnum::Interp;
	bool bReturn = false;

	if (auto pLocal = bInterp ? H::Entities.GetLocal() : nullptr)
	{
		const float flOldCurrentTime = I::GlobalVars->curtime;
		I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());
		bReturn = GetInfoMain(pPlayer, pWeapon, vAngles, tProjInfo, iFlags, flAutoCharge);
		I::GlobalVars->curtime = flOldCurrentTime;
	}
	else
		bReturn = GetInfoMain(pPlayer, pWeapon, vAngles, tProjInfo, iFlags, flAutoCharge);
	tProjInfo.m_iFlags = iFlags;

	if (!bReturn || !bInitCheck)
		return bReturn;

	if (tProjInfo.m_uType == FNV1A::Hash32Const("models/buildables/sentry3_rockets.mdl"))
		return true;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter(pPlayer);

	Vec3 vStart = bInterp ? pPlayer->GetEyePosition() : pPlayer->GetShootPos();
	Vec3 vEnd = tProjInfo.m_vPos;

	SDK::TraceHull(vStart, vEnd, -tProjInfo.m_vHull, tProjInfo.m_vHull, MASK_SOLID, &filter, &trace);
	tProjInfo.m_vPos = trace.endpos;
	return !trace.allsolid;
}

void CProjectileSimulation::GetInfo(CBaseEntity* pProjectile, ProjectileInfo& tProjInfo)
{
	auto paEntities = GetEntities(pProjectile);
	Vec3 vVelocity = GetVelocity(pProjectile);

	tProjInfo.m_pOwner = paEntities.second;
	tProjInfo.m_pWeapon = paEntities.first;
	tProjInfo.m_uType = FNV1A::Hash32(I::ModelInfoClient->GetModelName(pProjectile->GetModel()));

	tProjInfo.m_vPos = pProjectile->m_vecOrigin();
	tProjInfo.m_vAng = Math::VectorAngles(vVelocity);
	tProjInfo.m_vHull = pProjectile->m_vecMaxs();

	tProjInfo.m_flVelocity = vVelocity.Length();
	tProjInfo.m_flGravity = GetGravity(pProjectile, tProjInfo.m_pWeapon);
	tProjInfo.m_vPath.clear();
}

bool CProjectileSimulation::Initialize(ProjectileInfo& tProjInfo, bool bSimulate, bool bWorld)
{
	PROJ_ZONE(ProjInit);

	const bool bIsSpecial = m_pSpecial && tProjInfo.m_uType == m_pSpecial->m_uType;

	float flDrag = 0.f;
	Vec3 vDragBasis = {};
	Vec3 vAngDragBasis = {};

	switch (tProjInfo.m_uType)
	{
	case FNV1A::Hash32Const("custom"):
		flDrag = Vars::Visuals::Trajectory::Drag.Value;
		vDragBasis = { Vars::Visuals::Trajectory::DragX.Value, Vars::Visuals::Trajectory::DragY.Value, Vars::Visuals::Trajectory::DragZ.Value };
		vAngDragBasis = { Vars::Visuals::Trajectory::AngularDragX.Value, Vars::Visuals::Trajectory::AngularDragY.Value, Vars::Visuals::Trajectory::AngularDragZ.Value };
		break;
	case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
	case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_quadball/w_quadball_grenade.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.003902f, 0.009962f, 0.009962f };
		vAngDragBasis = { 0.003618f, 0.001514f, 0.001514f };
		break;
	case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
	case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_kingmaker_sticky/w_kingmaker_stickybomb.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb_d.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.007491f, 0.007491f, 0.007306f };
		vAngDragBasis = { 0.002777f, 0.002842f, 0.002812f };
		break;
	case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb2.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.007394f, 0.007394f, 0.007100f };
		vAngDragBasis = { 0.002654f, 0.002717f, 0.002708f };
		break;
	case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.020971f, 0.019420f, 0.020971f };
		vAngDragBasis = { 0.012997f, 0.013496f, 0.013714f };
		break;
	case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.022287f, 0.005208f, 0.110697f };
		vAngDragBasis = { 0.013982f, 0.043243f, 0.003465f };
		break;
	case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.009000f, 0.006581f, 0.006710f };
		vAngDragBasis = { 0.002233f, 0.002246f, 0.002206f };
		break;
	case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.013500f, 0.0108727f, 0.010804f };
		vAngDragBasis = { 0.002081f, 0.002162f, 0.002069f };
		break;
	case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.005127f, 0.002925f, 0.004337f };
		vAngDragBasis = { 0.000641f, 0.001350f, 0.000717f };
		break;
	case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.005514f, 0.002313f, 0.005558f };
		vAngDragBasis = { 0.000684f, 0.001439f, 0.000680f };
		break;
	case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.002208f, 0.001640f, 0.002187f };
		vAngDragBasis = { 0.000799f, 0.001515f, 0.000879f };
		break;
	case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.002335f, 0.001960f, 0.001920f };
		vAngDragBasis = { 0.000672f, 0.001064f, 0.000747f };
		break;
	case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
		flDrag = 1.f;
		vDragBasis = { 0.026360f, 0.021780f, 0.058978f };
		vAngDragBasis = { 0.035050f, 0.031199f, 0.022922f };
	}

	Vec3 vVelocity, vAngularVelocity;
	if (!bWorld)
	{
		Vec3 vAngle = tProjInfo.m_vAng;
		if (tProjInfo.m_uType == FNV1A::Hash32Const("models/weapons/c_models/c_sandwich/c_sandwich.mdl"))
			vAngle -= Vec3(10.f, 0.f, 0.f);

		Vec3 vForward, vRight, vUp; Math::AngleVectors(vAngle, &vForward, &vRight, &vUp);
		vVelocity = vForward * tProjInfo.m_flVelocity;

		switch (tProjInfo.m_uType)
		{
		case FNV1A::Hash32Const("custom"):
			vVelocity += vUp * Vars::Visuals::Trajectory::UpVelocity.Value;
			vAngularVelocity = { Vars::Visuals::Trajectory::AngularVelocityX.Value, Vars::Visuals::Trajectory::AngularVelocityY.Value, Vars::Visuals::Trajectory::AngularVelocityZ.Value };
			break;
		case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
		case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_quadball/w_quadball_grenade.mdl"):
		case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
		case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb2.mdl"):
		case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
			vVelocity += vUp * 200.f;
			if (!(tProjInfo.m_iFlags & ProjSimEnum::Interp) && G::CurrentUserCmd)
			{
				const bool bNoSpin = tProjInfo.m_pWeapon && SDK::AttribHookValue(0, "grenade_no_spin", tProjInfo.m_pWeapon);
				float flUpRand, flRightRand;
				int iAngYawRand;
				if (m_tSpreadCache.m_bHasGrenadeRand
					&& m_tSpreadCache.m_iTick == I::GlobalVars->tickcount
					&& m_tSpreadCache.m_pPlayer == tProjInfo.m_pOwner
					&& m_tSpreadCache.m_pWeapon == tProjInfo.m_pWeapon)
				{
					flUpRand = m_tSpreadCache.m_flUpRand;
					flRightRand = m_tSpreadCache.m_flRightRand;
					iAngYawRand = m_tSpreadCache.m_iAngYawRand;
				}
				else
				{
					flUpRand = SDK::RandomFloat(-10.f, 10.f);
					flRightRand = SDK::RandomFloat(-10.f, 10.f);
					iAngYawRand = bNoSpin ? 0 : SDK::RandomInt(-1200, 1200);
				}
				Vec3 vNewVelocity = vVelocity + vUp * flUpRand + vRight * flRightRand;
				if (!(tProjInfo.m_iFlags & ProjSimEnum::NoRandomAngles))
					vVelocity = vNewVelocity;
				else
					vVelocity = vVelocity.Normalized() * vNewVelocity.Length();
				if (!bNoSpin)
					vAngularVelocity = { 600.f, float(iAngYawRand), 0.f };
				break;
			}
			if (!tProjInfo.m_pWeapon || !SDK::AttribHookValue(0, "grenade_no_spin", tProjInfo.m_pWeapon))
				vAngularVelocity = { 600.f, -1200.f, 0.f };
			break;
		case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
			vVelocity = vForward * 10 + vUp;
			vVelocity.Normalize();
			vVelocity *= tProjInfo.m_flVelocity;
			vAngularVelocity = { 0.f, 500.f, 0.f };
			break;
		case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
		case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
			vVelocity = vForward * 10 + vUp;
			vVelocity.Normalize();
			vVelocity *= tProjInfo.m_flVelocity;
			vAngularVelocity = { 0.f, 100.f, 0.f };
			break;
		case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
		case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
		case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
		case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
		case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
			vVelocity += vUp * 200.f;
			vAngularVelocity = { 300.f, 0.f, 0.f };
			break;
		case FNV1A::Hash32Const("particles/flamethrower.pcf"):
			if (bSimulate && tProjInfo.m_pOwner)
			{
				Vec3 vOwnerVelocity = tProjInfo.m_pOwner->m_vecVelocity();
				if (!vOwnerVelocity.IsZero())
				{
					float flOwnerVelocity = vOwnerVelocity.Length();

					float flDot = vForward.Dot(vOwnerVelocity) / flOwnerVelocity;
					vVelocity = vForward * (tProjInfo.m_flVelocity + flOwnerVelocity * flDot);
				}
			}
		}
		vVelocity += tProjInfo.m_vVelocityAdd;
	}
	else
	{
		Vec3 vForward, vRight, vUp; Math::AngleVectors(tProjInfo.m_vAng, &vForward, &vRight, &vUp);
		vVelocity = vForward * tProjInfo.m_flVelocity;

		switch (tProjInfo.m_uType)
		{
		case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
		case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_quadball/w_quadball_grenade.mdl"):
		case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
		case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb2.mdl"):
		case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
			if (!tProjInfo.m_pWeapon || !SDK::AttribHookValue(0, "grenade_no_spin", tProjInfo.m_pWeapon))
				vAngularVelocity = { 600.f, -1200.f, 0.f };
			break;
		case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
			vAngularVelocity = { 0.f, 500.f, 0.f };
			break;
		case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
		case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
			vAngularVelocity = { 0.f, 100.f, 0.f };
			break;
		case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
		case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
		case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
		case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
		case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
			vAngularVelocity = { 300.f, 0.f, 0.f };
			break;
		}
	}

	if (bSimulate && !bIsSpecial && (!flDrag || !vDragBasis))
	{
		m_bPhysics = false;
		m_bAngDrag = false;
		m_bVPhysics = false;
		m_pCurrent = &tProjInfo;
		vVelocity.z += tProjInfo.m_flGravity * TICK_INTERVAL / 2;
		m_tObj.m_vOrigin = tProjInfo.m_vPos;
		m_tObj.m_vVelocity = vVelocity;
		if (tProjInfo.m_uType == FNV1A::Hash32Const("custom"))
			tProjInfo.m_uType = FNV1A::Hash32(Vars::Visuals::Trajectory::Type.Value.c_str());
		return true;
	}

	float flMaxVelocity = 1000000.f;
	float vMaxAngularVelocity = 1000000.f;

	switch (tProjInfo.m_uType)
	{
	case FNV1A::Hash32Const("custom"):
		if (Vars::Visuals::Trajectory::MaxVelocity.Value)
			flMaxVelocity = Vars::Visuals::Trajectory::MaxVelocity.Value;
		if (Vars::Visuals::Trajectory::MaxAngularVelocity.Value)
			vMaxAngularVelocity = Vars::Visuals::Trajectory::MaxAngularVelocity.Value;
		break;
	case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
	case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_quadball/w_quadball_grenade.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb2.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
	case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
	case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
	case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
		flMaxVelocity = k_flMaxVelocity;
		vMaxAngularVelocity = k_flMaxAngularVelocity;
	}
	if (bIsSpecial)
	{
		flMaxVelocity = k_flMaxVelocity;
		vMaxAngularVelocity = k_flMaxAngularVelocity;
	}

	if (bSimulate && !bIsSpecial)
	{
		m_pCurrent = &tProjInfo;
		m_bPhysics = flDrag && vDragBasis;
		m_bAngDrag = flDrag && vAngDragBasis;
		m_bVPhysics = false;
		InitPhysicsState(m_tObj, tProjInfo.m_vPos, tProjInfo.m_vAng, vVelocity, vAngularVelocity, tProjInfo.m_flGravity, flDrag, vDragBasis, vAngDragBasis, flMaxVelocity, vMaxAngularVelocity);
		if (tProjInfo.m_uType == FNV1A::Hash32Const("custom"))
			tProjInfo.m_uType = FNV1A::Hash32(Vars::Visuals::Trajectory::Type.Value.c_str());
		return true;
	}

	if (!m_pEnv)
	{
		m_pEnv = I::Physics->CreateEnvironment();
		if (m_pEnv)
			m_pEnv->SetAirDensity(AIR_DENSITY);
	}
	if (!m_pEnv)
		return false;

	if (!m_pObjBox)
	{
		CPhysCollide* pCollide = I::PhysicsCollision->BBoxToCollide({ -2.f, -2.f, -2.f }, { 2.f, 2.f, 2.f });
		objectparams_t tParams = m_tPhysDefaultObjectParams;
		tParams.damping = 0.f;
		tParams.rotdamping = 0.f;
		tParams.inertia = 0.f;
		tParams.rotInertiaLimit = 0.f;
		tParams.enableCollisions = false;

		m_pObjBox = m_pEnv->CreatePolyObject(pCollide, 0, tProjInfo.m_vPos, tProjInfo.m_vAng, &tParams);
	}

	bool bSpecialObject = false;
	m_pObj = m_pObjBox;
	if (bIsSpecial)
	{
		if (CreateSpecialObject(m_pEnv, m_pSpecial, m_tPhysDefaultObjectParams, m_pSpecialCollide, m_pObjSpecial))
			m_pObj = m_pObjSpecial, bSpecialObject = true;
	}

	if (!m_pObj)
		return false;

	if (m_pObj != m_pLastObj)
	{
		if (m_pObjBox && m_pObjBox != m_pObj)
			m_pObjBox->Sleep();
		if (m_pObjSpecial && m_pObjSpecial != m_pObj)
			m_pObjSpecial->Sleep();
		m_pObj->Wake();
		m_pLastObj = m_pObj;
	}

	m_pCurrent = &tProjInfo;

	if (bSpecialObject)
		flDrag = m_tPhysDefaultObjectParams.dragCoefficient;

	if (flDrag != m_pObj->m_dragCoefficient || !bSimulate)
		m_pObj->SetDragCoefficient(&flDrag, &flDrag);

	if (!bSpecialObject)
	{
		m_pObj->m_dragBasis = vDragBasis;
		m_pObj->m_angDragBasis = vAngDragBasis;
	}
	m_bPhysics = m_pObj->m_dragCoefficient && m_pObj->m_dragBasis;
	m_bAngDrag = m_pObj->m_dragCoefficient && m_pObj->m_angDragBasis;
	m_bVPhysics = m_bPhysics && bSpecialObject;

	if (bSimulate && !m_bPhysics)
		vVelocity.z += tProjInfo.m_flGravity * TICK_INTERVAL / 2;

	if (m_bPhysics || !bSimulate)
	{
		m_pObj->SetPosition(tProjInfo.m_vPos, bSpecialObject ? QAngle() : tProjInfo.m_vAng, true);
		m_pObj->SetVelocity(&vVelocity, &vAngularVelocity);
	}
	m_tObj.m_vOrigin = tProjInfo.m_vPos;
	if (m_bPhysics && !bSpecialObject)
		InitPhysicsState(m_tObj, tProjInfo.m_vPos, tProjInfo.m_vAng, vVelocity, vAngularVelocity, tProjInfo.m_flGravity, flDrag, vDragBasis, vAngDragBasis, flMaxVelocity, vMaxAngularVelocity);
	if (!m_bPhysics)
	{
		m_tObj.m_vVelocity = vVelocity;
		if (bIsSpecial && vVelocity.Length() > k_flMaxVelocity)
			m_tObj.m_vVelocity = vVelocity.Normalized() * k_flMaxVelocity;
	}

	{
		if (flMaxVelocity != m_flLastMaxVel || vMaxAngularVelocity != m_flLastMaxAngVel || !bSimulate)
		{
			physics_performanceparams_t params = {}; params.Defaults();
			params.maxVelocity = flMaxVelocity;
			params.maxAngularVelocity = vMaxAngularVelocity;
			m_pEnv->SetPerformanceSettings(&params);
			m_flLastMaxVel = flMaxVelocity;
			m_flLastMaxAngVel = vMaxAngularVelocity;
		}
		if (tProjInfo.m_flGravity != m_flLastGravity || !bSimulate)
		{
			m_pEnv->SetGravity({ 0.f, 0.f, -tProjInfo.m_flGravity });
			m_flLastGravity = tProjInfo.m_flGravity;
		}
		if (m_bPhysics || !bSimulate)
			m_pEnv->ResetSimulationClock();
	}

	if (tProjInfo.m_uType == FNV1A::Hash32Const("custom"))
		tProjInfo.m_uType = FNV1A::Hash32(Vars::Visuals::Trajectory::Type.Value.c_str());

	return true;
}

void CProjectileSimulation::RunTick(ProjectileInfo& tProjInfo, bool bPath)
{
	PROJ_ZONE(ProjTick);
	if (m_bPhysics)
		PROJ_COUNT(ProjTicksPhysics);
	else
		PROJ_COUNT(ProjTicksSimple);

	if (bPath)
		tProjInfo.m_vPath.push_back(m_tObj.m_vOrigin);

	if (m_bPhysics)
	{
		if (m_bVPhysics)
		{
			m_pEnv->Simulate(TICK_INTERVAL);
			m_pObj->GetPosition(&m_tObj.m_vOrigin, nullptr);
		}
		else
			SimulatePhysicsTick(m_tObj, TICK_INTERVAL);
	}
	else
	{
		Vec3& vOrigin = m_tObj.m_vOrigin;
		Vec3& vVelocity = m_tObj.m_vVelocity;

		if (tProjInfo.m_flGravity)
			vVelocity.z -= tProjInfo.m_flGravity * TICK_INTERVAL;

		vOrigin += vVelocity * TICK_INTERVAL;
	}
}

Vec3 CProjectileSimulation::GetOrigin()
{
	return m_tObj.m_vOrigin;
}

Vec3 CProjectileSimulation::GetVelocity()
{
	if (!m_bPhysics || !m_bVPhysics)
		return m_tObj.m_vVelocity;

	if (!m_pObj)
		return {};

	Vec3 vOut; m_pObj->GetVelocity(&vOut, nullptr);
	return vOut;
}

float CProjectileSimulation::GetDesync()
{
	if (I::EngineClient->IsPlayingDemo())
		return 0.f;

	return I::GlobalVars->curtime - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick) - F::Backtrack.GetReal();
}

void CProjectileSimulation::SetupTrace(CTraceFilterCollideable& filter, int& nMask, CTFWeaponBase* pWeapon, int nTick, bool bInterp)
{
	switch (nTick)
	{
	case 0:
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_RAYGUN:
			filter.m_iObject = OBJECT_DEFAULT;
			break;
		case TF_WEAPON_FLAMETHROWER:
		case TF_WEAPON_FLAME_BALL:
			nMask |= CONTENTS_WATER;
			break;
		case TF_WEAPON_DRG_POMSON:
		case TF_WEAPON_SYRINGEGUN_MEDIC:
		case TF_WEAPON_BAT_GIFTWRAP:
			if (bInterp)
				filter.m_iPlayer = PLAYER_ALL;
		}
		break;
	case 16:
		if (bInterp)
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_GRAPPLINGHOOK:
				break;
			default:
				filter.m_iPlayer = PLAYER_ALL;
			}
		}
	}

	if (m_bAngDrag)
		nMask |= CONTENTS_DISPSOLID;
}

void CProjectileSimulation::SetupTrace(CTraceFilterCollideable& filter, int& nMask, CBaseEntity* pProjectile)
{
	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFProjectile_EnergyRing:
		filter.m_iObject = OBJECT_DEFAULT;
		break;
	case ETFClassID::CTFProjectile_BallOfFire:
		nMask |= CONTENTS_WATER;
	}
}
