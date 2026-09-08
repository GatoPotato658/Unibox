#include "TraceFilters.h"

#include "../../SDK.h"

#include <algorithm>

namespace
{
	void CacheSkipWeapon(CBaseEntity* pSkip, std::vector<int>& vWeapons, bool& bWeapon)
	{
		if (pSkip && pSkip->IsPlayer())
		{
			auto pPlayer = pSkip->As<CTFPlayer>();
			auto pActiveWeapon = pPlayer->m_hActiveWeapon().Get();
			if (auto pWeapon = pActiveWeapon ? pActiveWeapon->As<CTFWeaponBase>() : nullptr)
				bWeapon = std::ranges::contains(vWeapons, pWeapon->GetWeaponID());
		}
		vWeapons.clear();
	}
}

bool CTraceFilterHitscan::ShouldHitEntity(IHandleEntity* pHandleEntity, int nContentsMask)
{
	if (!pHandleEntity || pHandleEntity == m_pSkip)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pHandleEntity);
	const auto nClassID = pEntity->GetClassID();

	switch (nClassID)
	{
	case ETFClassID::CTFAmmoPack:
	case ETFClassID::CFuncAreaPortalWindow:
	case ETFClassID::CFuncRespawnRoomVisualizer:
	case ETFClassID::CTFReviveMarker:
		return false;
	case ETFClassID::CBaseDoor:
	case ETFClassID::CBasePropDoor:
		return nContentsMask & CONTENTS_MOVEABLE && !m_bIgnoreDoors;
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
		return nContentsMask & CONTENTS_MOVEABLE && !m_bIgnoreCart;
	}

	if (m_iTeam == -1)
		m_iTeam = m_pSkip ? m_pSkip->m_iTeamNum() : TEAM_UNASSIGNED;

	if (nClassID == ETFClassID::CTFPlayer ||
		nClassID == ETFClassID::CBaseObject ||
		nClassID == ETFClassID::CObjectSentrygun ||
		nClassID == ETFClassID::CObjectDispenser)
	{
		if (!(nContentsMask & CONTENTS_MOVEABLE)) return false;
		if (m_iType != SKIP_CHECK && !m_vWeapons.empty())
			CacheSkipWeapon(m_pSkip, m_vWeapons, m_bWeapon);

		if (m_iType != SKIP_CHECK && (m_iWeapon == WEAPON_INCLUDE ? m_bWeapon : !m_bWeapon))
			return m_iType == FORCE_HIT;

		return pEntity->m_iTeamNum() != m_iTeam;
	}
	if (nClassID == ETFClassID::CObjectTeleporter)
		return nContentsMask & CONTENTS_MOVEABLE;
	if (nClassID == ETFClassID::CTFMedigunShield)
		return !(nContentsMask & CONTENTS_PLAYERCLIP) && pEntity->m_iTeamNum() != m_iTeam;

	return nContentsMask & CONTENTS_SOLID;
}
TraceType_t CTraceFilterHitscan::GetTraceType() const
{
	return TRACE_EVERYTHING;
}

bool CTraceFilterCollideable::ShouldHitEntity(IHandleEntity* pHandleEntity, int nContentsMask)
{
	if (!pHandleEntity || pHandleEntity == m_pSkip)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pHandleEntity);
	const auto nClassID = pEntity->GetClassID();

	if (m_iTeam == -1) 
		m_iTeam = m_pSkip ? m_pSkip->m_iTeamNum() : TEAM_UNASSIGNED;

	switch (nClassID)
	{
	case ETFClassID::CBaseEntity:
		return nContentsMask & CONTENTS_SOLID;
	case ETFClassID::CFunc_LOD:
	case ETFClassID::CDynamicProp:
	case ETFClassID::CPhysicsProp:
	case ETFClassID::CPhysicsPropMultiplayer:
	case ETFClassID::CFuncConveyor:
	case ETFClassID::CTFGenericBomb: 
	case ETFClassID::CTFPumpkinBomb: 
		return nContentsMask & CONTENTS_MOVEABLE;
	case ETFClassID::CBaseDoor:
	case ETFClassID::CBasePropDoor:
		return nContentsMask & CONTENTS_MOVEABLE && !m_bIgnoreDoors;
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
		return nContentsMask & CONTENTS_MOVEABLE && !m_bIgnoreCart;
	case ETFClassID::CTFPlayer:
	{
		if (!(nContentsMask & CONTENTS_MONSTER)) return false;
		if (m_iPlayer == PLAYER_ALL) return true;
		if (m_iPlayer == PLAYER_NONE) return false;

		if (m_iType != SKIP_CHECK && !m_vWeapons.empty())
			CacheSkipWeapon(m_pSkip, m_vWeapons, m_bWeapon);

		if (m_iType != SKIP_CHECK && (m_iWeapon == WEAPON_INCLUDE ? m_bWeapon : !m_bWeapon))
			return m_iType == FORCE_HIT;

		return pEntity->m_iTeamNum() != m_iTeam;
	}
	case ETFClassID::CBaseObject:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
		return nContentsMask & CONTENTS_MOVEABLE && (m_iObject == OBJECT_ALL ? true : m_iObject == OBJECT_NONE ? false : pEntity->m_iTeamNum() != m_iTeam);
	case ETFClassID::CObjectTeleporter:
		return nContentsMask & CONTENTS_MOVEABLE;
	case ETFClassID::CTFBaseBoss:
	case ETFClassID::CTFTankBoss:
	case ETFClassID::CMerasmus:
	case ETFClassID::CEyeballBoss:
	case ETFClassID::CHeadlessHatman:
	case ETFClassID::CZombie:
		return nContentsMask & CONTENTS_MONSTER && m_bMisc;
	case ETFClassID::CTFGrenadePipebombProjectile:
		return nContentsMask & CONTENTS_MOVEABLE && m_bMisc && pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() == TF_GL_MODE_REMOTE_DETONATE;
	case ETFClassID::CFuncRespawnRoomVisualizer:
		return nContentsMask & CONTENTS_PLAYERCLIP && pEntity->m_iTeamNum() != m_iTeam;
	case ETFClassID::CTFMedigunShield:
		return !(nContentsMask & CONTENTS_PLAYERCLIP) && pEntity->m_iTeamNum() != m_iTeam;
	}

	return false;
}
TraceType_t CTraceFilterCollideable::GetTraceType() const
{
	return TRACE_EVERYTHING;
}

bool CTraceFilterWorldAndPropsOnly::ShouldHitEntity(IHandleEntity* pHandleEntity, int nContentsMask)
{
	if (!pHandleEntity || pHandleEntity == m_pSkip)
		return false;
	if (pHandleEntity->GetRefEHandle().GetSerialNumber() == (1 << 15))
		return nContentsMask & CONTENTS_SOLID && pHandleEntity->GetRefEHandle().GetEntryIndex() != m_iTeam; // team variable since cliententitylist can give nullptrs

	auto pEntity = reinterpret_cast<CBaseEntity*>(pHandleEntity);
	if (m_iTeam == -1) m_iTeam = m_pSkip ? m_pSkip->m_iTeamNum() : TEAM_UNASSIGNED;
	
	switch (pEntity->GetClassID())
	{
	case ETFClassID::CBaseEntity:
		return nContentsMask & CONTENTS_SOLID;
	case ETFClassID::CFunc_LOD:
	case ETFClassID::CBaseDoor:
	case ETFClassID::CDynamicProp:
	case ETFClassID::CPhysicsProp:
	case ETFClassID::CPhysicsPropMultiplayer:
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
	case ETFClassID::CFuncConveyor:
		return nContentsMask & CONTENTS_MOVEABLE;
	case ETFClassID::CFuncRespawnRoomVisualizer:
		return nContentsMask & CONTENTS_PLAYERCLIP && pEntity->m_iTeamNum() != m_iTeam;
	}

	return false;
}
TraceType_t CTraceFilterWorldAndPropsOnly::GetTraceType() const
{
	return TRACE_EVERYTHING_FILTER_PROPS;
}

#define MOVEMENT_COLLISION_GROUP 8
#define RED_CONTENTS_MASK 0x800
#define BLU_CONTENTS_MASK 0x1000

bool CTraceFilterNavigation::ShouldHitEntity(IHandleEntity* pHandleEntity, int nContentsMask)
{
	if (!pHandleEntity)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pHandleEntity);
	if (pEntity->entindex() == 0)
		return true;

	const auto nClassID = pEntity->GetClassID();
	if (nClassID == ETFClassID::CBaseEntity)
		return true;

	if (m_iTeam == -1)
		m_iTeam = m_pSkip ? m_pSkip->m_iTeamNum() : TEAM_UNASSIGNED;

	/*
	if (nClassID == ETFClassID::CBaseDoor ||
		nClassID == ETFClassID::CBasePropDoor)
	{
		if (pEntity->m_nSolidType() == SOLID_NONE || (pEntity->m_usSolidFlags() & FSOLID_NOT_SOLID))
			return false;

		const int iTeamMask = m_iTeam == TF_TEAM_RED ? BLU_CONTENTS_MASK :
			(m_iTeam == TF_TEAM_BLUE ? RED_CONTENTS_MASK : CONTENTS_PLAYERCLIP);
		const bool bBlocksMovement = pEntity->ShouldCollide(MOVEMENT_COLLISION_GROUP, iTeamMask);
		if (!bBlocksMovement)
			return false;

		const bool bPassableDoor = pEntity->m_CollisionGroup() == COLLISION_GROUP_PASSABLE_DOOR;
		const int iDoorTeam = pEntity->m_iTeamNum();
		const bool bFriendlyOrNeutralDoor = iDoorTeam == m_iTeam || iDoorTeam == TEAM_UNASSIGNED;
		if (bPassableDoor && bFriendlyOrNeutralDoor)
			return false;

		return true;
	}
	*/

	if (nClassID == ETFClassID::CDynamicProp ||
		nClassID == ETFClassID::CPhysicsProp ||
		nClassID == ETFClassID::CPhysicsPropMultiplayer)
		return false;

	if (nClassID == ETFClassID::CObjectTeleporter)
		return (nContentsMask & CONTENTS_PLAYERCLIP) || m_iObject != OBJECT_NONE;

	if (nClassID == ETFClassID::CTFPlayer)
	{
		if (m_iPlayer == PLAYER_ALL) return true;
		if (m_iPlayer == PLAYER_NONE) return false;
		return pEntity->m_iTeamNum() != m_iTeam;
	}

	if (nClassID == ETFClassID::CBaseObject ||
		nClassID == ETFClassID::CObjectSentrygun ||
		nClassID == ETFClassID::CObjectDispenser)
	{
		if (m_iObject == OBJECT_ALL) return true;
		if (m_iObject == OBJECT_NONE) return false;
		return pEntity->m_iTeamNum() != m_iTeam || ((nContentsMask & CONTENTS_PLAYERCLIP) && m_pSkip && pEntity->As<CBaseObject>()->m_hBuilder().GetEntryIndex() == m_pSkip->entindex());
	}

	if (nClassID == ETFClassID::CFuncRespawnRoomVisualizer)
		return (nContentsMask & CONTENTS_PLAYERCLIP) && m_iTeam != TEAM_UNASSIGNED && pEntity->ShouldCollide(MOVEMENT_COLLISION_GROUP, m_iTeam == TF_TEAM_RED ? BLU_CONTENTS_MASK : RED_CONTENTS_MASK);

	return false;
}

TraceType_t CTraceFilterNavigation::GetTraceType() const
{
	return TRACE_EVERYTHING;
}
