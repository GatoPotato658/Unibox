#include "NavBotJobs.h"
#include "../../Misc/NamedPipe/NamedPipe.h"

static Vector NormalizePlanar(Vector vDirection)
{
	vDirection.z = 0.f;
	const float flLength = vDirection.Length();
	if (flLength <= 0.001f)
		return {};

	return vDirection / flLength;
}

bool CNavBotGroup::GetFormationOffset(CTFPlayer* pLocal, int iPositionIndex, Vector& vOut)
{
	if (iPositionIndex <= 0)
		return false; // Leader has no offset

	// Calculate the movement direction of the leader
	Vector vLeaderVelocity(0, 0, 0);

	if (!m_vLocalBotPositions.empty())
		vLeaderVelocity = m_vLocalBotPositions[0].second;
	else
	{
		// No leader found, use our own direction
		vLeaderVelocity = pLocal->m_vecVelocity();
	}

	// Normalize leader velocity for direction
	Vector vDirection = vLeaderVelocity;
	if (vDirection.Length() < 10.0f) // If leader is barely moving, use view direction
	{
		QAngle viewAngles;
		I::EngineClient->GetViewAngles(viewAngles);
		Math::AngleVectors(viewAngles, &vDirection);
	}

	vDirection = NormalizePlanar(vDirection);

	// Calculate cross product for perpendicular direction (for side-by-side formations)
	[[maybe_unused]] const Vector vRight = NormalizePlanar(vDirection.Cross(Vector(0, 0, 1)));

	// Different formation styles:
	// 1. Line formation (bots following one after another)
	vOut = (vDirection * -m_flFormationDistance * iPositionIndex);
	return true;
}

void CNavBotGroup::UpdateLocalBotPositions(CTFPlayer* pLocal)
{
	if (!m_tUpdateFormationTimer.Run(0.5f))
		return;

	m_vLocalBotPositions.clear();

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	int iLocalIdx = pLocal->entindex();
	uint32 uLocalUserID = pResource->m_iUserID(iLocalIdx);
	int iLocalTeam = pLocal->m_iTeamNum();

	// Then check each player
	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		if (i == iLocalIdx || !pResource->m_bValid(i))
			continue;
#ifdef TEXTMODE
		// Is this a local bot????
		if (!F::NamedPipe.IsLocalBot(pResource->m_iAccountID(i)))
			continue;
#endif

		// Get the player entity
		auto pEntity = I::ClientEntityList->GetClientEntity(i)->As<CBaseEntity>();
		if (!pEntity || pEntity->IsDormant() ||
			!pEntity->IsPlayer() || pEntity->m_iTeamNum() != iLocalTeam)
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive())
			continue;

		// Add to our list
		m_vLocalBotPositions.push_back({ pResource->m_iUserID(i), pPlayer->m_vecVelocity() });
	}

	// Sort by friendsID to ensure consistent ordering across all bots
	std::sort(m_vLocalBotPositions.begin(), m_vLocalBotPositions.end(),
		[](const std::pair<uint32_t, Vector>& a, const std::pair<uint32_t, Vector>& b)
		{
			return a.first < b.first;
		});

	// Determine our position in the formatin
	m_iPositionInFormation = -1;

	// Add ourselves to the list for calculation purposes
	std::vector<uint32_t> vAllBotsInOrder;
	vAllBotsInOrder.push_back(uLocalUserID);

	for (const auto& bot : m_vLocalBotPositions)
		vAllBotsInOrder.push_back(bot.first);

	// Sort all bots (including us)
	std::sort(vAllBotsInOrder.begin(), vAllBotsInOrder.end());

	// Find our pofition
	for (size_t i = 0; i < vAllBotsInOrder.size(); i++)
	{
		if (vAllBotsInOrder[i] == uLocalUserID)
		{
			m_iPositionInFormation = static_cast<int>(i);
			break;
		}
	}
}

bool CNavBotGroup::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::GroupWithOthers))
		return false;

	static int iConsecutiveFailures = 0;
	static Vector vLastTargetPos;

	// UpdateLocalBotPositions is called from Run(), so we don't need to call it here
	// If we haven't found a position in formation, we can't move in formation
	if (m_iPositionInFormation < 0 || m_vLocalBotPositions.empty())
		return false;

	// If we're the leader, don't move in formation
	if (m_iPositionInFormation == 0)
		return false;

	// Get our offset in the formation
	Vector vOffset;
	if (!GetFormationOffset(pLocal, m_iPositionInFormation, vOffset))
		return false;

	// Find the leader
	Vector vLeaderPos;
	CTFPlayer* pLeaderPlayer = nullptr;

	if (!m_vLocalBotPositions.empty())
	{
		// Find the actual leader in-game
		auto pLeader = I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(m_vLocalBotPositions[0].first))->As<CBaseEntity>();
		if (pLeader && pLeader->IsPlayer())
		{
			pLeaderPlayer = pLeader->As<CTFPlayer>();
			vLeaderPos = pLeaderPlayer->GetAbsOrigin();
		}
	}
	if (!pLeaderPlayer)
		return false;

	if (pLeaderPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
		return false;

	Vector vTargetPos = vLeaderPos + vOffset;

	// If we're already close enough to our position, don't bother moving
	float flDistToTarget = pLocal->GetAbsOrigin().DistTo(vTargetPos);
	if (flDistToTarget <= 30.f)
	{
		// Release the patrol slot once we have actually settled into formation.
		if (F::NavEngine.IsPathing() && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol && vLastTargetPos.DistTo(vTargetPos) <= 50.f)
			F::NavEngine.CancelPath();

		iConsecutiveFailures = 0;
		vLastTargetPos = vTargetPos;
		return false;
	}

	// Only try to move to the position if we're not already pathing to something important
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Patrol)
		return false;

	// Check if we're trying to path to the same position but repeatedly failing
	if (vLastTargetPos.DistTo(vTargetPos) <= 50.f && !F::NavEngine.IsPathing())
	{
		iConsecutiveFailures++;

		// If we've been failing to reach the same target for a while,
		// temporarily increase the acceptable distance to prevent getting stuck
		if (iConsecutiveFailures >= 3)
		{
			iConsecutiveFailures = 0;

			// Try a different path approach or temp increase formation distance
			m_flFormationDistance += 50.0f; // Temp increase formation distance
			if (m_flFormationDistance > 300.0f) // Cap the maximum distance
				m_flFormationDistance = 120.0f; // Reset to default if it gets too large

			return true; // Skip this attempt and try again with new formation distance
		}
	}
	else if (vLastTargetPos.DistTo(vTargetPos) > 50.f)
		iConsecutiveFailures = 0;
	vLastTargetPos = vTargetPos;
	// Try to navigate to our position in formation
	if (F::NavEngine.NavTo(vTargetPos, PriorityListEnum::Patrol, true, !F::NavEngine.IsPathing()))
		return true;

	return false;
}
