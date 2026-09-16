#pragma once
#include "../../SDK/SDK.h"

enum NavAttributeType
{
    NAV_MESH_INVALID = 0,
    NAV_MESH_CROUCH  = 0x0000001,
    NAV_MESH_JUMP    = 0x0000002,
    NAV_MESH_PRECISE   = 0x0000004,
    NAV_MESH_NO_JUMP   = 0x0000008,
    NAV_MESH_STOP      = 0x0000010,
    NAV_MESH_RUN       = 0x0000020,
    NAV_MESH_WALK      = 0x0000040,
    NAV_MESH_AVOID     = 0x0000080,
    NAV_MESH_TRANSIENT = 0x0000100,
    NAV_MESH_DONT_HIDE   = 0x0000200,
    NAV_MESH_STAND       = 0x0000400,
    NAV_MESH_NO_HOSTAGES = 0x0000800,
    NAV_MESH_STAIRS      = 0x0001000,
    NAV_MESH_NO_MERGE     = 0x0002000,
    NAV_MESH_OBSTACLE_TOP = 0x0004000,
    NAV_MESH_CLIFF        = 0x0008000,

    NAV_MESH_FIRST_CUSTOM = 0x00010000,
    NAV_MESH_LAST_CUSTOM = 0x04000000,

    NAV_MESH_FUNC_COST     = 0x20000000,
    NAV_MESH_HAS_ELEVATOR = 0x40000000,
    NAV_MESH_NAV_BLOCKER  = 0x80000000,
};

enum TFNavAttributeType
{
    TF_NAV_INVALID = 0x00000000,

    TF_NAV_BLOCKED         = 0x00000001,
    TF_NAV_SPAWN_ROOM_RED  = 0x00000002,
    TF_NAV_SPAWN_ROOM_BLUE = 0x00000004,
    TF_NAV_SPAWN_ROOM_EXIT = 0x00000008,
    TF_NAV_HAS_AMMO        = 0x00000010,
    TF_NAV_HAS_HEALTH      = 0x00000020,
    TF_NAV_CONTROL_POINT   = 0x00000040,

    TF_NAV_BLUE_SENTRY_DANGER = 0x00000080,
    TF_NAV_RED_SENTRY_DANGER  = 0x00000100,

    TF_NAV_BLUE_SETUP_GATE             = 0x00000800,
    TF_NAV_RED_SETUP_GATE              = 0x00001000,
    TF_NAV_BLOCKED_AFTER_POINT_CAPTURE = 0x00002000,
    TF_NAV_BLOCKED_UNTIL_POINT_CAPTURE = 0x00004000,
    TF_NAV_BLUE_ONE_WAY_DOOR           = 0x00008000,
    TF_NAV_RED_ONE_WAY_DOOR            = 0x00010000,

    TF_NAV_WITH_SECOND_POINT = 0x00020000,
    TF_NAV_WITH_THIRD_POINT  = 0x00040000,
    TF_NAV_WITH_FOURTH_POINT = 0x00080000,
    TF_NAV_WITH_FIFTH_POINT  = 0x00100000,

    TF_NAV_SNIPER_SPOT = 0x00200000,
    TF_NAV_SENTRY_SPOT = 0x00400000,

    TF_NAV_ESCAPE_ROUTE         = 0x00800000,
    TF_NAV_ESCAPE_ROUTE_VISIBLE = 0x01000000,

    TF_NAV_NO_SPAWNING = 0x02000000,

    TF_NAV_RESCUE_CLOSET = 0x04000000,

    TF_NAV_BOMB_CAN_DROP_HERE = 0x08000000,

    TF_NAV_DOOR_NEVER_BLOCKS  = 0x10000000,
    TF_NAV_DOOR_ALWAYS_BLOCKS = 0x20000000,

    TF_NAV_UNBLOCKABLE = 0x40000000,

    TF_NAV_PERSISTENT_ATTRIBUTES = TF_NAV_SNIPER_SPOT | TF_NAV_SENTRY_SPOT | TF_NAV_NO_SPAWNING | TF_NAV_BLUE_SETUP_GATE | TF_NAV_RED_SETUP_GATE | TF_NAV_BLOCKED_AFTER_POINT_CAPTURE | TF_NAV_BLOCKED_UNTIL_POINT_CAPTURE | TF_NAV_BLUE_ONE_WAY_DOOR | TF_NAV_RED_ONE_WAY_DOOR | TF_NAV_DOOR_NEVER_BLOCKS | TF_NAV_DOOR_ALWAYS_BLOCKS | TF_NAV_UNBLOCKABLE | TF_NAV_WITH_SECOND_POINT | TF_NAV_WITH_THIRD_POINT | TF_NAV_WITH_FOURTH_POINT | TF_NAV_WITH_FIFTH_POINT | TF_NAV_RESCUE_CLOSET
};

class CNavArea;
struct NavPlace_t
{
public:
	char m_sName[256];
	unsigned short m_uLen;
};

class CHidingSpot
{
public:
	enum
	{
		IN_COVER          = 0x01,
		GOOD_SNIPER_SPOT  = 0x02,
		IDEAL_SNIPER_SPOT = 0x04,
		EXPOSED           = 0x08
	};

	bool HasGoodCover() const
	{
		return (m_fFlags & IN_COVER) != 0;
	}
	bool IsGoodSniperSpot() const
	{
		return (m_fFlags & GOOD_SNIPER_SPOT) != 0;
	}
	bool IsIdealSniperSpot() const
	{
		return (m_fFlags & IDEAL_SNIPER_SPOT) != 0;
	}
	bool IsExposed() const
	{
		return (m_fFlags & EXPOSED) != 0;
	}

	Vector m_vPos;
	unsigned int m_uId;
	unsigned char m_fFlags;
};

struct AreaBindInfo_t
{
	unsigned int m_uId = 0;
	CNavArea* m_pArea = nullptr;

	unsigned char m_uAttributes{};
};

struct NavConnect_t
{
	unsigned int m_uId = 0;
	float m_flLength = -1;
	CNavArea* m_pArea = nullptr;

	bool operator==(const NavConnect_t& tOther) const { return m_pArea == tOther.m_pArea; }
};

struct SpotOrder_t
{
	CHidingSpot* spot;
	float flT;
	unsigned int m_uId;
};

struct SpotEncounter_t
{
	NavConnect_t m_tFrom;
	NavConnect_t m_tTo;
	int m_iFromDir;
	int m_iToDir;

	unsigned char m_uSpotCount;
	std::vector<SpotOrder_t> m_vSpots;
};

class CNavArea
{
public:
	uint32_t m_uId;
	int32_t m_iAttributeFlags;
	int32_t m_iTFAttributeFlags;
	Vector m_vNwCorner;
	Vector m_vSeCorner;
	Vector m_vCenter;
	float m_flInvDxCorners;
	float m_flInvDyCorners;
	float m_flNeZ;
	float m_flSwZ;
	float m_flMinZ;
	float m_flMaxZ;
	std::vector<NavConnect_t> m_vConnections;
	std::vector<NavConnect_t> m_vConnectionsDir[4];
	std::vector<AreaBindInfo_t> m_vPotentiallyVisibleAreas;
	std::vector<SpotEncounter_t> m_vSpotEncounters;
	std::vector<CHidingSpot> m_vHidingSpots;
	std::vector<uint32_t> m_vLadders[2];

	uint32_t m_uConnectionCount;
	uint32_t m_uVisibleAreaCount;
	uint32_t m_uInheritVisibilityFrom;
	uint32_t m_uEncounterSpotCount;
	unsigned char m_uHidingSpotCount;
	uint32_t m_uLadderCount;

	uint16_t m_uIndexType;

	float m_flEarliestOccupyTime[2];
	float m_flLightIntensity[4];

	bool IsBlocked(int iTeam) const
	{
		if (m_iTFAttributeFlags & TF_NAV_UNBLOCKABLE)
			return false;
		if (m_iTFAttributeFlags & TF_NAV_BLOCKED)
			return true;
		if (iTeam == TF_TEAM_RED && (m_iTFAttributeFlags & TF_NAV_BLUE_ONE_WAY_DOOR))
			return true;
		if (iTeam == TF_TEAM_BLUE && (m_iTFAttributeFlags & TF_NAV_RED_ONE_WAY_DOOR))
			return true;
		return false;
	}

	bool IsOverlapping(const Vector& vPos, float flTolerance = 0.0f) const
	{
		if (vPos.x + flTolerance < this->m_vNwCorner.x)
			return false;

		if (vPos.x - flTolerance > this->m_vSeCorner.x)
			return false;

		if (vPos.y + flTolerance < this->m_vNwCorner.y)
			return false;

		if (vPos.y - flTolerance > this->m_vSeCorner.y)
			return false;

		return true;
	}

	bool Contains(const Vector& vPoint) const
	{
		if (!IsOverlapping(vPoint))
			return false;

		if (vPoint.z > m_flMaxZ)
			return false;

		if (vPoint.z < m_flMinZ)
			return false;

		return true;
	}

	inline Vector GetSwCorner() const
	{
		return { m_vNwCorner.x, m_vSeCorner.y, m_flSwZ };
	}
	inline Vector GetNeCorner() const
	{
		return { m_vSeCorner.x, m_vNwCorner.y, m_flNeZ };
	}

	static float FloatSel(float flComparand, float flValGreaterEqual, float flLessThan)
	{
		return flComparand >= 0 ? flValGreaterEqual : flLessThan;
	}

	float GetZ(float x, float y) const
	{
		if (m_flInvDxCorners == 0.0f || m_flInvDyCorners == 0.0f)
			return m_flNeZ;

		float u = (x - m_vNwCorner.x) * m_flInvDxCorners;
		float v = (y - m_vNwCorner.y) * m_flInvDyCorners;

		u = FloatSel(u, u, 0);
		u = FloatSel(u - 1.0f, 1.0f, u);

		v = FloatSel(v, v, 0);
		v = FloatSel(v - 1.0f, 1.0f, v);

		float northZ = m_vNwCorner.z + u * (m_flNeZ - m_vNwCorner.z);
		float southZ = m_flSwZ + u * (m_vSeCorner.z - m_flSwZ);

		return northZ + v * (southZ - northZ);
	}

	Vector GetNearestPoint(const Vector2D vPoint) const
	{
		float x, y, z;

		x = FloatSel(vPoint.x - m_vNwCorner.x, vPoint.x, m_vNwCorner.x);
		x = FloatSel(x - m_vSeCorner.x, m_vSeCorner.x, x);

		y = FloatSel(vPoint.y - m_vNwCorner.y, vPoint.y, m_vNwCorner.y);
		y = FloatSel(y - m_vSeCorner.y, m_vSeCorner.y, y);

		z = GetZ(x, y);

		return Vector(x, y, z);
	}
};
