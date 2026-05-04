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
	if (!pLocal->IsAlive() || pLocal->IsAGhost() || (pLocal->IsTaunting() && !Vars::AntiAim::TauntSpin.Value) || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->InCond(TF_COND_HALLOWEEN_KART)
		|| G::Attacking == 1 || F::AutoRocketJump.IsRunning() || F::Ticks.m_bDoubletap // this m_bDoubletap check can probably be removed if we fix tickbase correctly
		|| pWeapon && pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka && pCmd->buttons & IN_ATTACK && !(G::LastUserCmd->buttons & IN_ATTACK))
		return false;

	if (pLocal->InCond(TF_COND_SHIELD_CHARGE) || pCmd->buttons & IN_ATTACK2 && pLocal->m_bShieldEquipped() && pLocal->m_flChargeMeter() == 100.f)
		return false;

	return true;
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

static inline int GetJitter(uint32_t uHash)
{
	static std::unordered_map<uint32_t, bool> mJitter = {};

	if (!I::ClientState->chokedcommands)
		mJitter[uHash] = !mJitter[uHash];
	return mJitter[uHash] ? 1 : -1;
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
		static float flNextChange[2] = { 0.f, 0.f };
		static float flYawOffset[2] = { 0.f, 0.f };
		static bool bSpin[2] = { false, false };
		static float flSpinSpeed[2] = { 0.f, 0.f };

		int i = bFake ? 1 : 0;
		float flCurTime = I::GlobalVars->curtime;

		if (flCurTime > flNextChange[i])
		{
			flNextChange[i] = flCurTime + SDK::RandomFloat(0.5f, 5.f);
			bSpin[i] = SDK::RandomInt(0, 1);
			if (bSpin[i])
				flSpinSpeed[i] = SDK::RandomFloat(-30.f, 30.f);
			else
				flYawOffset[i] = SDK::RandomFloat(-180.f, 180.f);
		}

		if (bSpin[i])
			return fmod(I::GlobalVars->tickcount * flSpinSpeed[i] + 180.f, 360.f) - 180.f;
		else
			return flYawOffset[i];
	}
	case Vars::AntiAim::YawEnum::Sideways:
	{
		static bool bSideways = false;
		if (bFake)
			bSideways = !bSideways;
		return bSideways ? 90.f : -90.f;
	}
	case Vars::AntiAim::YawEnum::Omega:
	{
		static float flRandomYaw = 0.f;
		if (bFake)
		{
			flRandomYaw = Math::NormalizeAngle(flRandomYaw + SDK::RandomFloat(-30.f, 30.f));
			return flRandomYaw;
		}
		return Math::NormalizeAngle(flRandomYaw - 180.f + SDK::RandomFloat(-40.f, 40.f));
	}
	case Vars::AntiAim::YawEnum::RandomUnclamped: return SDK::RandomFloat(-65536.f, 65536.f);
	case Vars::AntiAim::YawEnum::Heck: return SDK::RandomFloat(-359999.97f, 359999.97f);
	case Vars::AntiAim::YawEnum::Tornado:
	{
		static float flYaw[2] = {};
		static float flSpeed[2] = {};
		static int iRetuneTick[2] = {};

		const int i = bFake ? 1 : 0;
		const int iTick = I::GlobalVars->tickcount;
		if (iTick >= iRetuneTick[i] || !flSpeed[i])
		{
			iRetuneTick[i] = iTick + SDK::RandomInt(8, 24);
			const float flBaseSpeed = fmaxf(5.f, fabsf(Vars::AntiAim::SpinSpeed.Value));
			flSpeed[i] = SDK::RandomFloat(flBaseSpeed, flBaseSpeed * 3.f) * (SDK::RandomInt(0, 1) ? 1.f : -1.f);
		}

		flYaw[i] = Math::NormalizeAngle(flYaw[i] + flSpeed[i]);
		return Math::NormalizeAngle(flYaw[i] + sinf(iTick * 0.28f + i * 0.7f) * 35.f);
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
		static float flPhase[2] = {};
		const int i = bFake ? 1 : 0;
		const float flStep = fmaxf(0.01f, fabsf(Vars::AntiAim::SpinSpeed.Value) * 0.006f);
		flPhase[i] += flStep + (bFake ? 0.07f : 0.05f);

		const float flYaw = sinf(flPhase[i] * 2.3f) * 125.f + cosf(flPhase[i] * 1.1f) * 35.f;
		return Math::NormalizeAngle(flYaw);
	}
	case Vars::AntiAim::YawEnum::Quantum:
	{
		static int iNextShift[2] = {};
		static float flQuantumYaw[2] = {};
		static constexpr float arrQuantumAngles[8] = { -180.f, -135.f, -90.f, -45.f, 0.f, 45.f, 90.f, 135.f };

		const int i = bFake ? 1 : 0;
		const int iTick = I::GlobalVars->tickcount;
		if (iTick >= iNextShift[i])
		{
			iNextShift[i] = iTick + SDK::RandomInt(2, 7);
			flQuantumYaw[i] = arrQuantumAngles[SDK::RandomInt(0, 7)];
		}

		return Math::NormalizeAngle(flQuantumYaw[i] + SDK::RandomFloat(-25.f, 25.f));
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
	float flYaw = GetBaseYaw(pLocal, pCmd, bFake) + GetYawOffset(pLocal, bFake);
	return flYaw;
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
		static float flNextChange = 0.f;
		static float flPitch = 0.f;
		if (I::GlobalVars->curtime > flNextChange)
		{
			flNextChange = I::GlobalVars->curtime + SDK::RandomFloat(0.5f, 5.f);
			flPitch = SDK::RandomFloat(-89.f, 89.f);
		}
		flRealPitch = flPitch;
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
		static int iNextPick = 0;
		static float flPitch = 0.f;
		static constexpr float arrPitches[5] = { -89.f, 89.f, -45.f, 45.f, 0.f };

		const int iTick = I::GlobalVars->tickcount;
		if (iTick >= iNextPick)
		{
			iNextPick = iTick + SDK::RandomInt(2, 8);
			flPitch = arrPitches[SDK::RandomInt(0, 4)];
		}

		flRealPitch = flPitch;
		break;
	}
	case Vars::AntiAim::PitchRealEnum::TimedFlip:
	{
		static bool bUp = false;
		static float flNextSwap = 0.f;
		const float flCurTime = I::GlobalVars->curtime;

		if (flNextSwap < flCurTime - 15.f)
		{
			flNextSwap = 0.f;
			bUp = false;
		}

		if (!flNextSwap)
		{
			bUp = true;
			flNextSwap = flCurTime + 3.f;
		}
		else if (flCurTime >= flNextSwap)
		{
			bUp = !bUp;
			flNextSwap = flCurTime + 3.f;
		}

		flRealPitch = bUp ? -89.f : 89.f;
		break;
	}
	case Vars::AntiAim::PitchRealEnum::TimedFlipRandom:
	{
		static bool bUp = false;
		static float flNextSwap = 0.f;
		const float flCurTime = I::GlobalVars->curtime;

		if (flNextSwap < flCurTime - 15.f)
		{
			flNextSwap = 0.f;
			bUp = false;
		}

		if (!flNextSwap)
		{
			bUp = true;
			flNextSwap = flCurTime + SDK::RandomFloat(3.f, 10.f);
		}
		else if (flCurTime >= flNextSwap)
		{
			bUp = !bUp;
			flNextSwap = flCurTime + SDK::RandomFloat(3.f, 10.f);
		}

		flRealPitch = bUp ? -89.f : 89.f;
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
		static float flNextChange = 0.f;
		static float flPitch = 0.f;
		if (I::GlobalVars->curtime > flNextChange)
		{
			flNextChange = I::GlobalVars->curtime + SDK::RandomFloat(0.5f, 5.f);
			flPitch = SDK::RandomFloat(-89.f, 89.f);
		}
		flFakePitch = flPitch;
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
		static bool bVar = true;
		float flMove = (pLocal->IsDucking() ? 3 : 1) * ((bVar = !bVar) ? 1 : -1);
		Vec3 vDir = { flMove, flMove, 0 };

		Vec3 vMove = Math::RotatePoint(vDir, {}, { 0, -pCmd->viewangles.y, 0 });
		pCmd->forwardmove = vMove.x * (fmodf(fabsf(pCmd->viewangles.x), 180.f) > 90.f ? -1 : 1);
		pCmd->sidemove = -vMove.y;

		pLocal->m_vecVelocity() = { 1, 1 }; // a bit stupid but it's probably fine
	}
}



void CAntiAim::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	G::AntiAim = AntiAimOn() && ShouldRun(pLocal, pWeapon, pCmd);

	int iAntiBackstab = F::Misc.AntiBackstab(pLocal, pCmd);
	if (!iAntiBackstab)
		FakeShotAngles(pLocal, pWeapon, pCmd);

	if (!G::AntiAim)
	{
		vRealAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		vFakeAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		return;
	}

	vEdgeTrace.clear();

	Vec2& vAngles = G::SendPacket ? vFakeAngles : vRealAngles;
	vAngles.x = iAntiBackstab != 2 ? GetPitch(pCmd->viewangles.x) : pCmd->viewangles.x;
	vAngles.y = !iAntiBackstab ? GetYaw(pLocal, pCmd, G::SendPacket) : pCmd->viewangles.y;

	if (F::AntiCheatCompatibility.Active())
		Math::ClampAngles(vAngles);

	SDK::FixMovement(pCmd, vAngles);
	pCmd->viewangles.x = vAngles.x;
	pCmd->viewangles.y = vAngles.y;

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