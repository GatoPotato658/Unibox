#include "FollowBot.h"
#include "../Misc/Misc.h"
#include "../Players/PlayerUtils.h"
#include "../NavBot/BotUtils.h"
#include "../NavBot/NavEngine/NavEngine.h"

static bool HasManualMovementInput(CUserCmd* pCmd)
{
	return pCmd && (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT)) && !F::Misc.m_bAntiAFK;
}

static void SyncBestSlot(CTFPlayer* pLocal)
{
	if (F::BotUtils.m_iCurrentSlot != F::BotUtils.m_iBestSlot)
		F::BotUtils.SetSlot(pLocal, Vars::Misc::Movement::BotUtils::WeaponSlot.Value ? F::BotUtils.m_iBestSlot : -1);
}

static bool CanUseNavToTarget(const FollowTarget_t& tTarget)
{
	if (!Vars::Misc::Movement::FollowBot::UseNav.Value || !F::NavEngine.IsNavMeshLoaded() || tTarget.m_vLastKnownPos.IsZero())
		return false;

	if (!tTarget.m_bDormant)
		return Vars::Misc::Movement::FollowBot::UseNav.Value;

	return Vars::Misc::Movement::FollowBot::UseNav.Value == Vars::Misc::Movement::FollowBot::UseNavEnum::Dormant;
}

static bool ShouldCancelFollowNav(const FollowTarget_t& tTarget, const Vec3& vLocalOrigin)
{
	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::Followbot)
		return false;

	const bool bClose = vLocalOrigin.DistTo(tTarget.m_vLastKnownPos) < Vars::Misc::Movement::FollowBot::FollowDistance.Value + 150.f;
	if (!tTarget.m_bNew && bClose)
		return true;

	return !tTarget.m_vLastKnownPos.IsZero() && F::NavEngine.IsPathing() &&
		tTarget.m_vLastKnownPos.DistTo(F::NavEngine.GetCrumbs()->back().m_vPos) >= Vars::Misc::Movement::FollowBot::AbandonDistance.Value;
}

static bool ShouldStartFollowNav(const FollowTarget_t& tTarget, size_t nCurrentPathSize)
{
	return tTarget.m_bUnreachable ||
		tTarget.m_bDormant ||
		(tTarget.m_bNew && tTarget.m_flDistance >= Vars::Misc::Movement::FollowBot::FollowDistance.Value) ||
		tTarget.m_flDistance >= Vars::Misc::Movement::FollowBot::AbandonDistance.Value ||
		nCurrentPathSize >= Vars::Misc::Movement::FollowBot::MaxNodes.Value;
}

static int GetPreferScore(int iEntIndex)
{
	switch (Vars::Misc::Movement::FollowBot::Prefer.Value)
	{
	case Vars::Misc::Movement::FollowBot::PreferEnum::PreferFriends:
		return H::Entities.IsFriend(iEntIndex) ? 1 : 0;
	case Vars::Misc::Movement::FollowBot::PreferEnum::PartyMembers:
		return H::Entities.InParty(iEntIndex) ? 1 : 0;
	}
	return 0;
}

static int GetClassPreference(CTFPlayer* pLocal, CTFPlayer* pPlayer, int iPriority, int iPrefer)
{
	if (!pPlayer || !pLocal || iPriority || iPrefer || pPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
		return 0;

	switch (pPlayer->m_iClass())
	{
	case TF_CLASS_HEAVY:
	case TF_CLASS_SOLDIER:
	case TF_CLASS_DEMOMAN:
		return 2;
	case TF_CLASS_ENGINEER:
	case TF_CLASS_SPY:
		return 0;
	default:
		return 1;
	}
}

void CFollowBot::UpdateTargets(CTFPlayer* pLocal)
{
	m_vTargets.clear();
	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	auto vLocalOrigin = pLocal->GetAbsOrigin();
	const auto eGroup = Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Teammates &&
		Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Enemies ? EntityEnum::PlayerAll :
		Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Teammates ? EntityEnum::PlayerTeam : EntityEnum::PlayerEnemy;

	float flMaxDist = Vars::Misc::Movement::FollowBot::UseNav.Value && F::NavEngine.IsNavMeshLoaded() ? Vars::Misc::Movement::FollowBot::NavAbandonDistance.Value : Vars::Misc::Movement::FollowBot::ActivationDistance.Value;
	bool bTryDormant = Vars::Misc::Movement::FollowBot::UseNav.Value == Vars::Misc::Movement::FollowBot::UseNavEnum::Dormant && F::NavEngine.IsNavMeshLoaded();
	const auto& vPlayers = H::Entities.GetGroup(eGroup);
	m_vTargets.reserve(vPlayers.size());
	for (auto pEntity : vPlayers)
	{
		int iEntIndex = pEntity->entindex();
		if (pLocal->entindex() != iEntIndex && pResource->m_bValid(iEntIndex))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			bool bDormant = pPlayer->IsDormant();
			int iPriority = F::PlayerUtils.GetFollowPriority(iEntIndex, false);
			if (iPriority >= Vars::Misc::Movement::FollowBot::MinPriority.Value &&
				(bTryDormant || !bDormant) &&
				pPlayer->IsAlive() &&
				!pPlayer->IsAGhost() &&
				!IsAfkTarget(pPlayer))
			{
				Vec3 vOrigin;
				float flDistance = FLT_MAX;
				if (F::BotUtils.GetDormantOrigin(iEntIndex, &vOrigin))
					flDistance = vLocalOrigin.DistTo(vOrigin);

				if (flDistance <= flMaxDist)
				{
					int iPrefer = GetPreferScore(iEntIndex);
					int iClassPreference = GetClassPreference(pLocal, pPlayer, iPriority, iPrefer);
					m_vTargets.emplace_back(
						iEntIndex,
						pResource->m_iUserID(iEntIndex),
						iPriority,
						iPrefer,
						iClassPreference,
						flDistance,
						false,
						true,
						bDormant,
						vOrigin,
						FNV1A::Hash32(F::PlayerUtils.GetPlayerName(iEntIndex, pResource->GetName(iEntIndex))),
						pPlayer
					);
				}
			}
		}
	}

	std::sort(m_vTargets.begin(), m_vTargets.end(), [&](const FollowTarget_t& a, const FollowTarget_t& b) -> bool
		{
			if (a.m_iPriority != b.m_iPriority)
				return a.m_iPriority > b.m_iPriority;

			if (a.m_iPrefer != b.m_iPrefer)
				return a.m_iPrefer > b.m_iPrefer;

			if (a.m_iClassPreference != b.m_iClassPreference)
				return a.m_iClassPreference > b.m_iClassPreference;

			return a.m_flDistance < b.m_flDistance;
		});
}

void CFollowBot::UpdateLockedTarget(CTFPlayer* pLocal)
{
	if (m_tLockedTarget.m_iUserID == -1)
		return;

	if ((m_tLockedTarget.m_iEntIndex = I::EngineClient->GetPlayerForUserID(m_tLockedTarget.m_iUserID)) <= 0)
	{
		Reset(FB_RESET_NAV);
		return;
	}

	// Did our target leave and someone took their uid? 
	// Should never happen unless we had a huge network lag
	// Actually i dont think this will ever happen in any case as i havent seen uids being reused in the same match
	/*
	auto pResource = H::Entities.GetResource();
	if (pResource && pResource->m_bValid(m_tLockedTarget.m_iEntIndex) &&
		FNV1A::Hash32(F::PlayerUtils.GetPlayerName(m_tLockedTarget.m_iEntIndex, pResource->GetName(m_tLockedTarget.m_iEntIndex))) != m_tLockedTarget.m_uNameHash)
	{
		Reset(FB_RESET_NAV);
		return;
	}
	*/

	if (!(m_tLockedTarget.m_pPlayer = I::ClientEntityList->GetClientEntity(m_tLockedTarget.m_iEntIndex)->As<CTFPlayer>()))
		return;

	if (!IsValidTarget(pLocal, m_tLockedTarget.m_pPlayer))
	{
		Reset(FB_RESET_NAV);
		return;
	}

	auto vLocalOrigin = pLocal->GetAbsOrigin();
	float flDistance = FLT_MAX;
	bool bCanNav = Vars::Misc::Movement::FollowBot::UseNav.Value && F::NavEngine.IsNavMeshLoaded();
	if (!(m_tLockedTarget.m_bDormant = m_tLockedTarget.m_pPlayer->IsDormant()))
		flDistance = vLocalOrigin.DistTo(m_tLockedTarget.m_vLastKnownPos = m_tLockedTarget.m_pPlayer->GetAbsOrigin());
	else if (Vars::Misc::Movement::FollowBot::UseNav.Value == Vars::Misc::Movement::FollowBot::UseNavEnum::Dormant && bCanNav)
	{
		Vector vOrigin;
		if (F::BotUtils.GetDormantOrigin(m_tLockedTarget.m_iEntIndex, &vOrigin))
			m_tLockedTarget.m_vLastKnownPos = vOrigin;

		if (m_tLockedTarget.m_vLastKnownPos.IsZero())
		{
			Reset(FB_RESET_NAV);
			return;
		}
		flDistance = Vars::Misc::Movement::FollowBot::NavAbandonDistance.Value;
	}

	float flMaxDist = bCanNav ? Vars::Misc::Movement::FollowBot::NavAbandonDistance.Value : Vars::Misc::Movement::FollowBot::AbandonDistance.Value;
	if (flDistance > flMaxDist)
	{
		Reset(FB_RESET_NAV);
		return;
	}

	m_tLockedTarget.m_flDistance = flDistance;
	m_tLockedTarget.m_bNew = false;
}

bool CFollowBot::IsValidTarget(CTFPlayer* pLocal, CTFPlayer* pPlayer)
{
	if (!pPlayer || !pPlayer->IsPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
		return false;

	if (IsAfkTarget(pPlayer))
		return false;

	if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
	{
		if (Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Enemies)
			return true;
	}
	else if (Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Teammates)
		return true;

	return false;
}

bool CFollowBot::IsAfkTarget(CTFPlayer* pPlayer)
{
	if (!pPlayer || pPlayer->IsDormant())
		return false;

	int iEntIndex = pPlayer->entindex();
	if (iEntIndex <= 0 || iEntIndex >= MAX_PLAYERS || !I::GlobalVars)
		return false;

	Vec3 vOrigin = pPlayer->GetAbsOrigin();
	Vec3 vAngles = pPlayer->GetEyeAngles();
	const bool bMoved = m_aLastOrigins[iEntIndex].IsZero() || vOrigin.DistTo(m_aLastOrigins[iEntIndex]) > 8.f;
	const bool bLooked = vAngles.DeltaAngle(m_aLastAngles[iEntIndex]).Length() > 2.f;
	const bool bVelocity = pPlayer->m_vecVelocity().Length2D() > 10.f;
	const bool bButtons = pPlayer->m_nButtons() & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT | IN_JUMP | IN_DUCK | IN_ATTACK | IN_ATTACK2);

	if (bMoved || bLooked || bVelocity || bButtons || m_aLastActiveTimes[iEntIndex] <= 0.f)
		m_aLastActiveTimes[iEntIndex] = I::GlobalVars->curtime;

	m_aLastOrigins[iEntIndex] = vOrigin;
	m_aLastAngles[iEntIndex] = vAngles;

	return I::GlobalVars->curtime - m_aLastActiveTimes[iEntIndex] > 8.f;
}

void CFollowBot::LookAtPath(CTFPlayer* pLocal, CUserCmd* pCmd, std::deque<Vec3>* vIn, bool bSmooth)
{
	bool bSilent = Vars::Misc::Movement::FollowBot::LookAtPath.Value == Vars::Misc::Movement::FollowBot::LookAtPathEnum::Silent;
	if (!bSilent || !G::AntiAim)
	{
		if (G::Attacking != 1)
		{
			switch (Vars::Misc::Movement::FollowBot::LookAtPathMode.Value)
			{
			case Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Path:
				F::BotUtils.LookAtPath(pCmd, vIn->front().Get2D(), pLocal->GetEyePosition(), bSilent);
				break;
			case Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Copy:
				if (!vIn->size())
					return;
				[[fallthrough]];
			default:
				F::BotUtils.LookAtPath(pCmd, vIn->front(), pLocal->GetEyePosition(), bSilent, bSmooth);
				break;
			}
		}
	}
	if (Vars::Misc::Movement::FollowBot::LookAtPathMode.Value == Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Copy && vIn->size())
		vIn->pop_front();
}

void CFollowBot::Run(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::FollowBot::Enabled.Value ||
		!Vars::Misc::Movement::FollowBot::Targets.Value ||
		!pLocal->IsAlive() || pLocal->IsTaunting())
	{
		Reset(FB_RESET_TARGETS | FB_RESET_NAV);
		return;
	}

	if (HasManualMovementInput(pCmd))
	{
		Reset(FB_RESET_TARGETS | FB_RESET_NAV);
		return;
	}

	if (!Vars::Misc::Movement::NavBot::Enabled.Value)
		SyncBestSlot(pLocal);

	UpdateTargets(pLocal);
	UpdateLockedTarget(pLocal);

	if (m_vTargets.empty())
	{
		if (m_tLockedTarget.m_iUserID == -1)
		{
			Reset(FB_RESET_NAV);
			return;
		}
	}
	else
	{
		if (m_tLockedTarget.m_iUserID == -1 || m_tLockedTarget.m_iUserID != m_vTargets.front().m_iUserID)
		{
			Reset(FB_RESET_NAV);
			m_tLockedTarget = m_vTargets.front();
		}
	}

	Vec3 vLocalOrigin = pLocal->GetAbsOrigin();
	if (ShouldCancelFollowNav(m_tLockedTarget, vLocalOrigin))
	{
		const bool bClose = vLocalOrigin.DistTo(m_tLockedTarget.m_vLastKnownPos) < Vars::Misc::Movement::FollowBot::FollowDistance.Value + 150.f;
		if (bClose && m_tLockedTarget.m_bDormant)
		{
			// We reached our goal but the target is nowhere to be found
			Reset(FB_RESET_NAV);
			return;
		}

		m_tLockedTarget.m_bUnreachable = false;
		F::NavEngine.CancelPath();
	}

	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::Followbot)
	{
		if (ShouldStartFollowNav(m_tLockedTarget, m_vCurrentPath.size()))
		{
			const bool bNav = CanUseNavToTarget(m_tLockedTarget) && F::NavEngine.GetLocalNavArea(vLocalOrigin) &&
				F::NavEngine.NavTo(m_tLockedTarget.m_vLastKnownPos, PriorityListEnum::Followbot);

			// We couldn't find a path to the target
			if (!bNav)
				Reset(FB_RESET_NONE);
			else
			{
				m_bActive = false;
				m_vCurrentPath.clear();
				m_vTempAngles.clear();
			}

			return;
		}
	}
	else
	{
		SyncBestSlot(pLocal);

		// Already pathing, no point in running everything else
		return;
	}

	if (m_tLockedTarget.m_pPlayer)
	{
		auto vCurrentOrigin = m_tLockedTarget.m_pPlayer->GetAbsOrigin();
		if (vLocalOrigin.DistTo(vCurrentOrigin) >= Vars::Misc::Movement::FollowBot::AbandonDistance.Value)
		{
			Reset(FB_RESET_NONE);
			return;
		}
		m_bActive = true;

		float flDistToCurrent = vLocalOrigin.DistTo2D(vCurrentOrigin);
		if (flDistToCurrent <= Vars::Misc::Movement::FollowBot::FollowDistance.Value ||
			(m_vCurrentPath.size() > 8 && flDistToCurrent < vLocalOrigin.DistTo2D(m_vCurrentPath.front().m_vOrigin)))
		{
			m_vCurrentPath.clear();
			m_vTempAngles.clear();
		}
		else
		{
			if (Vars::Misc::Movement::FollowBot::LookAtPath.Value && Vars::Misc::Movement::FollowBot::LookAtPathMode.Value == Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Copy)
				m_vTempAngles.push_back(m_tLockedTarget.m_pPlayer->GetEyeAngles());
			else
				m_vTempAngles.clear();

			if (m_vCurrentPath.size())
			{
				if (m_vCurrentPath.back().m_vOrigin.DistTo2D(vCurrentOrigin) >= 20.f)
				{
					m_vCurrentPath.push_back({ vCurrentOrigin, m_vTempAngles });
					m_vTempAngles.clear();
				}
			}
			else
			{
				m_vCurrentPath.push_back({ vCurrentOrigin, m_vTempAngles });
				m_vTempAngles.clear();
			}
		}
	}

	Vec3 vDest;
	bool bShouldWalk = false;
	std::deque<Vec3>* pCurrentAngles = nullptr;
	if (m_vCurrentPath.size() > 1)
	{
		auto begin = m_vCurrentPath.rbegin();
		auto eraseAt = begin;

		// Iterate in reverse so we can optimize the path by erasing nodes older than a close one
		for (auto it = begin, end = m_vCurrentPath.rend(); it != end; ++it)
		{
			if (vLocalOrigin.DistTo2D(it->m_vOrigin) <= 20.f)
			{
				eraseAt = it;

				// Go back to the last checked node
				if (it != begin)
					--it;
			}

			// We found a closest node or reached the beginning of the path
			if (eraseAt != begin || it == end - 1)
			{
				vDest = it->m_vOrigin;
				if ((vDest.z - vLocalOrigin.z) <= PLAYER_CROUCHED_JUMP_HEIGHT)
				{
					pCurrentAngles = &it->m_vAngles;
					bShouldWalk = true;
				}
				// Our goal is too high up we cant reach our target
				else m_tLockedTarget.m_bUnreachable = true;
				break;
			}
		}
		if (m_tLockedTarget.m_bUnreachable)
			m_vCurrentPath.clear();
		else if (eraseAt != begin)
			m_vCurrentPath.erase(m_vCurrentPath.begin(), eraseAt.base());
	}
	else if (m_vCurrentPath.size())
	{
		vDest = m_vCurrentPath.front().m_vOrigin;
		pCurrentAngles = &m_vCurrentPath.front().m_vAngles;
		bShouldWalk = true;
	}

	if (Vars::Misc::Movement::FollowBot::LookAtPath.Value)
	{
		std::deque<Vec3> vCurrentAngles;
		if (!pCurrentAngles || Vars::Misc::Movement::FollowBot::LookAtPathMode.Value >= Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::CopyImmediate)
		{
			Vector vAngles = m_vLastTargetAngles;
			if (m_tLockedTarget.m_pPlayer)
			{
				if (Vars::Misc::Movement::FollowBot::LookAtPathMode.Value == Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::AtTarget)
					vAngles = m_vLastTargetAngles = Math::CalcAngle(pLocal->GetEyePosition(), m_tLockedTarget.m_pPlayer->m_vecOrigin() + m_tLockedTarget.m_pPlayer->GetViewOffset());
				else
					vAngles = m_vLastTargetAngles = m_tLockedTarget.m_pPlayer->GetEyeAngles();
			}
			else if (m_tLockedTarget.m_iEntIndex == -1)
				vAngles = I::EngineClient->GetViewAngles();

			vCurrentAngles.push_back(vAngles);
		}
		else if (Vars::Misc::Movement::FollowBot::LookAtPathMode.Value == Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Path)
			vCurrentAngles.push_back(vDest);

		std::deque<Vec3>* pFinalAngles = !vCurrentAngles.empty() ? &vCurrentAngles : pCurrentAngles;
		LookAtPath(pLocal, pCmd, pFinalAngles, Vars::Misc::Movement::FollowBot::LookAtPathNoSnap.Value && Math::CalcFov(pFinalAngles->front(), F::BotUtils.m_vLastAngles) > 3.f);
	}

	SyncBestSlot(pLocal);

	if (bShouldWalk)
		SDK::WalkTo(pCmd, pLocal, vDest);
}

void CFollowBot::Reset(int iFlags)
{
	if (iFlags & FB_RESET_NAV && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Followbot)
		F::NavEngine.CancelPath();
	if (iFlags & FB_RESET_TARGETS)
		m_vTargets.clear();

	m_vCurrentPath.clear();
	m_vTempAngles.clear();
	m_tLockedTarget = FollowTarget_t{};
	m_bActive = false;
	if (iFlags & FB_RESET_TARGETS)
	{
		m_aLastActiveTimes = {};
		m_aLastOrigins = {};
		m_aLastAngles = {};
	}
}

void CFollowBot::Render()
{
	if (!Vars::Misc::Movement::FollowBot::DrawPath.Value || !m_vCurrentPath.size())
		return;

	if (m_vCurrentPath.size() > 1)
	{
		for (size_t i = 0; i < m_vCurrentPath.size() - 1; i++)
		{
			H::Draw.RenderBox(m_vCurrentPath[i].m_vOrigin, Vector(-1.0f, -1.0f, -1.0f), Vector(1.0f, 1.0f, 1.0f), Vector(), Vars::Colors::FollowbotPathBox.Value, false);
			H::Draw.RenderLine(m_vCurrentPath[i].m_vOrigin, m_vCurrentPath[i + 1].m_vOrigin, Vars::Colors::FollowbotPathLine.Value, false);
		}
	}
	H::Draw.RenderBox(m_vCurrentPath.back().m_vOrigin, Vector(-1.0f, -1.0f, -1.0f), Vector(1.0f, 1.0f, 1.0f), Vector(), Vars::Colors::FollowbotPathBox.Value, false);
}
