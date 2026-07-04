#include "BotUtils.h"
#include "NavEngine/NavEngine.h"
#include "NavRuntime.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"

bool CBotUtils::IsSurfaceWalkable(const Vector& vNormal)
{
	static const Vector vUp = { 0.f, 0.f, 1.f };
	float flAngle = Math::Rad2Deg(std::acos(vNormal.Dot(vUp)));
	return flAngle < 50.f;
}

bool CBotUtils::SmartJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || !Vars::Misc::Movement::NavEngine::Enabled.Value || !Vars::Misc::Movement::NavBot::SmartJump.Value) return false;
	if (NavRuntime::IsMinigunJumpLocked(H::Entities.GetWeapon(), pCmd))
		return false;

	if (pLocal->OnSolid())
	{
		Vector vVelocity = pLocal->m_vecVelocity();
		Vector vMoveInput = { pCmd->forwardmove, -pCmd->sidemove, 0.f };
		if (vMoveInput.Length() > 0.f)
		{
			Vector vViewAngles = I::EngineClient->GetViewAngles();
			Vector vForward, vRight;
			Math::AngleVectors(vViewAngles, &vForward, &vRight, nullptr);
			vForward.z = vRight.z = 0.f;
			vForward.Normalize();
			vRight.Normalize();

			Vector vRotatedMoveDir = vForward * vMoveInput.x + vRight * vMoveInput.y;
			vVelocity = vRotatedMoveDir.Normalized() * std::max(10.f, vVelocity.Length());
		}

		const float flJumpForce = 277.f;
		const float flGravity = 800.f;
		float flTimeToPeak = flJumpForce / flGravity;
		float flDistTravelled = vVelocity.Length2D() * flTimeToPeak;
		Vector vJumpDirection = vVelocity.Normalized();
		if (F::NavEngine.IsPathing())
		{
			Vector vPathDir = F::NavEngine.GetCurrentPathDir();
			if (!vPathDir.IsZero())
			{
				if (vJumpDirection.Dot(vPathDir) < 0.5f)
					return false;

				auto pCrumbs = F::NavEngine.GetCrumbs();
				if (pCrumbs->size() > 1)
				{
					Vector vNextDir = ((*pCrumbs)[1].m_vPos - (*pCrumbs)[0].m_vPos);
					vNextDir.z = 0.f;
					if (vNextDir.Normalize() > 0.1f && vPathDir.Dot(vNextDir) < 0.707f)
					{
						if (pLocal->GetAbsOrigin().DistTo((*pCrumbs)[0].m_vPos) < 100.f)
							return false;
					}
				}

				vJumpDirection = vPathDir;
			}
		}
		Vector vJumpPeakPos = pLocal->GetAbsOrigin() + vJumpDirection * flDistTravelled;
		m_vJumpPeakPos = vJumpPeakPos;

		const Vector vHullMin = { -23.99f, -23.99f, 0.f };
		const Vector vHullMax = { 23.99f, 23.99f, 62.f };
		const Vector vHullMinSjump = { -16.f, -16.f, 0.f };
		const Vector vHullMaxSjump = { 16.f, 16.f, 62.f };
		const Vector vStepHeight = { 0.f, 0.f, 18.f };
		const Vector vMaxJumpHeight = { 0.f, 0.f, 72.f };

		Vector vTraceStart = pLocal->GetAbsOrigin() + vStepHeight;
		Vector vTraceEnd = vTraceStart + vJumpDirection * flDistTravelled;

		CGameTrace forwardTrace = {};
		CTraceFilterNavigation filter(pLocal);
		filter.m_iPlayer = PLAYER_DEFAULT;
		SDK::TraceHull(vTraceStart, vTraceEnd, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID, &filter, &forwardTrace);

		m_vPredictedJumpPos = forwardTrace.endpos;

		if (forwardTrace.fraction < 1.0f && !IsSurfaceWalkable(forwardTrace.plane.normal))
		{
			CGameTrace downwardTrace = {};
			SDK::TraceHull(forwardTrace.endpos, forwardTrace.endpos - vMaxJumpHeight, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID_BRUSHONLY, &filter, &downwardTrace);

			Vector vLandingPos = downwardTrace.endpos + vJumpDirection * 10.f;
			CGameTrace landingTrace = {};
			SDK::TraceHull(vLandingPos + vMaxJumpHeight, vLandingPos, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &landingTrace);

			m_vPredictedJumpPos = landingTrace.endpos;

			if (landingTrace.fraction > 0.f && landingTrace.fraction < 0.75f)
			{
				if (IsSurfaceWalkable(landingTrace.plane.normal))
					return true;
			}
		}
	}
	else if (pCmd->buttons & IN_JUMP)
		return true;

	return false;
}

void CBotUtils::HandleSmartJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || !Vars::Misc::Movement::NavEngine::Enabled.Value || F::AutoRocketJump.IsRunning())
	{
		m_eJumpState = STATE_AWAITING_JUMP;
		return;
	}

	if (NavRuntime::IsMinigunJumpLocked(H::Entities.GetWeapon(), pCmd))
	{
		pCmd->buttons &= ~IN_JUMP;
		m_eJumpState = STATE_AWAITING_JUMP;
		return;
	}

	bool bOnGround = pLocal->OnSolid();
	bool bDucking = pLocal->IsDucking();

	if (bOnGround && bDucking)
		m_eJumpState = STATE_AWAITING_JUMP;

	switch (m_eJumpState)
	{
	case STATE_AWAITING_JUMP:
		if (SmartJump(pLocal, pCmd))
			m_eJumpState = (Vars::Misc::Movement::AutoCTap.Value && bOnGround) ? STATE_CTAP : STATE_JUMP;
		break;
	case STATE_CTAP:
		pCmd->buttons |= IN_DUCK;
		pCmd->buttons &= ~IN_JUMP;
		m_eJumpState = STATE_JUMP;
		break;
	case STATE_JUMP:
		pCmd->buttons &= ~IN_DUCK;
		pCmd->buttons |= IN_JUMP;
		m_eJumpState = STATE_ASCENDING;
		break;
	case STATE_ASCENDING:
		pCmd->buttons |= IN_DUCK;
		if (pLocal->m_vecVelocity().z <= 0.f)
			m_eJumpState = STATE_DESCENDING;
		else if (bOnGround)
			m_eJumpState = STATE_AWAITING_JUMP;
		break;
	case STATE_DESCENDING:
		pCmd->buttons &= ~IN_DUCK;
		if (!bOnGround)
		{
			if (SmartJump(pLocal, pCmd))
			{
				pCmd->buttons &= ~IN_DUCK;
				pCmd->buttons |= IN_JUMP;
				m_eJumpState = (Vars::Misc::Movement::AutoCTap.Value && bOnGround) ? STATE_CTAP : STATE_JUMP;
			}
		}
		else
		{
			pCmd->buttons |= IN_DUCK;
			m_eJumpState = STATE_AWAITING_JUMP;
		}
		break;
	}
}
