#pragma once
#include "../../SDK/SDK.h"
#include <array>

inline constexpr int DOOR_MAX_PLAYERS = 128;
inline constexpr int DOOR_FRAME_RING = 128;

class CDoorPrediction
{
private:
	enum class DoorState : uint8_t
	{
		Closed,
		Opening,
		Open,
		Closing,
		Unknown
	};

	struct DoorMembers_t
	{
		std::array<uint64_t, DOOR_MAX_PLAYERS / 64> m_aBits = {};
	};

	struct DoorRuntime_t
	{
		DoorMapData_t m_tMap = {};
		CHandle<CBaseEntity> m_hEntity = {};
		int m_iSnapshotTick = -1;
		float m_flSimTime = 0.f;
		Vec3 m_vReceived = {};
		DoorState m_eState = DoorState::Unknown;
		Vec3 m_vSaved = {};
		bool m_bPosed = false;
	};

	struct DoorPlayer_t
	{
		std::array<Vec3, 2> m_aOrigin = {};
		std::array<int, 2> m_aTick = { -1, -1 };
		int m_iSeen = -1;
		float m_flSimTime = 0.f;
		Vec3 m_vMins = {};
		Vec3 m_vMaxs = {};
		int m_iTeam = 0;
	};

	struct DoorLocal_t
	{
		Vec3 m_vOrigin = {};
		Vec3 m_vMins = {};
		Vec3 m_vMaxs = {};
		int m_iFrame = -1;
	};

	void SyncMap();
	void Bind();
	void Observe(int iTick);
	void ObservePlayers(int iTick);

	Vec3 PoseAt(const DoorRuntime_t& tDoor, int iFrame) const;
	void SeedMembers(const DoorRuntime_t& tDoor, DoorState eState, DoorMembers_t* pBefore) const;
	void Step(const DoorRuntime_t& tDoor, int iFrame, Vec3& vPosition, DoorState& eState) const;
	void FireLinks(const DoorRuntime_t& tDoor, int iFrame, DoorMembers_t* pBefore, DoorState& eState) const;
	DoorMembers_t Members(const DoorMapTrigger_t& tTrigger, int iFrame) const;
	bool Blocks(const DoorRuntime_t& tDoor, const Vec3& vPosition, int iFrame) const;
	bool LocalAt(int iFrame, DoorLocal_t& tOut) const;
	Vec3 PlayerAt(const DoorPlayer_t& tPlayer, int iFrame) const;

	std::vector<DoorRuntime_t> m_vDoors = {};
	std::array<DoorMapTrigger_t, DOOR_MAP_MAX_TRIGGERS> m_aTriggers = {};
	int m_iTriggerCount = 0;
	std::array<DoorPlayer_t, DOOR_MAX_PLAYERS> m_aPlayers = {};
	std::array<uint8_t, DOOR_MAX_PLAYERS> m_aPlayerList = {};
	int m_iPlayerCount = 0;
	std::array<DoorLocal_t, DOOR_FRAME_RING> m_aLocal = {};
	int m_iLocalIndex = -1;
	int m_iLocalTeam = 0;
	int m_iSnapshotTick = -1;
	int m_iPreviousTick = -1;
	int m_iMapRevision = -1;
	bool m_bTeamWin = false;

public:
	void PostDataUpdate();
	void Pose(int iCommand);
	void RecordPredicted(int iCommand, CTFPlayer* pPlayer);
	void Restore();
	void Clear();
};

ADD_FEATURE(CDoorPrediction, Doors);
