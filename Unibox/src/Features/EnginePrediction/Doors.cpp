#include "Doors.h"

#include "../Ticks/Ticks.h"
#include <algorithm>
#include <charconv>

namespace
{
	constexpr int DOOR_FRAME_LAG = 1;
	constexpr float DOOR_END_TOLERANCE = 2.f / 32.f;

	bool NearEnd(const Vec3& vLeft, const Vec3& vRight)
	{
		return std::fabs(vLeft.x - vRight.x) <= DOOR_END_TOLERANCE
			&& std::fabs(vLeft.y - vRight.y) <= DOOR_END_TOLERANCE
			&& std::fabs(vLeft.z - vRight.z) <= DOOR_END_TOLERANCE;
	}

	bool BoxesOverlap(const Vec3& vMinsA, const Vec3& vMaxsA, const Vec3& vMinsB, const Vec3& vMaxsB)
	{
		return vMaxsA.x > vMinsB.x && vMinsA.x < vMaxsB.x
			&& vMaxsA.y > vMinsB.y && vMinsA.y < vMaxsB.y
			&& vMaxsA.z > vMinsB.z && vMinsA.z < vMaxsB.z;
	}

	int BrushModel(std::string_view sModel)
	{
		if (sModel.size() < 2 || sModel.front() != '*')
			return -1;

		sModel.remove_prefix(1);
		int iModel = 0;
		const auto [pEnd, iError] = std::from_chars(sModel.data(), sModel.data() + sModel.size(), iModel);
		return iError == std::errc() && pEnd != sModel.data() ? iModel : -1;
	}
}

void CDoorPrediction::SyncMap()
{
	if (m_iMapRevision == static_cast<int>(G::DoorStorageRevision))
		return;

	Restore();
	m_vDoors.clear();
	m_vDoors.reserve(std::min<size_t>(G::DoorStorage.size(), DOOR_MAP_MAX_COUNT));
	for (size_t i = 0; i < G::DoorStorage.size() && i < DOOR_MAP_MAX_COUNT; i++)
	{
		DoorRuntime_t tDoor = {};
		tDoor.m_tMap = G::DoorStorage[i];
		m_vDoors.push_back(std::move(tDoor));
	}

	m_iTriggerCount = std::min<int>(static_cast<int>(G::DoorTriggerStorage.size()), DOOR_MAP_MAX_TRIGGERS);
	for (int i = 0; i < m_iTriggerCount; i++)
		m_aTriggers[i] = G::DoorTriggerStorage[i];

	m_aLocal = {};
	m_aPlayers = {};
	m_iPlayerCount = 0;
	m_iSnapshotTick = -1;
	m_iPreviousTick = -1;
	m_iMapRevision = static_cast<int>(G::DoorStorageRevision);
}

void CDoorPrediction::PostDataUpdate()
{
	if (!I::EngineClient || !I::EngineClient->IsInGame())
		return;

	const int iLocalIndex = I::EngineClient->GetLocalPlayer();
	if (iLocalIndex <= 0 || iLocalIndex >= DOOR_MAX_PLAYERS)
		return;

	IClientEntity* pClientEntity = I::ClientEntityList->GetClientEntity(iLocalIndex);
	CBaseEntity* pLocalEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
	CTFPlayer* pLocal = pLocalEntity && pLocalEntity->GetClassID() == ETFClassID::CTFPlayer
		? pLocalEntity->As<CTFPlayer>() : nullptr;
	if (!pLocal)
		return;

	const int iTick = I::ClientState->m_ClockDriftMgr.m_nServerTick;
	if (iTick == m_iSnapshotTick)
		return;

	SyncMap();
	m_iPreviousTick = m_iSnapshotTick;
	m_iSnapshotTick = iTick;
	m_iLocalIndex = iLocalIndex;
	m_iLocalTeam = pLocal->m_iTeamNum();
	CTFGameRules* pRules = I::TFGameRules();
	m_bTeamWin = pRules && pRules->m_iRoundState() == GR_STATE_TEAM_WIN;

	if (m_vDoors.empty())
		return;

	Bind();
	Observe(iTick);
	ObservePlayers(iTick);
}

void CDoorPrediction::Bind()
{
	const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= iHighest; i++)
	{
		CBaseEntity* pEntity = I::ClientEntityList->GetClientEntity(i) ? I::ClientEntityList->GetClientEntity(i)->As<CBaseEntity>() : nullptr;
		if (!pEntity || pEntity->IsDormant() || pEntity->GetClassID() != ETFClassID::CBaseDoor)
			continue;

		const model_t* pModel = pEntity->GetModel();
		const char* szModel = pModel ? I::ModelInfoClient->GetModelName(pModel) : nullptr;
		if (!szModel)
			continue;

		const int iModel = BrushModel(szModel);
		if (iModel < 0)
			continue;

		int iBestDoor = -1;
		float flBestDistance = FLT_MAX;
		for (size_t iDoor = 0; iDoor < m_vDoors.size(); iDoor++)
		{
			DoorRuntime_t& tDoor = m_vDoors[iDoor];
			if (tDoor.m_tMap.m_iModel != iModel)
				continue;

			CBaseEntity* pBound = tDoor.m_hEntity.Get();
			if (pBound == pEntity)
			{
				iBestDoor = static_cast<int>(iDoor);
				flBestDistance = 0.f;
				break;
			}

			if (pBound && I::ClientEntityList->GetClientEntity(pBound->entindex()) == pBound)
				continue;

			const float flDistance = pEntity->m_vecOrigin().DistToSqr(tDoor.m_tMap.m_vPos1);
			if (flDistance < flBestDistance)
			{
				flBestDistance = flDistance;
				iBestDoor = static_cast<int>(iDoor);
			}
		}

		if (iBestDoor < 0)
			continue;

		DoorRuntime_t& tDoor = m_vDoors[iBestDoor];
		if (tDoor.m_hEntity.Get() != pEntity)
		{
			tDoor.m_hEntity = pEntity;
			tDoor.m_iSnapshotTick = -1;
			tDoor.m_eState = DoorState::Unknown;
		}
	}
}

void CDoorPrediction::Observe(int iTick)
{
	for (DoorRuntime_t& tDoor : m_vDoors)
	{
		CBaseEntity* pEntity = tDoor.m_hEntity.Get();
		if (!pEntity || pEntity->IsDormant())
			continue;

		const float flSimTime = pEntity->m_flSimulationTime();
		if (tDoor.m_iSnapshotTick >= 0 && flSimTime == tDoor.m_flSimTime)
		{
			tDoor.m_iSnapshotTick = iTick;
			continue;
		}

		const Vec3 vReceived = pEntity->m_vecOrigin();
		const Vec3 vPrevious = tDoor.m_iSnapshotTick >= 0 ? tDoor.m_vReceived : vReceived;
		tDoor.m_vReceived = vReceived;
		tDoor.m_flSimTime = flSimTime;
		tDoor.m_iSnapshotTick = iTick;

		if (NearEnd(vReceived, tDoor.m_tMap.m_vPos1))
			tDoor.m_eState = DoorState::Closed;
		else if (NearEnd(vReceived, tDoor.m_tMap.m_vPos2))
			tDoor.m_eState = DoorState::Open;
		else if (vReceived == vPrevious)
			tDoor.m_eState = DoorState::Unknown;
		else
			tDoor.m_eState = (vReceived - vPrevious).Dot(tDoor.m_tMap.m_vDir) > 0.f
				? DoorState::Opening : DoorState::Closing;
	}
}

void CDoorPrediction::ObservePlayers(int iTick)
{
	m_iPlayerCount = 0;
	const int iMaxClients = std::min(I::EngineClient->GetMaxClients(), DOOR_MAX_PLAYERS - 1);
	for (int i = 1; i <= iMaxClients; i++)
	{
		if (i == m_iLocalIndex)
			continue;

		CBaseEntity* pEntity = I::ClientEntityList->GetClientEntity(i) ? I::ClientEntityList->GetClientEntity(i)->As<CBaseEntity>() : nullptr;
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CTFPlayer || pEntity->IsDormant())
			continue;

		CTFPlayer* pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive())
			continue;

		DoorPlayer_t& tRecord = m_aPlayers[i];
		const Vec3 vOrigin = pPlayer->m_vecOrigin();
		const float flSimTime = pPlayer->m_flSimulationTime();
		if (tRecord.m_iSeen != m_iPreviousTick || tRecord.m_aTick[1] < 0)
		{
			tRecord.m_aOrigin = { vOrigin, vOrigin };
			tRecord.m_aTick = { iTick, iTick };
		}
		else if (flSimTime != tRecord.m_flSimTime)
		{
			tRecord.m_aOrigin[0] = tRecord.m_aOrigin[1];
			tRecord.m_aTick[0] = tRecord.m_aTick[1];
			tRecord.m_aOrigin[1] = vOrigin;
			tRecord.m_aTick[1] = iTick;
		}

		tRecord.m_iSeen = iTick;
		tRecord.m_flSimTime = flSimTime;
		tRecord.m_vMins = pPlayer->m_vecMins();
		tRecord.m_vMaxs = pPlayer->m_vecMaxs();
		tRecord.m_iTeam = pPlayer->m_iTeamNum();
		m_aPlayerList[m_iPlayerCount++] = static_cast<uint8_t>(i);
	}
}

void CDoorPrediction::Pose(int iCommand)
{
	SyncMap();
	if (m_vDoors.empty())
		return;

	const int iFrame = F::Ticks.CommandFrame(iCommand);
	if (iFrame < 0)
		return;

	for (DoorRuntime_t& tDoor : m_vDoors)
	{
		CBaseEntity* pEntity = tDoor.m_hEntity.Get();
		if (!pEntity || pEntity->IsDormant() || tDoor.m_iSnapshotTick < 0 || tDoor.m_bPosed)
			continue;

		const Vec3 vPose = PoseAt(tDoor, iFrame - DOOR_FRAME_LAG);
		const Vec3& vCurrent = pEntity->GetAbsOrigin();
		if (vPose == vCurrent)
			continue;

		tDoor.m_vSaved = vCurrent;
		tDoor.m_bPosed = true;
		pEntity->SetAbsOrigin(vPose);
	}
}

void CDoorPrediction::RecordPredicted(int iCommand, CTFPlayer* pPlayer)
{
	if (!pPlayer)
		return;

	const int iFrame = F::Ticks.CommandFrame(iCommand);
	if (iFrame < 0)
		return;

	m_aLocal[static_cast<size_t>(iFrame) % DOOR_FRAME_RING] = {
		pPlayer->m_vecOrigin(),
		pPlayer->m_vecMins(),
		pPlayer->m_vecMaxs(),
		iFrame
	};
}

void CDoorPrediction::Restore()
{
	for (DoorRuntime_t& tDoor : m_vDoors)
	{
		if (!tDoor.m_bPosed)
			continue;

		tDoor.m_bPosed = false;
		if (CBaseEntity* pEntity = tDoor.m_hEntity.Get())
			pEntity->SetAbsOrigin(tDoor.m_vSaved);
	}
}

void CDoorPrediction::Clear()
{
	Restore();
	*this = CDoorPrediction{};
}

Vec3 CDoorPrediction::PoseAt(const DoorRuntime_t& tDoor, int iFrame) const
{
	Vec3 vPosition = tDoor.m_vReceived;
	DoorState eState = tDoor.m_eState;
	const bool bPredicted = tDoor.m_tMap.m_bPredicted && !m_bTeamWin;
	const bool bResting = eState == DoorState::Closed || eState == DoorState::Open;
	if (eState == DoorState::Unknown || (!bPredicted && bResting))
		return vPosition;

	std::array<DoorMembers_t, DOOR_MAP_MAX_LINKS> aBefore = {};
	if (bPredicted)
		SeedMembers(tDoor, eState, aBefore.data());

	for (int i = tDoor.m_iSnapshotTick; i <= iFrame; i++)
	{
		if (i > tDoor.m_iSnapshotTick)
			Step(tDoor, i, vPosition, eState);
		if (bPredicted)
			FireLinks(tDoor, i, aBefore.data(), eState);
	}
	return vPosition;
}

void CDoorPrediction::SeedMembers(const DoorRuntime_t& tDoor, DoorState eState, DoorMembers_t* pBefore) const
{
	if (m_iLocalIndex < 0 || m_iLocalIndex >= DOOR_MAX_PLAYERS)
		return;

	const size_t iLocalWord = static_cast<size_t>(m_iLocalIndex) / 64;
	const uint64_t uLocalBit = uint64_t{ 1 } << (m_iLocalIndex % 64);

	if (eState == DoorState::Closed || eState == DoorState::Closing)
	{
		for (int iLink = 0; iLink < tDoor.m_tMap.m_iLinkCount; iLink++)
		{
			const DoorMapLink_t& tLink = tDoor.m_tMap.m_aLinks[iLink];
			pBefore[iLink] = Members(m_aTriggers[tLink.m_iTrigger], tDoor.m_iSnapshotTick - 1);
			pBefore[iLink].m_aBits[iLocalWord] &= ~uLocalBit;
		}
		return;
	}

	bool bAnyone = false;
	for (int iLink = 0; iLink < tDoor.m_tMap.m_iLinkCount; iLink++)
	{
		const DoorMapTrigger_t& tTrigger = m_aTriggers[tDoor.m_tMap.m_aLinks[iLink].m_iTrigger];
		const DoorMembers_t tEarlier = Members(tTrigger, tDoor.m_iSnapshotTick - 1);
		const DoorMembers_t tCurrent = Members(tTrigger, tDoor.m_iSnapshotTick);
		const DoorMembers_t tOldest = Members(tTrigger, tDoor.m_iSnapshotTick - 2);
		for (size_t iWord = 0; iWord < pBefore[iLink].m_aBits.size(); iWord++)
			pBefore[iLink].m_aBits[iWord] = tEarlier.m_aBits[iWord] | tCurrent.m_aBits[iWord];
		pBefore[iLink].m_aBits[iLocalWord] |= tOldest.m_aBits[iLocalWord] & uLocalBit;
		if (std::any_of(pBefore[iLink].m_aBits.begin(), pBefore[iLink].m_aBits.end(), [](uint64_t uBits) { return uBits != 0; }))
			bAnyone = true;
	}

	if (eState != DoorState::Opening || bAnyone)
		return;

	for (int iLink = 0; iLink < tDoor.m_tMap.m_iLinkCount; iLink++)
	{
		if (tDoor.m_tMap.m_aLinks[iLink].m_bCloses)
		{
			pBefore[iLink].m_aBits[0] |= uint64_t{ 1 };
			return;
		}
	}
}

void CDoorPrediction::Step(const DoorRuntime_t& tDoor, int iFrame, Vec3& vPosition, DoorState& eState) const
{
	if (eState == DoorState::Opening)
	{
		const float flLeft = (tDoor.m_tMap.m_vPos2 - vPosition).Dot(tDoor.m_tMap.m_vDir);
		if (flLeft <= tDoor.m_tMap.m_flStep)
		{
			vPosition = tDoor.m_tMap.m_vPos2;
			eState = DoorState::Open;
			return;
		}
		vPosition += tDoor.m_tMap.m_vDir * tDoor.m_tMap.m_flStep;
		return;
	}

	if (eState != DoorState::Closing)
		return;

	const float flLeft = (vPosition - tDoor.m_tMap.m_vPos1).Dot(tDoor.m_tMap.m_vDir);
	const bool bArrives = flLeft <= tDoor.m_tMap.m_flStep;
	Vec3 vNext = vPosition - tDoor.m_tMap.m_vDir * tDoor.m_tMap.m_flStep;
	if (bArrives)
		vNext = tDoor.m_tMap.m_vPos1;

	if (Blocks(tDoor, vNext, iFrame))
	{
		if (tDoor.m_tMap.m_bReverses)
			eState = DoorState::Opening;
		return;
	}

	vPosition = vNext;
	if (bArrives)
		eState = DoorState::Closed;
}

void CDoorPrediction::FireLinks(const DoorRuntime_t& tDoor, int iFrame, DoorMembers_t* pBefore, DoorState& eState) const
{
	for (int iLink = 0; iLink < tDoor.m_tMap.m_iLinkCount; iLink++)
	{
		const DoorMapLink_t& tInfo = tDoor.m_tMap.m_aLinks[iLink];
		const DoorMembers_t tNow = Members(m_aTriggers[tInfo.m_iTrigger], iFrame);
		DoorMembers_t tEntrants = {};
		for (size_t iWord = 0; iWord < tEntrants.m_aBits.size(); iWord++)
			tEntrants.m_aBits[iWord] = tNow.m_aBits[iWord] & ~pBefore[iLink].m_aBits[iWord];

		const bool bWasOccupied = std::any_of(pBefore[iLink].m_aBits.begin(), pBefore[iLink].m_aBits.end(),
			[](uint64_t uBits) { return uBits != 0; });
		const bool bIsOccupied = std::any_of(tNow.m_aBits.begin(), tNow.m_aBits.end(),
			[](uint64_t uBits) { return uBits != 0; });
		pBefore[iLink] = tNow;

		bool bEntered = std::any_of(tEntrants.m_aBits.begin(), tEntrants.m_aBits.end(),
			[](uint64_t uBits) { return uBits != 0; });
		if (tInfo.m_bAll)
			bEntered = !bWasOccupied && bIsOccupied;

		if (tInfo.m_bOpens && bEntered && (eState == DoorState::Closed || eState == DoorState::Closing))
		{
			eState = DoorState::Opening;
			continue;
		}

		if (tInfo.m_bCloses && bWasOccupied && !bIsOccupied
			&& (eState == DoorState::Open || eState == DoorState::Opening))
			eState = DoorState::Closing;
	}
}

CDoorPrediction::DoorMembers_t CDoorPrediction::Members(const DoorMapTrigger_t& tTrigger, int iFrame) const
{
	DoorMembers_t tInside = {};
	DoorLocal_t tLocal = {};
	IClientEntity* pClientEntity = m_iLocalIndex > 0 && m_iLocalIndex < DOOR_MAX_PLAYERS
		? I::ClientEntityList->GetClientEntity(m_iLocalIndex) : nullptr;
	CBaseEntity* pLocalEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
	CTFPlayer* pLocal = pLocalEntity && pLocalEntity->GetClassID() == ETFClassID::CTFPlayer
		? pLocalEntity->As<CTFPlayer>() : nullptr;
	if ((!tTrigger.m_iTeam || tTrigger.m_iTeam == m_iLocalTeam) && pLocal && pLocal->IsAlive()
		&& m_iLocalIndex >= 0 && m_iLocalIndex < DOOR_MAX_PLAYERS && LocalAt(iFrame, tLocal)
		&& BoxesOverlap(tLocal.m_vOrigin + tLocal.m_vMins, tLocal.m_vOrigin + tLocal.m_vMaxs,
			tTrigger.m_vMins, tTrigger.m_vMaxs))
		tInside.m_aBits[static_cast<size_t>(m_iLocalIndex) / 64] |= uint64_t{ 1 } << (m_iLocalIndex % 64);

	for (int i = 0; i < m_iPlayerCount; i++)
	{
		const uint8_t uIndex = m_aPlayerList[i];
		const DoorPlayer_t& tPlayer = m_aPlayers[uIndex];
		if (tTrigger.m_iTeam && tTrigger.m_iTeam != tPlayer.m_iTeam)
			continue;

		const Vec3 vOrigin = PlayerAt(tPlayer, iFrame);
		if (BoxesOverlap(vOrigin + tPlayer.m_vMins, vOrigin + tPlayer.m_vMaxs, tTrigger.m_vMins, tTrigger.m_vMaxs))
			tInside.m_aBits[uIndex / 64] |= uint64_t{ 1 } << (uIndex % 64);
	}
	return tInside;
}

bool CDoorPrediction::Blocks(const DoorRuntime_t& tDoor, const Vec3& vPosition, int iFrame) const
{
	const Vec3 vMins = vPosition + tDoor.m_tMap.m_vMins;
	const Vec3 vMaxs = vPosition + tDoor.m_tMap.m_vMaxs;

	DoorLocal_t tLocal = {};
	IClientEntity* pClientEntity = m_iLocalIndex > 0 && m_iLocalIndex < DOOR_MAX_PLAYERS
		? I::ClientEntityList->GetClientEntity(m_iLocalIndex) : nullptr;
	CBaseEntity* pLocalEntity = pClientEntity ? pClientEntity->As<CBaseEntity>() : nullptr;
	CTFPlayer* pLocal = pLocalEntity && pLocalEntity->GetClassID() == ETFClassID::CTFPlayer
		? pLocalEntity->As<CTFPlayer>() : nullptr;
	if (pLocal && pLocal->IsAlive() && LocalAt(iFrame, tLocal)
		&& BoxesOverlap(tLocal.m_vOrigin + tLocal.m_vMins, tLocal.m_vOrigin + tLocal.m_vMaxs, vMins, vMaxs))
		return true;

	for (int i = 0; i < m_iPlayerCount; i++)
	{
		const DoorPlayer_t& tPlayer = m_aPlayers[m_aPlayerList[i]];
		const Vec3 vOrigin = PlayerAt(tPlayer, iFrame);
		if (BoxesOverlap(vOrigin + tPlayer.m_vMins, vOrigin + tPlayer.m_vMaxs, vMins, vMaxs))
			return true;
	}
	return false;
}

bool CDoorPrediction::LocalAt(int iFrame, DoorLocal_t& tOut) const
{
	if (iFrame < 0)
		return false;

	for (int i = iFrame; i > iFrame - MULTIPLAYER_BACKUP; i--)
	{
		const DoorLocal_t& tSlot = m_aLocal[static_cast<size_t>(i) % DOOR_FRAME_RING];
		if (tSlot.m_iFrame == i)
		{
			tOut = tSlot;
			return true;
		}
		if (tSlot.m_iFrame > i)
			return false;
	}
	return false;
}

Vec3 CDoorPrediction::PlayerAt(const DoorPlayer_t& tPlayer, int iFrame) const
{
	const int iSpan = tPlayer.m_aTick[1] - tPlayer.m_aTick[0];
	if (iSpan <= 0)
		return tPlayer.m_aOrigin[1];

	const Vec3 vVelocity = (tPlayer.m_aOrigin[1] - tPlayer.m_aOrigin[0]) / static_cast<float>(iSpan);
	static ConVar* pMaxVelocity = H::ConVars.FindVar("sv_maxvelocity");
	const float flMaxVelocity = pMaxVelocity ? pMaxVelocity->GetFloat() : 3500.f;
	if (vVelocity.Length() > flMaxVelocity * TICK_INTERVAL)
		return tPlayer.m_aOrigin[1];
	return tPlayer.m_aOrigin[1] + vVelocity * static_cast<float>(iFrame - tPlayer.m_aTick[1]);
}
