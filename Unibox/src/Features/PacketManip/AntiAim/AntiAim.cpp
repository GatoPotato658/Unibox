#include "AntiAim.h"

#include "../../Ticks/Ticks.h"
#include "../../Players/PlayerUtils.h"
#include "../../Misc/Misc.h"
#include "../../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"

bool CAntiAim::AntiAimOn()
{
	return Vars::AntiAim::Enabled.Value
		&& (Vars::AntiAim::PitchReal.Value
		|| Vars::AntiAim::PitchFake.Value
		|| Vars::AntiAim::YawReal.Value
		|| Vars::AntiAim::YawFake.Value
		|| Vars::AntiAim::RealYawBase.Value
		|| Vars::AntiAim::FakeYawBase.Value
		|| Vars::AntiAim::RealYawOffset.Value
		|| Vars::AntiAim::FakeYawOffset.Value);
}

bool CAntiAim::YawOn()
{
	return Vars::AntiAim::Enabled.Value
		&& (Vars::AntiAim::YawReal.Value
		|| Vars::AntiAim::YawFake.Value
		|| Vars::AntiAim::RealYawBase.Value
		|| Vars::AntiAim::FakeYawBase.Value
		|| Vars::AntiAim::RealYawOffset.Value
		|| Vars::AntiAim::FakeYawOffset.Value);
}

bool CAntiAim::ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal->IsAlive() || pLocal->IsAGhost() || (pLocal->IsTaunting() && !Vars::AntiAim::TauntSpin.Value) || pLocal->m_MoveType() != MOVETYPE_WALK && !(pLocal->IsTaunting() && Vars::AntiAim::TauntSpin.Value) || pLocal->InCond(TF_COND_HALLOWEEN_KART)
		|| G::Attacking == 1 || F::AutoRocketJump.IsRunning() || F::Ticks.m_bDoubletap // this m_bDoubletap check can probably be removed if we fix tickbase correctly
		|| pWeapon && pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka && pCmd->buttons & IN_ATTACK && !(G::LastUserCmd->buttons & IN_ATTACK))
		return false;

	if (pLocal->InCond(TF_COND_SHIELD_CHARGE) || pCmd->buttons & IN_ATTACK2 && pLocal->m_bShieldEquipped() && pLocal->m_flChargeMeter() == 100.f)
		return false;

	return true;
}

void CAntiAim::ResetState()
{
	m_mJitter.clear();
	m_flUltraNextChange[0] = m_flUltraNextChange[1] = 0.f;
	m_flUltraYawOffset[0] = m_flUltraYawOffset[1] = 0.f;
	m_bUltraSpin[0] = m_bUltraSpin[1] = false;
	m_flUltraSpinSpeed[0] = m_flUltraSpinSpeed[1] = 0.f;
	m_bSideways = false;
	m_flOmegaYaw = 0.f;
	m_flTornadoYaw[0] = m_flTornadoYaw[1] = 0.f;
	m_flTornadoSpeed[0] = m_flTornadoSpeed[1] = 0.f;
	m_iTornadoRetune[0] = m_iTornadoRetune[1] = 0;
	m_flHelixPhase[0] = m_flHelixPhase[1] = 0.f;
	m_iQuantumShift[0] = m_iQuantumShift[1] = 0;
	m_flQuantumYaw[0] = m_flQuantumYaw[1] = 0.f;
	m_flPitchUltraNext = 0.f;
	m_flPitchUltra = 0.f;
	m_flPitchUltraNextFake = 0.f;
	m_flPitchUltraFake = 0.f;
	m_iMoonwalkNext = 0;
	m_flMoonwalkPitch = 0.f;
	m_bTimedFlipUp = false;
	m_flTimedFlipNext = 0.f;
	m_bTimedFlipRandUp = false;
	m_flTimedFlipRandNext = 0.f;
	m_bMinWalkVar = true;
}

bool CAntiAim::CheckAndResetTime()
{
	if (!I::GlobalVars)
		return false;
	float flCurTime = I::GlobalVars->curtime;
	int iTick = I::GlobalVars->tickcount;
	bool bReset = false;
	if (m_flLastCurTime > 0.f && flCurTime < m_flLastCurTime - 0.5f)
		bReset = true;
	if (m_iLastTick > 0 && iTick < m_iLastTick - 5)
		bReset = true;
	if (flCurTime > m_flTimedFlipNext + 100.f)
		bReset = true;
	if (flCurTime > m_flPitchUltraNext + 100.f && m_flPitchUltraNext > 0.f)
		bReset = true;
	for (int i = 0; i < 2; i++)
	{
		if (m_flUltraNextChange[i] > 0.f && flCurTime > m_flUltraNextChange[i] + 100.f)
			bReset = true;
		if (m_iTornadoRetune[i] > 0 && iTick > m_iTornadoRetune[i] + 1000)
			bReset = true;
		if (m_iQuantumShift[i] > 0 && iTick > m_iQuantumShift[i] + 1000)
			bReset = true;
	}
	m_flLastCurTime = flCurTime;
	m_iLastTick = iTick;
	if (bReset)
	{
		ResetState();
		return true;
	}
	return false;
}

void CAntiAim::OnLevelInit()
{
	ResetState();
	m_flLastCurTime = 0.f;
	m_iLastTick = 0;
	vRealAngles = vFakeAngles = {};
	vEdgeTrace.clear();
	m_bShownValid = m_bShownBones = m_bAutoValid = false;
	m_iPlannedChoke = m_iLastChoked = 0;
}



void CAntiAim::FakeShotAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::AntiAim::HidePitchOnShot.Value || G::Attacking != 1 || G::PrimaryWeaponType != EWeaponType::HITSCAN || pLocal->m_MoveType() != MOVETYPE_WALK)
		return;

	switch (pWeapon ? pWeapon->GetWeaponID() : 0)
	{
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_LASER_POINTER:
		return;
	}

	G::SilentAngles = true;
	if (!Vars::Aimbot::General::NoSpread.Value)
	{	// messes with nospread accuracy
		pCmd->viewangles.x = 180 - pCmd->viewangles.x;
		pCmd->viewangles.y += 180;
	}
	else
		pCmd->viewangles.x += 360 * (vFakeAngles.x < 0 ? -1 : 1);
}

static inline float EdgeDistance(CTFPlayer* pEntity, float flYaw, float flOffset)
{
	Vec3 vForward, vRight; Math::AngleVectors({ 0, flYaw, 0 }, &vForward, &vRight, nullptr);
	Vec3 vCenter = pEntity->GetCenter();

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};
	SDK::Trace(vCenter, vCenter + vRight * flOffset, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	F::AntiAim.vEdgeTrace.emplace_back(trace.startpos, trace.endpos);
	SDK::Trace(trace.endpos, trace.endpos + vForward * 300.f, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	F::AntiAim.vEdgeTrace.emplace_back(trace.startpos, trace.endpos);

	return trace.fraction;
}

static inline int GetEdge(CTFPlayer* pEntity, const float flYaw)
{
	float flSize = pEntity->GetSize().y;
	float flEdgeLeftDist = EdgeDistance(pEntity, flYaw, -flSize);
	float flEdgeRightDist = EdgeDistance(pEntity, flYaw, flSize);

	return flEdgeLeftDist > flEdgeRightDist ? -1 : 1;
}

int CAntiAim::GetJitter(uint32_t uHash)
{
	if (!I::ClientState->chokedcommands)
		m_mJitter[uHash] = !m_mJitter[uHash];
	auto it = m_mJitter.find(uHash);
	return (it != m_mJitter.end() && it->second) ? 1 : -1;
}

float CAntiAim::GetYawOffset(CTFPlayer* pEntity, bool bFake)
{
	const int iMode = bFake ? Vars::AntiAim::YawFake.Value : Vars::AntiAim::YawReal.Value;
	int iJitter = GetJitter(FNV1A::Hash32Const("Yaw"));

	switch (iMode)
	{
	case Vars::AntiAim::YawEnum::Forward: return 0.f;
	case Vars::AntiAim::YawEnum::Left: return 90.f;
	case Vars::AntiAim::YawEnum::Right: return -90.f;
	case Vars::AntiAim::YawEnum::Backwards: return 180.f;
	case Vars::AntiAim::YawEnum::Edge: return (bFake ? Vars::AntiAim::FakeYawValue.Value : Vars::AntiAim::RealYawValue.Value) * GetEdge(pEntity, I::EngineClient->GetViewAngles().y);
	case Vars::AntiAim::YawEnum::Jitter: return (bFake ? Vars::AntiAim::FakeYawValue.Value : Vars::AntiAim::RealYawValue.Value) * iJitter;
	case Vars::AntiAim::YawEnum::Spin: return fmod(I::GlobalVars->tickcount * Vars::AntiAim::SpinSpeed.Value + 180.f, 360.f) - 180.f;
	case Vars::AntiAim::YawEnum::Random: return SDK::RandomFloat(-180.f, 180.f);
	case Vars::AntiAim::YawEnum::Wiggle: return (sin(I::GlobalVars->tickcount * Vars::AntiAim::SpinSpeed.Value * 0.1f) * 90.f);
	case Vars::AntiAim::YawEnum::Mercedes:
	{
		int iStep = I::GlobalVars->tickcount % 3;
		return (iStep == 1 ? 120.f : (iStep == 2 ? -120.f : 0.f));
	}
	case Vars::AntiAim::YawEnum::Star:
	{
		int iStep = I::GlobalVars->tickcount % 5;
		return (iStep == 1 ? 72.f : (iStep == 2 ? 144.f : (iStep == 3 ? -144.f : (iStep == 4 ? -72.f : 0.f))));
	}
	case Vars::AntiAim::YawEnum::UltraRandom:
	{
		int i = bFake ? 1 : 0;
		float flCurTime = I::GlobalVars->curtime;

		if (flCurTime > m_flUltraNextChange[i])
		{
			m_flUltraNextChange[i] = flCurTime + SDK::RandomFloat(0.5f, 5.f);
			m_bUltraSpin[i] = SDK::RandomInt(0, 1);
			if (m_bUltraSpin[i])
				m_flUltraSpinSpeed[i] = SDK::RandomFloat(-30.f, 30.f);
			else
				m_flUltraYawOffset[i] = SDK::RandomFloat(-180.f, 180.f);
		}

		if (m_bUltraSpin[i])
			return fmod(I::GlobalVars->tickcount * m_flUltraSpinSpeed[i] + 180.f, 360.f) - 180.f;
		else
			return m_flUltraYawOffset[i];
	}
	case Vars::AntiAim::YawEnum::Sideways:
	{
		if (bFake)
			m_bSideways = !m_bSideways;
		return m_bSideways ? 90.f : -90.f;
	}
	case Vars::AntiAim::YawEnum::Omega:
	{
		if (bFake)
		{
			m_flOmegaYaw = Math::NormalizeAngle(m_flOmegaYaw + SDK::RandomFloat(-30.f, 30.f));
			return m_flOmegaYaw;
		}
		return Math::NormalizeAngle(m_flOmegaYaw - 180.f + SDK::RandomFloat(-40.f, 40.f));
	}
	case Vars::AntiAim::YawEnum::RandomUnclamped: return SDK::RandomFloat(-65536.f, 65536.f);
	case Vars::AntiAim::YawEnum::Heck: return SDK::RandomFloat(-359999.97f, 359999.97f);
	case Vars::AntiAim::YawEnum::Tornado:
	{
		const int i = bFake ? 1 : 0;
		const int iTick = I::GlobalVars->tickcount;
		if (iTick >= m_iTornadoRetune[i] || !m_flTornadoSpeed[i])
		{
			m_iTornadoRetune[i] = iTick + SDK::RandomInt(8, 24);
			const float flBaseSpeed = fmaxf(5.f, fabsf(Vars::AntiAim::SpinSpeed.Value));
			m_flTornadoSpeed[i] = SDK::RandomFloat(flBaseSpeed, flBaseSpeed * 3.f) * (SDK::RandomInt(0, 1) ? 1.f : -1.f);
		}

		m_flTornadoYaw[i] = Math::NormalizeAngle(m_flTornadoYaw[i] + m_flTornadoSpeed[i]);
		return Math::NormalizeAngle(m_flTornadoYaw[i] + sinf(iTick * 0.28f + i * 0.7f) * 35.f);
	}
	case Vars::AntiAim::YawEnum::Pulse:
	{
		float flBase = 0.f;
		switch ((I::GlobalVars->tickcount / 6 + (bFake ? 1 : 0)) % 4)
		{
		case 0: flBase = 0.f; break;
		case 1: flBase = 180.f; break;
		case 2: flBase = 90.f; break;
		default: flBase = -90.f; break;
		}
		return Math::NormalizeAngle(flBase + SDK::RandomFloat(-15.f, 15.f));
	}
	case Vars::AntiAim::YawEnum::Helix:
	{
		const int i = bFake ? 1 : 0;
		const float flStep = fmaxf(0.01f, fabsf(Vars::AntiAim::SpinSpeed.Value) * 0.006f);
		m_flHelixPhase[i] += flStep + (bFake ? 0.07f : 0.05f);

		const float flYaw = sinf(m_flHelixPhase[i] * 2.3f) * 125.f + cosf(m_flHelixPhase[i] * 1.1f) * 35.f;
		return Math::NormalizeAngle(flYaw);
	}
	case Vars::AntiAim::YawEnum::Auto: return bFake ? 0.f : 180.f;
	case Vars::AntiAim::YawEnum::Quantum:
	{
		static constexpr float arrQuantumAngles[8] = { -180.f, -135.f, -90.f, -45.f, 0.f, 45.f, 90.f, 135.f };

		const int i = bFake ? 1 : 0;
		const int iTick = I::GlobalVars->tickcount;
		if (iTick >= m_iQuantumShift[i])
		{
			m_iQuantumShift[i] = iTick + SDK::RandomInt(2, 7);
			m_flQuantumYaw[i] = arrQuantumAngles[SDK::RandomInt(0, 7)];
		}

		return Math::NormalizeAngle(m_flQuantumYaw[i] + SDK::RandomFloat(-25.f, 25.f));
	}
	}
	return 0.f;
}

float CAntiAim::GetBaseYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	const int iMode = bFake ? Vars::AntiAim::FakeYawBase.Value : Vars::AntiAim::RealYawBase.Value;
	const float flOffset = bFake ? Vars::AntiAim::FakeYawOffset.Value : Vars::AntiAim::RealYawOffset.Value;
	switch (iMode) // 0 offset, 1 at player
	{
	case Vars::AntiAim::YawModeEnum::View: return pCmd->viewangles.y + flOffset;
	case Vars::AntiAim::YawModeEnum::Target:
	{
		float flSmallestAngleTo = 0.f; float flSmallestFovTo = 360.f;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() || F::PlayerUtils.IsIgnored(pPlayer->entindex()))
				continue;
			
			const Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), pPlayer->m_vecOrigin());
			const float flFOVTo = Math::CalcFov(I::EngineClient->GetViewAngles(), vAngleTo);

			if (flFOVTo < flSmallestFovTo)
			{
				flSmallestAngleTo = vAngleTo.y;
				flSmallestFovTo = flFOVTo;
			}
		}
		return (flSmallestFovTo == 360.f ? pCmd->viewangles.y + flOffset : flSmallestAngleTo + flOffset);
	}
	}
	return pCmd->viewangles.y;
}

float CAntiAim::GetYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	if (m_bAutoValid && (bFake ? Vars::AntiAim::YawFake.Value : Vars::AntiAim::YawReal.Value) == Vars::AntiAim::YawEnum::Auto)
		return bFake ? m_flAutoFake : m_flAutoReal;

	float flYaw = GetBaseYaw(pLocal, pCmd, bFake) + GetYawOffset(pLocal, bFake);
	return flYaw;
}

static inline float BinYaw(int iBin)
{
	return float(iBin) * (360.f / CAntiAim::AutoYawBins) - 180.f;
}

static inline int YawBin(float flYaw)
{
	return int((Math::NormalizeAngle(flYaw) + 180.f) / (360.f / CAntiAim::AutoYawBins) + 0.5f) % CAntiAim::AutoYawBins;
}

static inline Vec3 SpinAround(const Vec3& vPoint, const Vec3& vAxis, float flDegrees)
{
	const float flSin = sinf(Math::Deg2Rad(flDegrees)), flCos = cosf(Math::Deg2Rad(flDegrees));
	const float flX = vPoint.x - vAxis.x, flY = vPoint.y - vAxis.y;
	return { vAxis.x + flX * flCos - flY * flSin, vAxis.y + flX * flSin + flY * flCos, vPoint.z };
}

static inline float TorsoTwist(const mstudioposeparamdesc_t* pAimYaw, float flEyeYaw, float flFeetYaw)
{
	return -std::clamp(-Math::NormalizeAngle(flEyeYaw - flFeetYaw), pAimYaw->start, pAimYaw->end);
}

static inline void StepFeetYaw(float& flGoalFeet, float& flCurrentFeet, float flEyeYaw, bool bMoving)
{
	if (bMoving)
		flGoalFeet = flEyeYaw;
	else
	{
		const float flDelta = Math::NormalizeAngle(flGoalFeet - flEyeYaw);
		if (fabsf(flDelta) > 45.f)
			flGoalFeet += flDelta > 0.f ? -45.f : 45.f;
	}

	flGoalFeet = Math::NormalizeAngle(flGoalFeet);
	if (flGoalFeet == flCurrentFeet)
		return;

	const float flRaw = flGoalFeet - flCurrentFeet;
	const float flStep = 720.f * TICK_INTERVAL * std::clamp(fabsf(flRaw) / 60.f, 0.01f, 1.f);
	if (fabsf(flRaw) < flStep)
	{
		flCurrentFeet = flGoalFeet;
		return;
	}

	flCurrentFeet = Math::NormalizeAngle(flCurrentFeet + (Math::NormalizeAngle(flRaw) < 0.f ? -flStep : flStep));
}

static inline int FirstHitbox(const mstudiohitboxset_t* pSet, const matrix3x4* aBones, const Vec3& vEye, const Vec3& vPoint)
{
	int iFirst = -1;
	float flNearest = std::numeric_limits<float>::max();
	for (int i = 0; i < pSet->numhitboxes; i++)
	{
		auto pBox = pSet->pHitbox(i);
		float flDist = 0.f;
		if (Math::RayToOBB(vEye, vPoint - vEye, pBox->bbmin, pBox->bbmax, aBones[pBox->bone], 1.f, &flDist) && flDist < flNearest)
		{
			flNearest = flDist;
			iFirst = i;
		}
	}
	return iFirst;
}

static inline void GetHeadPoints(const mstudiobbox_t* pHead, const matrix3x4* aBones, Vec3* aPoints)
{
	const Vec3 vCenter = (pHead->bbmin + pHead->bbmax) * 0.5f;
	const Vec3 vQuarter = (pHead->bbmax - pHead->bbmin) * 0.25f;
	Math::VectorTransform(vCenter, aBones[pHead->bone], aPoints[0]);
	for (int i = 0; i < CAntiAim::HeadPoints - 1; i++)
	{
		const Vec3 vOffset = { i & 1 ? vQuarter.x : -vQuarter.x, i & 2 ? vQuarter.y : -vQuarter.y, i & 4 ? vQuarter.z : -vQuarter.z };
		Math::VectorTransform(vCenter + vOffset, aBones[pHead->bone], aPoints[i + 1]);
	}
}

static inline bool IsHeadshotWeapon(CTFWeaponBase* pWeapon)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_COMPOUND_BOW:
		return true;
	case TF_WEAPON_REVOLVER:
		return pWeapon->m_iItemDefinitionIndex() == Spy_m_TheAmbassador || pWeapon->m_iItemDefinitionIndex() == Spy_m_FestiveAmbassador;
	}
	return false;
}

static inline bool IsBetterYaw(const CAntiAim::YawScore_t& tScore, const CAntiAim::YawScore_t& tBest)
{
	if (tScore.m_iRealExposed != tBest.m_iRealExposed)
		return tScore.m_iRealExposed < tBest.m_iRealExposed;
	if (tScore.m_iOverlap != tBest.m_iOverlap)
		return tScore.m_iOverlap < tBest.m_iOverlap;
	return tScore.m_iFakeExposed > tBest.m_iFakeExposed;
}

void CAntiAim::StepShown(CTFPlayer* pLocal, const Vec3& vSent, int iSteps)
{
	auto pAnimState = pLocal->m_PlayerAnimState();
	if (!pAnimState)
	{
		m_bShownValid = m_bShownBones = false;
		return;
	}

	char aLive[sizeof(CTFPlayerAnimState)];
	memcpy(aLive, pAnimState, sizeof(CTFPlayerAnimState));
	const auto aPose = pLocal->m_flPoseParameter();
	const int iSequence = pLocal->m_nSequence();
	const float flCycle = pLocal->m_flCycle();
	const float flFrameTime = I::GlobalVars->frametime;
	const float flCurTime = I::GlobalVars->curtime;

	if (m_bShownValid)
	{
		pAnimState->m_flEyeYaw = m_tShown.m_flEyeYaw;
		pAnimState->m_flEyePitch = m_tShown.m_flEyePitch;
		pAnimState->m_flGoalFeetYaw = m_tShown.m_flGoalFeetYaw;
		pAnimState->m_flCurrentFeetYaw = m_tShown.m_flCurrentFeetYaw;
		pAnimState->m_flLastAimTurnTime = m_tShown.m_flLastAimTurnTime;
		pAnimState->m_bCurrentFeetYawInitialized = m_tShown.m_bCurrentFeetYawInitialized;
		pAnimState->m_PoseParameterData = m_tShown.m_tPoseData;
		pAnimState->m_angRender = m_tShown.m_vRender;
		pLocal->m_flPoseParameter() = m_tShown.m_aPose;
		pLocal->m_nSequence() = m_tShown.m_iSequence;
		pLocal->m_flCycle() = m_tShown.m_flCycle;
	}

	I::GlobalVars->frametime = TICK_INTERVAL;
	I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());
	const float flPitch = std::clamp(vSent.x, -90.f, 90.f);
	for (int i = 0; i < iSteps; i++)
		pAnimState->Update(pAnimState->m_flEyeYaw = vSent.y, flPitch);

	m_tShown.m_flEyeYaw = pAnimState->m_flEyeYaw;
	m_tShown.m_flEyePitch = pAnimState->m_flEyePitch;
	m_tShown.m_flGoalFeetYaw = pAnimState->m_flGoalFeetYaw;
	m_tShown.m_flCurrentFeetYaw = pAnimState->m_flCurrentFeetYaw;
	m_tShown.m_flLastAimTurnTime = pAnimState->m_flLastAimTurnTime;
	m_tShown.m_bCurrentFeetYawInitialized = pAnimState->m_bCurrentFeetYawInitialized;
	m_tShown.m_tPoseData = pAnimState->m_PoseParameterData;
	m_tShown.m_vRender = pAnimState->m_angRender;
	m_tShown.m_aPose = pLocal->m_flPoseParameter();
	m_tShown.m_iSequence = pLocal->m_nSequence();
	m_tShown.m_flCycle = pLocal->m_flCycle();
	m_bShownValid = true;
	m_flSentYaw = vSent.y;

	pLocal->InvalidateBoneCache();
	m_bShownBones = pLocal->SetupBones(m_aShownBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime);

	I::GlobalVars->frametime = flFrameTime;
	I::GlobalVars->curtime = flCurTime;
	memcpy(pAnimState, aLive, sizeof(CTFPlayerAnimState));
	pLocal->m_flPoseParameter() = aPose;
	pLocal->m_nSequence() = iSequence;
	pLocal->m_flCycle() = flCycle;
	pLocal->InvalidateBoneCache();
}

bool CAntiAim::ScanHead(CTFPlayer* pLocal)
{
	auto pAnimState = pLocal->m_PlayerAnimState();
	auto pHdr = pLocal->GetStudiomodel();
	auto pSet = pLocal->GetAimHitboxSet();
	if (!pAnimState || !pHdr || !pSet || pSet->numhitboxes <= HITBOX_HEAD)
		return false;

	const int iAimYaw = pAnimState->m_PoseParameterData.m_iAimYaw;
	if (iAimYaw < 0 || iAimYaw >= pHdr->numlocalposeparameters)
		return false;

	HeadScan_t& tScan = m_tScan;
	pLocal->InvalidateBoneCache();
	if (!pLocal->SetupBones(tScan.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
		return false;

	tScan.m_pSet = pSet;
	tScan.m_pHead = pSet->pHitbox(HITBOX_HEAD);
	tScan.m_pAimYaw = pHdr->pLocalPoseParameter(iAimYaw);
	tScan.m_vOrigin = pLocal->m_vecOrigin();
	tScan.m_flGoalFeet = pAnimState->m_flGoalFeetYaw;
	tScan.m_flCurrentFeet = pAnimState->m_flCurrentFeetYaw;
	tScan.m_bMoving = pLocal->m_vecVelocity().Length() > 1.f;
	tScan.m_flHeadYaw = tScan.m_flCurrentFeet + TorsoTwist(tScan.m_pAimYaw, pAnimState->m_flEyeYaw, tScan.m_flCurrentFeet);

	const matrix3x4* aShownBones = m_bShownBones ? m_aShownBones : tScan.m_aBones;
	tScan.m_flShownYaw = m_bShownBones
		? m_tShown.m_flCurrentFeetYaw + TorsoTwist(tScan.m_pAimYaw, m_flSentYaw, m_tShown.m_flCurrentFeetYaw)
		: tScan.m_flHeadYaw;

	GetHeadPoints(tScan.m_pHead, tScan.m_aBones, tScan.m_aPoints);
	GetHeadPoints(tScan.m_pHead, aShownBones, tScan.m_aShownPoints);

	Vec3 vForward; Math::AngleVectors(I::EngineClient->GetViewAngles(), &vForward);
	tScan.m_vFrontEye = tScan.m_aPoints[0] + vForward * 8192.f;

	for (auto& tTarget : m_vTargets)
	{
		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnly filter = {};
		SDK::Trace(tTarget.m_vEye, tScan.m_aPoints[0], MASK_SHOT, &filter, &trace);
		tTarget.m_bSeesHead = trace.fraction == 1.f;
	}

	CountExposure(tScan.m_aBones, tScan.m_aPoints, tScan.m_flHeadYaw, tScan.m_aExposed);
	CountExposure(aShownBones, tScan.m_aShownPoints, tScan.m_flShownYaw, tScan.m_aShownExposed);
	return true;
}

void CAntiAim::CountExposure(const matrix3x4* aBones, const Vec3* aPoints, float flBuiltYaw, int* aExposed)
{
	for (int iBin = 0; iBin < AutoYawBins; iBin++)
	{
		aExposed[iBin] = 0;

		const float flTurn = flBuiltYaw - BinYaw(iBin);
		for (auto& tTarget : m_vTargets)
		{
			if (!tTarget.m_bSeesHead)
				continue;

			const Vec3 vEye = SpinAround(tTarget.m_vEye, m_tScan.m_vOrigin, flTurn);
			for (int i = 0; i < HeadPoints; i++)
			{
				if (FirstHitbox(m_tScan.m_pSet, aBones, vEye, aPoints[i]) == HITBOX_HEAD)
					aExposed[iBin]++;
			}
		}
	}
}

float CAntiAim::SettledHeadYaw(float flRealYaw, float flFakeYaw)
{
	const int iSettleCommands = int(180.f / (720.f * TICK_INTERVAL)) + 1;

	float flGoalFeet = m_tScan.m_flGoalFeet;
	float flCurrentFeet = m_tScan.m_flCurrentFeet;
	for (int iCommand = 0; iCommand < iSettleCommands; iCommand += m_iPlannedChoke + 1)
	{
		for (int i = 0; i < m_iPlannedChoke; i++)
			StepFeetYaw(flGoalFeet, flCurrentFeet, flRealYaw, m_tScan.m_bMoving);
		StepFeetYaw(flGoalFeet, flCurrentFeet, flFakeYaw, m_tScan.m_bMoving);
	}

	return flCurrentFeet + TorsoTwist(m_tScan.m_pAimYaw, flFakeYaw, flCurrentFeet);
}

int CAntiAim::CountShotOverlap(const Vec3& vEye, float flHeadYaw, float flFakeYaw)
{
	const HeadScan_t& tScan = m_tScan;
	const Vec3 vTurnedEye = SpinAround(vEye, tScan.m_vOrigin, tScan.m_flHeadYaw - flHeadYaw);
	const matrix3x4& mHead = tScan.m_aBones[tScan.m_pHead->bone];

	int iOverlap = 0;
	for (auto& vPoint : tScan.m_aShownPoints)
	{
		const Vec3 vShown = SpinAround(vPoint, tScan.m_vOrigin, flFakeYaw - tScan.m_flShownYaw + tScan.m_flHeadYaw - flHeadYaw);
		const Vec3 vDelta = vShown - vTurnedEye;
		float flDist = 0.f;
		if (Math::RayToOBB(vTurnedEye, vDelta, tScan.m_pHead->bbmin, tScan.m_pHead->bbmax, mHead, 1.f, &flDist) && flDist <= vDelta.Length())
			iOverlap++;
	}
	return iOverlap;
}

int CAntiAim::CountOverlap(float flHeadYaw, float flFakeYaw)
{
	int iOverlap = 0;
	bool bSeen = false;
	for (auto& tTarget : m_vTargets)
	{
		if (!tTarget.m_bSeesHead)
			continue;

		bSeen = true;
		iOverlap += CountShotOverlap(tTarget.m_vEye, flHeadYaw, flFakeYaw);
	}

	return bSeen ? iOverlap : CountShotOverlap(m_tScan.m_vFrontEye, flHeadYaw, flFakeYaw);
}

CAntiAim::YawScore_t CAntiAim::ScoreYaws(float flRealYaw, float flFakeYaw)
{
	const float flHeadYaw = SettledHeadYaw(flRealYaw, flFakeYaw);

	YawScore_t tScore = {};
	tScore.m_iRealExposed = m_tScan.m_aExposed[YawBin(flHeadYaw)];
	tScore.m_iOverlap = CountOverlap(flHeadYaw, flFakeYaw);
	tScore.m_iFakeExposed = m_tScan.m_aShownExposed[YawBin(flFakeYaw)];
	return tScore;
}

float CAntiAim::BestRealYaw(float flFakeYaw)
{
	float flBestYaw = m_flAutoReal;
	YawScore_t tBest = ScoreYaws(flBestYaw, flFakeYaw);
	for (int iBin = 0; iBin < AutoYawBins; iBin++)
	{
		const float flYaw = BinYaw(iBin);
		const YawScore_t tScore = ScoreYaws(flYaw, flFakeYaw);
		if (IsBetterYaw(tScore, tBest))
			tBest = tScore, flBestYaw = flYaw;
	}
	return flBestYaw;
}

float CAntiAim::BestFakeYaw(float flRealYaw)
{
	float flBestYaw = m_flAutoFake;
	YawScore_t tBest = ScoreYaws(flRealYaw, flBestYaw);
	for (int iBin = 0; iBin < AutoYawBins; iBin++)
	{
		const float flYaw = BinYaw(iBin);
		const YawScore_t tScore = ScoreYaws(flRealYaw, flYaw);
		if (IsBetterYaw(tScore, tBest))
			tBest = tScore, flBestYaw = flYaw;
	}
	return flBestYaw;
}

void CAntiAim::PlanAutoYaw(CTFPlayer* pLocal)
{
	if (!I::ClientState->chokedcommands)
		m_iPlannedChoke = m_iLastChoked;
	m_iLastChoked = I::ClientState->chokedcommands;

	const bool bRealAuto = Vars::AntiAim::YawReal.Value == Vars::AntiAim::YawEnum::Auto;
	const bool bFakeAuto = Vars::AntiAim::YawFake.Value == Vars::AntiAim::YawEnum::Auto;
	if (!bRealAuto && !bFakeAuto)
	{
		m_bAutoValid = false;
		return;
	}

	m_vTargets.clear();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsTaunting() || pPlayer->IsInvisible()
			|| pPlayer->InCond(TF_COND_HALLOWEEN_KART) || F::PlayerUtils.IsIgnored(pPlayer->entindex()))
			continue;

		auto pWeapon = pPlayer->m_hActiveWeapon().Get();
		if (pWeapon && IsHeadshotWeapon(pWeapon->As<CTFWeaponBase>()))
			m_vTargets.push_back({ pPlayer->GetShootPos() });
	}

	const float flView = I::EngineClient->GetViewAngles().y;
	const bool bHideHead = !m_vTargets.empty();
	const bool bPlan = bHideHead && !I::ClientState->chokedcommands && ScanHead(pLocal);
	if (!m_bAutoValid)
		m_flAutoReal = m_flAutoFake = flView;

	if (bRealAuto)
	{
		if (!bHideHead)
			m_flAutoReal = flView;
		else if (bPlan)
			m_flAutoReal = BestRealYaw(-m_flAutoReal);
	}

	if (bFakeAuto)
	{
		if (!bHideHead)
			m_flAutoFake = flView;
		else if (bPlan)
			m_flAutoFake = BestFakeYaw(bRealAuto ? m_flAutoReal : vRealAngles.y);
	}

	m_bAutoValid = true;
}

float CAntiAim::GetPitch(float flCurPitch)
{
	float flRealPitch = 0.f, flFakePitch = 0.f;
	int iJitter = GetJitter(FNV1A::Hash32Const("Pitch"));

	switch (Vars::AntiAim::PitchReal.Value)
	{
	case Vars::AntiAim::PitchRealEnum::Up: flRealPitch = -89.f; break;
	case Vars::AntiAim::PitchRealEnum::Down: flRealPitch = 89.f; break;
	case Vars::AntiAim::PitchRealEnum::Zero: flRealPitch = 0.f; break;
	case Vars::AntiAim::PitchRealEnum::Jitter: flRealPitch = -89.f * iJitter; break;
	case Vars::AntiAim::PitchRealEnum::ReverseJitter: flRealPitch = 89.f * iJitter; break;
	case Vars::AntiAim::PitchRealEnum::HalfUp: flRealPitch = -45.f; break;
	case Vars::AntiAim::PitchRealEnum::HalfDown: flRealPitch = 45.f; break;
	case Vars::AntiAim::PitchRealEnum::Random: flRealPitch = SDK::RandomFloat(-89.f, 89.f); break;
	case Vars::AntiAim::PitchRealEnum::Spin: flRealPitch = fmod(I::GlobalVars->tickcount * Vars::AntiAim::SpinSpeed.Value + 180.f, 360.f) - 180.f; break;
	case Vars::AntiAim::PitchRealEnum::UltraRandom:
	{
		if (I::GlobalVars->curtime > m_flPitchUltraNext)
		{
			m_flPitchUltraNext = I::GlobalVars->curtime + SDK::RandomFloat(0.5f, 5.f);
			m_flPitchUltra = SDK::RandomFloat(-89.f, 89.f);
		}
		flRealPitch = m_flPitchUltra;
		break;
	}
	case Vars::AntiAim::PitchRealEnum::Heck: flRealPitch = SDK::RandomFloat(-149489.97f, 149489.97f); break;
	case Vars::AntiAim::PitchRealEnum::Saw:
	{
		const float flProgress = fmodf(I::GlobalVars->tickcount * 0.035f, 2.f);
		flRealPitch = flProgress < 1.f ? -89.f + flProgress * 178.f : 89.f - (flProgress - 1.f) * 178.f;
		break;
	}
	case Vars::AntiAim::PitchRealEnum::Moonwalk:
	{
		static constexpr float arrPitches[5] = { -89.f, 89.f, -45.f, 45.f, 0.f };

		const int iTick = I::GlobalVars->tickcount;
		if (iTick >= m_iMoonwalkNext)
		{
			m_iMoonwalkNext = iTick + SDK::RandomInt(2, 8);
			m_flMoonwalkPitch = arrPitches[SDK::RandomInt(0, 4)];
		}

		flRealPitch = m_flMoonwalkPitch;
		break;
	}
	case Vars::AntiAim::PitchRealEnum::TimedFlip:
	{
		const float flCurTime = I::GlobalVars->curtime;

		if (m_flTimedFlipNext < flCurTime - 15.f)
		{
			m_flTimedFlipNext = 0.f;
			m_bTimedFlipUp = false;
		}

		if (!m_flTimedFlipNext)
		{
			m_bTimedFlipUp = true;
			m_flTimedFlipNext = flCurTime + 3.f;
		}
		else if (flCurTime >= m_flTimedFlipNext)
		{
			m_bTimedFlipUp = !m_bTimedFlipUp;
			m_flTimedFlipNext = flCurTime + 3.f;
		}

		flRealPitch = m_bTimedFlipUp ? -89.f : 89.f;
		break;
	}
	case Vars::AntiAim::PitchRealEnum::TimedFlipRandom:
	{
		const float flCurTime = I::GlobalVars->curtime;

		if (m_flTimedFlipRandNext < flCurTime - 15.f)
		{
			m_flTimedFlipRandNext = 0.f;
			m_bTimedFlipRandUp = false;
		}

		if (!m_flTimedFlipRandNext)
		{
			m_bTimedFlipRandUp = true;
			m_flTimedFlipRandNext = flCurTime + SDK::RandomFloat(3.f, 10.f);
		}
		else if (flCurTime >= m_flTimedFlipRandNext)
		{
			m_bTimedFlipRandUp = !m_bTimedFlipRandUp;
			m_flTimedFlipRandNext = flCurTime + SDK::RandomFloat(3.f, 10.f);
		}

		flRealPitch = m_bTimedFlipRandUp ? -89.f : 89.f;
		break;
	}
	}

	switch (Vars::AntiAim::PitchFake.Value)
	{
	case Vars::AntiAim::PitchFakeEnum::Up: flFakePitch = -89.f; break;
	case Vars::AntiAim::PitchFakeEnum::Down: flFakePitch = 89.f; break;
	case Vars::AntiAim::PitchFakeEnum::Jitter: flFakePitch = -89.f * iJitter; break;
	case Vars::AntiAim::PitchFakeEnum::ReverseJitter: flFakePitch = 89.f * iJitter; break;
	case Vars::AntiAim::PitchFakeEnum::HalfUp: flFakePitch = -45.f; break;
	case Vars::AntiAim::PitchFakeEnum::HalfDown: flFakePitch = 45.f; break;
	case Vars::AntiAim::PitchFakeEnum::Random: flFakePitch = SDK::RandomFloat(-89.f, 89.f); break;
	case Vars::AntiAim::PitchFakeEnum::Spin: flFakePitch = fmod(I::GlobalVars->tickcount * Vars::AntiAim::SpinSpeed.Value + 180.f, 360.f) - 180.f; break;
	case Vars::AntiAim::PitchFakeEnum::UltraRandom:
	{
		if (I::GlobalVars->curtime > m_flPitchUltraNextFake)
		{
			m_flPitchUltraNextFake = I::GlobalVars->curtime + SDK::RandomFloat(0.5f, 5.f);
			m_flPitchUltraFake = SDK::RandomFloat(-89.f, 89.f);
		}
		flFakePitch = m_flPitchUltraFake;
		break;
	}
	case Vars::AntiAim::PitchFakeEnum::Inverse: break;
	case Vars::AntiAim::PitchFakeEnum::Mirror: break;
	}

	if (Vars::AntiAim::PitchFake.Value == Vars::AntiAim::PitchFakeEnum::Mirror)
	{
		float flPitch = -(Vars::AntiAim::PitchReal.Value ? flRealPitch : flCurPitch);
		return flPitch + (flPitch >= 0.f ? 360.f : -360.f);
	}

	if (Vars::AntiAim::PitchFake.Value == Vars::AntiAim::PitchFakeEnum::Inverse)
	{
		float flPitch = Vars::AntiAim::PitchReal.Value ? flRealPitch : flCurPitch;
		if (flPitch <= -89.f)
			return flPitch + 360.f;
		if (flPitch >= 89.f)
			return flPitch - 360.f;
		return flPitch;
	}

	if (Vars::AntiAim::PitchReal.Value && Vars::AntiAim::PitchFake.Value)
		return flRealPitch + (flFakePitch > 0.f ? 360.f : -360.f);
	else if (Vars::AntiAim::PitchReal.Value)
		return flRealPitch;
	else if (Vars::AntiAim::PitchFake.Value)
		return flFakePitch;
	else
		return flCurPitch;
}

void CAntiAim::MinWalk(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::AntiAim::MinWalk.Value || !YawOn() || !pLocal->m_hGroundEntity() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	if (!pCmd->forwardmove && !pCmd->sidemove && pLocal->m_vecVelocity().Length2D() < 2.f)
	{
		float flMove = (pLocal->IsDucking() ? 3 : 1) * ((m_bMinWalkVar = !m_bMinWalkVar) ? 1 : -1);
		Vec3 vDir = { flMove, flMove, 0 };

		float flYaw = Math::NormalizeAngle(pCmd->viewangles.y);
		Vec3 vMove = Math::RotatePoint(vDir, {}, { 0, -flYaw, 0 });
		float flPitchNorm = Math::NormalizeAngle(pCmd->viewangles.x);
		pCmd->forwardmove = vMove.x * (fabsf(flPitchNorm) > 90.f ? -1 : 1);
		pCmd->sidemove = -vMove.y;
	}
}



void CAntiAim::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	CheckAndResetTime();
	static bool bAutoEnabled = false;
	const bool bTauntSpinActive = Vars::AntiAim::TauntSpin.Value && pLocal->IsTaunting();
	if (bTauntSpinActive && !Vars::AntiAim::Enabled.Value)
	{
		Vars::AntiAim::Enabled.Value = true;
		bAutoEnabled = true;
	}
	else if (!bTauntSpinActive && bAutoEnabled)
	{
		Vars::AntiAim::Enabled.Value = false;
		bAutoEnabled = false;
	}

	G::AntiAim = AntiAimOn() && ShouldRun(pLocal, pWeapon, pCmd);
	if (F::Misc.IsDuckSpeedActive())
	{
		G::AntiAim = false;
		return;
	}

	int iAntiBackstab = F::Misc.AntiBackstab(pLocal, pCmd);
	if (!iAntiBackstab)
		FakeShotAngles(pLocal, pWeapon, pCmd);

	if (!G::AntiAim)
	{
		vRealAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		vFakeAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		m_bShownValid = m_bShownBones = false;
		return;
	}

	vEdgeTrace.clear();
	PlanAutoYaw(pLocal);

	Vec2& vAngles = G::SendPacket ? vFakeAngles : vRealAngles;
	vAngles.x = iAntiBackstab != 2 ? GetPitch(pCmd->viewangles.x) : pCmd->viewangles.x;
	vAngles.y = !iAntiBackstab ? GetYaw(pLocal, pCmd, G::SendPacket) : pCmd->viewangles.y;

	if (F::AntiCheatCompatibility.Active())
		Math::ClampAngles(vAngles);

	Vec2 vFix = { Math::NormalizeAngle(vAngles.x), Math::NormalizeAngle(vAngles.y) };
	SDK::FixMovement(pCmd, vFix);
	pCmd->viewangles.x = vAngles.x;
	pCmd->viewangles.y = vAngles.y;

	if (!m_bAutoValid)
		m_bShownValid = m_bShownBones = false;
	else if (G::SendPacket)
		StepShown(pLocal, pCmd->viewangles, I::ClientState->chokedcommands + 1);

	MinWalk(pLocal, pCmd);
}

void CAntiAim::Draw(CTFPlayer* pLocal)
{
	if (!pLocal->IsAlive() || pLocal->IsAGhost() || !I::Input->CAM_IsThirdPerson() || !AntiAimOn())
		return;

	if (Vars::AntiAim::AntiAimLines.Value)
	{
		const auto& vOrigin = pLocal->GetAbsOrigin();

		Vec3 vScreen1, vScreen2;
		if (SDK::W2S(vOrigin, vScreen1))
		{
			if (SDK::W2S(vOrigin + Math::RotatePoint({ 50, 0, 0 }, {}, { 0, vRealAngles.y, 0 }), vScreen2))
				H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, { 0, 255, 0, 255 });
			if (SDK::W2S(vOrigin + Math::RotatePoint({ 50, 0, 0 }, {}, { 0, vFakeAngles.y, 0 }), vScreen2))
				H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, { 255, 0, 0, 255 });
		}

		for (auto& vPair : vEdgeTrace)
		{
			if (SDK::W2S(vPair.first, vScreen1) && SDK::W2S(vPair.second, vScreen2))
				H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, { 255, 255, 255, 255 });
		}
	}
}
