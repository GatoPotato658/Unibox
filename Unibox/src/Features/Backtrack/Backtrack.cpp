#include "Backtrack.h"

#include "../PacketManip/FakeLag/FakeLag.h"
#include "../Ticks/Ticks.h"
#include "../Aimbot/Aimbot.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../ImGui/IndicatorPanel.h"
#include "../Visuals/Groups/Groups.h"
#include "../Resolver/Resolver.h"

void CBacktrack::Reset()
{
	for (auto& tTrack : m_aTracks)
		tTrack.Reset();
	m_mExtraTracks.clear();
	m_mDidShoot.clear();
	m_tLatency = {};
	m_flMaxUnlag = 1.f;
	m_flFakeLatency = 0.f;
	m_flFakeInterp = 0.015f;
	m_flKeepWindow = 1.f;
	m_bAnyConsumer = true;
	m_bSettingUpBones = false;
	m_iTickCount = 0;
	m_flSentInterp = -1.f;
	m_tRecord = {};
	m_sLastPingText.clear();
	m_sLastScoreboardText.clear();
	m_bLastDrawValid = false;
}

PlayerTrack_t* CBacktrack::FindTrack(CBaseEntity* pEntity)
{
	if (!pEntity)
		return nullptr;

	const int iIndex = pEntity->entindex();
	if (iIndex >= 0 && iIndex < MAX_TRACK_PLAYERS)
	{
		auto& tTrack = m_aTracks[iIndex];
		return tTrack.m_pEntity == pEntity ? &tTrack : nullptr;
	}

	auto it = m_mExtraTracks.find(pEntity);
	return it != m_mExtraTracks.end() ? &it->second : nullptr;
}

PlayerTrack_t& CBacktrack::GetOrCreateTrack(CBaseEntity* pEntity)
{
	const int iIndex = pEntity->entindex();
	if (iIndex >= 0 && iIndex < MAX_TRACK_PLAYERS)
	{
		auto& tTrack = m_aTracks[iIndex];
		if (tTrack.m_pEntity != pEntity)
		{
			tTrack.Clear();
			tTrack.m_pEntity = pEntity;
			tTrack.m_iEntIndex = iIndex;
		}
		return tTrack;
	}

	auto& tTrack = m_mExtraTracks[pEntity];
	tTrack.m_pEntity = pEntity;
	tTrack.m_iEntIndex = iIndex;
	return tTrack;
}

float CBacktrack::GetReal(int iFlow, bool bNoFake)
{
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return 0.f;

	if (iFlow != MAX_FLOWS)
		return pNetChan->GetLatency(iFlow) - (bNoFake && iFlow == FLOW_INCOMING ? GetFakeLatency() : 0.f);
	return pNetChan->GetLatency(FLOW_INCOMING) + pNetChan->GetLatency(FLOW_OUTGOING) - (bNoFake ? GetFakeLatency() : 0.f);
}

float CBacktrack::GetWishFake()
{
	return std::clamp(Vars::Backtrack::Latency.Value / 1000.f, 0.f, m_flMaxUnlag);
}

float CBacktrack::GetWishLerp()
{
	if (F::AntiCheatCompatibility.Active())
		return std::clamp(Vars::Backtrack::Interp.Value / 1000.f, G::Lerp, 0.1f);

	return std::clamp(Vars::Backtrack::Interp.Value / 1000.f, G::Lerp, m_flMaxUnlag);
}

float CBacktrack::GetFakeLatency()
{
	return m_flFakeLatency;
}

float CBacktrack::GetFakeInterp()
{
	if (F::AntiCheatCompatibility.Active())
		return std::min(m_flFakeInterp, 0.1f);

	return m_flFakeInterp;
}

float CBacktrack::GetWindow()
{
	return Vars::Backtrack::Window.Value / 1000.f;
}

int CBacktrack::GetAnticipatedChoke(int iMethod)
{
	int iAnticipatedChoke = 0;
	if (F::Ticks.CanChoke() && G::PrimaryWeaponType != EWeaponType::HITSCAN && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
		iAnticipatedChoke = 1;
	if (F::FakeLag.m_iGoal && !Vars::Fakelag::UnchokeOnAttack.Value && F::Ticks.m_iShiftedTicks == F::Ticks.m_iShiftedGoal && !F::Ticks.m_bDoubletap)
		iAnticipatedChoke = F::FakeLag.m_iGoal - I::ClientState->chokedcommands;
	return iAnticipatedChoke;
}

int CBacktrack::GetServerTick()
{
	return m_iTickCount + TIME_TO_TICKS(GetReal(FLOW_OUTGOING)) + GetAnticipatedChoke();
}

int CBacktrack::GetRewindTick(float flTimeMod)
{
	float flCorrect = std::clamp(GetReal(MAX_FLOWS, false) + ROUND_TO_TICKS(GetFakeInterp()), 0.f, m_flMaxUnlag) + flTimeMod;
	return GetServerTick() + Vars::Backtrack::Offset.Value - TIME_TO_TICKS(flCorrect);
}

void CBacktrack::CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (F::AntiCheatCompatibility.Active())
		return;

	pCmd->tick_count += TIME_TO_TICKS(GetFakeInterp());
	if (!Vars::Visuals::Removals::Lerp.Value && !Vars::Visuals::Removals::Interpolation.Value)
		pCmd->tick_count -= TIME_TO_TICKS(G::Lerp);
}

void CBacktrack::SendLerp()
{
	static Timer tTimer = {};
	if (!tTimer.Run(0.1f))
		return;

	float flTarget = GetWishLerp();
	if (m_flSentInterp != flTarget)
	{
		m_flSentInterp = flTarget;

		auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
		if (pNetChan && I::EngineClient->IsConnected())
		{
			NET_SetConVar tConvar1 = { "cl_interp", std::to_string(m_flSentInterp).c_str() };
			pNetChan->SendNetMsg(tConvar1);

			NET_SetConVar tConvar2 = { "cl_interp_ratio", "1" };
			pNetChan->SendNetMsg(tConvar2);

			NET_SetConVar tConvar3 = { "cl_interpolate", "1" };
			pNetChan->SendNetMsg(tConvar3);
		}
	}
}

void CBacktrack::SetLerp()
{
	m_flFakeInterp = m_flSentInterp;
}

bool CBacktrack::GetRecords(CBaseEntity* pEntity, std::vector<TickRecord*>& vReturn)
{
	vReturn.clear();
	auto pTrack = FindTrack(pEntity);
	if (!pTrack || pTrack->Empty())
		return false;

	vReturn.reserve(pTrack->Size());
	for (int i = 0; i < pTrack->Size(); i++)
		vReturn.push_back(&pTrack->At(i));
	return true;
}

void CBacktrack::GetValidRecords(const std::vector<TickRecord*>& vRecords, std::vector<TickRecord*>& vReturn, CTFPlayer* pLocal, bool bDistance, float flTimeMod)
{
	vReturn.clear();
	if (vRecords.empty())
		return;

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return;

	const float flCorrect = std::clamp(GetReal(MAX_FLOWS, false) + ROUND_TO_TICKS(GetFakeInterp()), 0.f, m_flMaxUnlag) + flTimeMod;
	const int iServerTick = GetServerTick() + Vars::Backtrack::Offset.Value;
	const float flWindow = GetWindow();

	if (!F::AntiCheatCompatibility.Active() && flWindow)
	{
		vReturn.reserve(vRecords.size());
		for (auto pRecord : vRecords)
		{
			if (pRecord->m_bInvalid)
				continue;

			const float flDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(pRecord->m_flSimTime));
			if (fabsf(flDelta) > flWindow)
				continue;

			vReturn.push_back(pRecord);
		}
	}

	if (vReturn.empty())
	{
		TickRecord* pBest = nullptr;
		float flMinDelta = 0.2f;
		for (auto pRecord : vRecords)
		{
			if (pRecord->m_bInvalid)
				continue;

			const float flDelta = fabsf(flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(pRecord->m_flSimTime)));
			if (flDelta > flMinDelta)
				continue;

			flMinDelta = flDelta;
			pBest = pRecord;
		}
		if (pBest)
			vReturn.push_back(pBest);
	}
	else if (pLocal && vReturn.size() > 1)
	{
		const bool bPreferOnShot = Vars::Backtrack::PreferOnShot.Value;
		if (bDistance)
		{
			const Vec3 vLocalOrigin = pLocal->m_vecOrigin();
			std::sort(vReturn.begin(), vReturn.end(), [bPreferOnShot, vLocalOrigin](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (bPreferOnShot && a->m_bOnShot != b->m_bOnShot)
					return a->m_bOnShot > b->m_bOnShot;

				return vLocalOrigin.DistToSqr(a->m_vOrigin) < vLocalOrigin.DistToSqr(b->m_vOrigin);
			});
		}
		else
		{
			std::sort(vReturn.begin(), vReturn.end(), [bPreferOnShot, flCorrect, iServerTick](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (bPreferOnShot && a->m_bOnShot != b->m_bOnShot)
					return a->m_bOnShot > b->m_bOnShot;

				const float flADelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(a->m_flSimTime));
				const float flBDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(b->m_flSimTime));
				return fabsf(flADelta) < fabsf(flBDelta);
			});
		}
	}
}

void CBacktrack::GetValidRecords(CBaseEntity* pEntity, std::vector<TickRecord*>& vReturn, CTFPlayer* pLocal, bool bDistance, float flTimeMod)
{
	vReturn.clear();
	auto pTrack = FindTrack(pEntity);
	if (!pTrack || pTrack->Empty())
		return;

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return;

	const float flCorrect = std::clamp(GetReal(MAX_FLOWS, false) + ROUND_TO_TICKS(GetFakeInterp()), 0.f, m_flMaxUnlag) + flTimeMod;
	const int iServerTick = GetServerTick() + Vars::Backtrack::Offset.Value;
	const float flWindow = GetWindow();
	const int nCount = pTrack->Size();

	if (!F::AntiCheatCompatibility.Active() && flWindow)
	{
		vReturn.reserve(nCount);
		for (int i = 0; i < nCount; i++)
		{
			auto& tRecord = pTrack->At(i);
			if (tRecord.m_bInvalid)
				continue;

			const float flDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(tRecord.m_flSimTime));
			if (fabsf(flDelta) > flWindow)
				continue;

			vReturn.push_back(&tRecord);
		}
	}

	if (vReturn.empty())
	{
		TickRecord* pBest = nullptr;
		float flMinDelta = 0.2f;
		for (int i = 0; i < nCount; i++)
		{
			auto& tRecord = pTrack->At(i);
			if (tRecord.m_bInvalid)
				continue;

			const float flDelta = fabsf(flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(tRecord.m_flSimTime)));
			if (flDelta > flMinDelta)
				continue;

			flMinDelta = flDelta;
			pBest = &tRecord;
		}
		if (pBest)
			vReturn.push_back(pBest);
	}
	else if (pLocal && vReturn.size() > 1)
	{
		const bool bPreferOnShot = Vars::Backtrack::PreferOnShot.Value;
		if (bDistance)
		{
			const Vec3 vLocalOrigin = pLocal->m_vecOrigin();
			std::sort(vReturn.begin(), vReturn.end(), [bPreferOnShot, vLocalOrigin](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (bPreferOnShot && a->m_bOnShot != b->m_bOnShot)
					return a->m_bOnShot > b->m_bOnShot;

				return vLocalOrigin.DistToSqr(a->m_vOrigin) < vLocalOrigin.DistToSqr(b->m_vOrigin);
			});
		}
		else
		{
			std::sort(vReturn.begin(), vReturn.end(), [bPreferOnShot, flCorrect, iServerTick](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (bPreferOnShot && a->m_bOnShot != b->m_bOnShot)
					return a->m_bOnShot > b->m_bOnShot;

				const float flADelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(a->m_flSimTime));
				const float flBDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(b->m_flSimTime));
				return fabsf(flADelta) < fabsf(flBDelta);
			});
		}
	}
}

std::vector<TickRecord*> CBacktrack::GetValidRecords(std::vector<TickRecord*>& vRecords, CTFPlayer* pLocal, bool bDistance, float flTimeMod)
{
	std::vector<TickRecord*> vReturn = {};
	GetValidRecords(static_cast<const std::vector<TickRecord*>&>(vRecords), vReturn, pLocal, bDistance, flTimeMod);
	return vReturn;
}

TickRecord* CBacktrack::GetFirstValidRecord(CBaseEntity* pEntity)
{
	auto pTrack = FindTrack(pEntity);
	if (!pTrack || pTrack->Empty())
		return nullptr;

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return nullptr;

	const float flCorrect = std::clamp(GetReal(MAX_FLOWS, false) + ROUND_TO_TICKS(GetFakeInterp()), 0.f, m_flMaxUnlag);
	const int iServerTick = GetServerTick() + Vars::Backtrack::Offset.Value;
	const int nCount = pTrack->Size();

	if (!F::AntiCheatCompatibility.Active() && GetWindow())
	{
		const float flWindow = GetWindow();
		for (int i = 0; i < nCount; i++)
		{
			auto& tRecord = pTrack->At(i);
			if (tRecord.m_bInvalid)
				continue;

			const float flDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(tRecord.m_flSimTime));
			if (fabsf(flDelta) > flWindow)
				continue;

			return &tRecord;
		}
	}

	TickRecord* pBest = nullptr;
	float flMinDelta = 0.2f;
	for (int i = 0; i < nCount; i++)
	{
		auto& tRecord = pTrack->At(i);
		if (tRecord.m_bInvalid)
			continue;

		const float flDelta = fabsf(flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(tRecord.m_flSimTime)));
		if (flDelta > flMinDelta)
			continue;

		flMinDelta = flDelta;
		pBest = &tRecord;
	}

	return pBest;
}

TickRecord* CBacktrack::GetLatestRecord(CBaseEntity* pEntity)
{
	auto pTrack = FindTrack(pEntity);
	if (!pTrack || pTrack->Empty())
		return nullptr;

	return &pTrack->Front();
}

static bool SetupFallbackHitboxBones(CBaseEntity* pEntity, TickRecord& tRecord)
{
	if (!pEntity || !pEntity->IsPlayer() || pEntity->IsDormant())
		return false;
	auto pPlayer = pEntity->As<CTFPlayer>();
	if (!pPlayer->IsAlive() || pPlayer->IsAGhost())
		return false;

	const Vec3 vOldAbsOrigin = pPlayer->GetAbsOrigin();
	const Vec3 vOldAbsAngles = pPlayer->GetAbsAngles();
	pPlayer->SetAbsOrigin(pPlayer->m_vecOrigin());
	pPlayer->SetAbsAngles(pPlayer->m_angRotation());
	pPlayer->InvalidateBoneCache();

	F::Backtrack.SetSettingUpBones(true);
	const bool bSetup = pPlayer->SetupBones(tRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, pPlayer->m_flSimulationTime());
	F::Backtrack.SetSettingUpBones(false);

	pPlayer->SetAbsOrigin(vOldAbsOrigin);
	pPlayer->SetAbsAngles(vOldAbsAngles);
	pPlayer->InvalidateBoneCache();

	return bSetup;
}

matrix3x4* CBacktrack::GetBones(CBaseEntity* pEntity)
{
	if (auto pTrack = FindTrack(pEntity); pTrack && !pTrack->Empty())
		return pTrack->Front().m_aBones;

	return SetupFallbackHitboxBones(pEntity, m_tRecord) ? m_tRecord.m_aBones : nullptr;
}

matrix3x4* CBacktrack::GetBones(CBaseEntity* pEntity, float flOldCurrentTime, float flLerp)
{
	auto pTrack = FindTrack(pEntity);
	if (!pTrack || pTrack->Empty())
		return SetupFallbackHitboxBones(pEntity, m_tRecord) ? m_tRecord.m_aBones : nullptr;

	for (int i = 0; i < pTrack->Size(); i++)
	{
		auto& tRecord = pTrack->At(i);
		if (tRecord.m_bInvalid)
			continue;
		if (flOldCurrentTime - tRecord.m_flSimTime > flLerp)
			return tRecord.m_aBones;
	}
	return pTrack->Front().m_aBones;
}

void CBacktrack::MakeRecords()
{
	if (!m_bAnyConsumer)
		return;

	bool bBacktrackVisuals = false;
	bool bTeamBacktrackVisuals = false;
	for (const auto& tGroup : F::Groups.m_vGroups)
	{
		if (!(tGroup.m_iTargets & TargetsEnum::Players) || !(tGroup.m_iBacktrack & BacktrackEnum::Enabled))
			continue;
		if (!tGroup.m_tBacktrackChams(false) && !tGroup.m_tBacktrackGlow())
			continue;
		bBacktrackVisuals = true;
		if ((tGroup.m_iConditions & (ConditionsEnum::Team | ConditionsEnum::BLU | ConditionsEnum::RED))
			|| !(tGroup.m_iConditions & ConditionsEnum::Enemy))
			bTeamBacktrackVisuals = true;
	}

	auto pLocal = H::Entities.GetLocal();
	const int iLocalIdx = I::EngineClient->GetLocalPlayer();
	const int iLocalTeam = pLocal ? pLocal->m_iTeamNum() : -1;
	const bool bNeedTeam = !pLocal || SDK::FriendlyFire() || bTeamBacktrackVisuals
		|| Vars::Aimbot::Healing::AutoHeal.Value
		|| pLocal->m_iClass() == TF_CLASS_MEDIC
		|| (Vars::Aimbot::Melee::WhipTeam.Value && (G::PrimaryWeaponType == EWeaponType::MELEE || G::SecondaryWeaponType == EWeaponType::MELEE))
		|| (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::ExtinguishTeam);

	const int iBoneMask = bBacktrackVisuals ? (BONE_USED_BY_HITBOX | BONE_USED_BY_VERTEX_MASK) : BONE_USED_BY_HITBOX;
	const int iDesiredCap = std::max(TIME_TO_TICKS(m_flKeepWindow) + 4, 4);

	static auto sv_lagcompensation_teleport_dist = H::ConVars.FindVar("sv_lagcompensation_teleport_dist");
	const float flTeleportDistSqr = powf(sv_lagcompensation_teleport_dist->GetFloat(), 2.f);

	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->entindex() == iLocalIdx ||
			pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() ||
			!H::Entities.GetDeltaTime(pPlayer->entindex()))
			continue;

		if (!bNeedTeam && pPlayer->m_iTeamNum() == iLocalTeam)
			continue;

		auto& tTrack = GetOrCreateTrack(pPlayer);
		const float flSimTime = pPlayer->m_flSimulationTime();
		if (!tTrack.Empty() && tTrack.Front().m_flSimTime >= flSimTime)
		{
			if ((pPlayer->m_vecOrigin() - tTrack.Front().m_vOrigin).Length2DSqr() > flTeleportDistSqr)
			{
				tTrack.Clear();
				H::Entities.SetLagCompensation(pPlayer->entindex(), true);
			}
			continue;
		}

		const bool bHasLastRecord = !tTrack.Empty();
		const Vec3 vLastOrigin = bHasLastRecord ? tTrack.Front().m_vOrigin : Vec3();

		TickRecord& tCurRecord = tTrack.PushFrontSlot(iDesiredCap);

		const Vec3 vOldAbsOrigin = pPlayer->GetAbsOrigin();
		const Vec3 vOldAbsAngles = pPlayer->GetAbsAngles();
		pPlayer->SetAbsOrigin(pPlayer->m_vecOrigin());
		pPlayer->SetAbsAngles(pPlayer->m_angRotation());
		pPlayer->InvalidateBoneCache();

		m_bSettingUpBones = true;
		bool bSetup = pPlayer->SetupBones(tCurRecord.m_aBones, MAXSTUDIOBONES, iBoneMask, flSimTime);
		if (bSetup && pPlayer->UsesAimStudio())
		{
			if (!tCurRecord.m_pRenderBones || tCurRecord.m_pRenderBones.use_count() != 1)
				tCurRecord.m_pRenderBones = std::make_shared<std::array<matrix3x4, MAXSTUDIOBONES>>();
			memcpy(tCurRecord.m_pRenderBones->data(), tCurRecord.m_aBones, sizeof(tCurRecord.m_aBones));
			bSetup = pPlayer->SetupAimBones(tCurRecord.m_aBones, MAXSTUDIOBONES, iBoneMask, flSimTime);
		}
		else
			tCurRecord.m_pRenderBones.reset();
		m_bSettingUpBones = false;

		pPlayer->SetAbsOrigin(vOldAbsOrigin);
		pPlayer->SetAbsAngles(vOldAbsAngles);
		pPlayer->InvalidateBoneCache();

		if (!bSetup)
		{
			tTrack.PopFront();
			continue;
		}

		tCurRecord.m_flSimTime = flSimTime;
		tCurRecord.m_vOrigin = pPlayer->m_vecOrigin();
		tCurRecord.m_vMins = pPlayer->m_vecMins();
		tCurRecord.m_vMaxs = pPlayer->m_vecMaxs();
		tCurRecord.m_bOnShot = m_mDidShoot[pPlayer->entindex()];
		tCurRecord.m_bInvalid = false;
		tCurRecord.m_vVelocity = pPlayer->m_vecVelocity();
		tCurRecord.m_flModelScale = pPlayer->m_flModelScale();

		bool bLagComp = false;
		if (bHasLastRecord)
		{
			const Vec3 vDelta = tCurRecord.m_vOrigin - vLastOrigin;
			if (vDelta.Length2DSqr() > flTeleportDistSqr)
			{
				bLagComp = true;
				if (!H::Entities.GetLagCompensation(pPlayer->entindex()))
					tTrack.Resize(1);
				else
				{
					for (int i = 1; i < tTrack.Size(); i++)
						tTrack.At(i).m_bInvalid = true;
				}
			}
		}

		H::Entities.SetLagCompensation(pPlayer->entindex(), bLagComp);
		m_mDidShoot[pPlayer->entindex()] = false;
	}
}

void CBacktrack::CleanRecords()
{
	const float flDeadtime = TICKS_TO_TIME(GetServerTick()) - m_flKeepWindow;
	const int iLocalIdx = I::EngineClient->GetLocalPlayer();

	auto fCleanTrack = [&](PlayerTrack_t& tTrack)
	{
		if (!tTrack.m_pEntity || tTrack.m_iEntIndex <= 0)
		{
			tTrack.Clear();
			tTrack.m_pEntity = nullptr;
			tTrack.m_iEntIndex = -1;
			return;
		}

		auto pEntity = I::ClientEntityList->GetClientEntity(tTrack.m_iEntIndex);
		if (!pEntity || pEntity->As<CBaseEntity>() != tTrack.m_pEntity || tTrack.m_iEntIndex == iLocalIdx || !m_bAnyConsumer)
		{
			tTrack.Clear();
			tTrack.m_pEntity = nullptr;
			tTrack.m_iEntIndex = -1;
			return;
		}

		auto pPlayer = tTrack.m_pEntity->As<CTFPlayer>();
		if (!pPlayer->IsPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
		{
			tTrack.Clear();
			return;
		}

		if (tTrack.Size() > 1 && tTrack.Back().m_flSimTime == std::numeric_limits<float>::max())
			tTrack.PopBack();
		while (tTrack.Size() > 1)
		{
			const float flTailTime = tTrack.Back().m_flSimTime;
			if (flTailTime < flDeadtime || flTailTime == std::numeric_limits<float>::max())
				tTrack.PopBack();
			else
				break;
		}
	};

	for (auto& tTrack : m_aTracks)
	{
		if (tTrack.m_pEntity || !tTrack.Empty())
			fCleanTrack(tTrack);
	}
	for (auto it = m_mExtraTracks.begin(); it != m_mExtraTracks.end();)
	{
		fCleanTrack(it->second);
		if (!it->second.m_pEntity)
			it = m_mExtraTracks.erase(it);
		else
			++it;
	}
}

void CBacktrack::Store()
{
	if (!I::EngineClient->IsInGame())
		return;

	static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
	m_flMaxUnlag = sv_maxunlag->GetFloat();

	bool bBacktrackVisuals = false;
	for (const auto& tGroup : F::Groups.m_vGroups)
	{
		if ((tGroup.m_iTargets & TargetsEnum::Players) && (tGroup.m_iBacktrack & BacktrackEnum::Enabled)
			&& (tGroup.m_tBacktrackChams(false) || tGroup.m_tBacktrackGlow()))
		{
			bBacktrackVisuals = true;
			break;
		}
	}

	auto pLocal = H::Entities.GetLocal();
	const bool bNeedHistory = bBacktrackVisuals
		|| Vars::Backtrack::Window.Value > 0
		|| Vars::Backtrack::Latency.Value > 0
		|| Vars::Backtrack::Interp.Value > 0
		|| Vars::Aimbot::Healing::AutoHeal.Value
		|| (pLocal && pLocal->m_iClass() == TF_CLASS_MEDIC)
		|| G::PrimaryWeaponType == EWeaponType::HITSCAN
		|| G::PrimaryWeaponType == EWeaponType::MELEE
		|| G::SecondaryWeaponType == EWeaponType::HITSCAN
		|| G::SecondaryWeaponType == EWeaponType::MELEE
		|| (Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::OnHit);

	m_bAnyConsumer = bNeedHistory || Vars::Aimbot::General::AimType.Value != 0 || Vars::Debug::DrawHitboxes.Value;

	if (bNeedHistory)
		m_flKeepWindow = std::min(m_flMaxUnlag, std::max(GetReal(MAX_FLOWS, false) + ROUND_TO_TICKS(GetFakeInterp()) + std::max(GetWindow(), 0.2f), G::Lerp) + 4.f * TICK_INTERVAL);
	else
		m_flKeepWindow = std::min(m_flMaxUnlag, std::max(G::Lerp + 4.f * TICK_INTERVAL, 0.1f));

	MakeRecords();
	CleanRecords();
}

void CBacktrack::ResolverUpdate(CBaseEntity* pEntity)
{
	if (!pEntity || !pEntity->IsPlayer())
		return;

	auto pPlayer = pEntity->As<CTFPlayer>();
	auto* pTrack = FindTrack(pPlayer);
	if (!pTrack || pTrack->Empty())
		return;

	TickRecord& tFront = pTrack->Front();

	float flYaw = pPlayer->m_angEyeAnglesY();
	float flPitch = pPlayer->m_angEyeAnglesX();
	const bool bResolved = F::Resolver.GetAngles(pPlayer, &flYaw, &flPitch, nullptr, false);
	const float flOldYaw = pPlayer->m_angEyeAnglesY();
	const float flOldPitch = pPlayer->m_angEyeAnglesX();

	if (bResolved)
	{
		pPlayer->m_angEyeAnglesY() = flYaw;
		pPlayer->m_angEyeAnglesX() = flPitch;
		const bool bOldUpdating = G::UpdatingAnims;
		G::UpdatingAnims = true;
		pPlayer->UpdateClientSideAnimation();
		G::UpdatingAnims = bOldUpdating;
	}

	const Vec3 vOldAbsOrigin = pPlayer->GetAbsOrigin();
	const Vec3 vOldAbsAngles = pPlayer->GetAbsAngles();
	pPlayer->SetAbsOrigin(tFront.m_vOrigin);
	pPlayer->SetAbsAngles(pPlayer->m_angRotation());
	pPlayer->InvalidateBoneCache();

	const int iBoneMask = BONE_USED_BY_HITBOX | BONE_USED_BY_VERTEX_MASK;
	m_bSettingUpBones = true;
	bool bSetup = pPlayer->SetupBones(tFront.m_aBones, MAXSTUDIOBONES, iBoneMask, tFront.m_flSimTime);
	if (bSetup && pPlayer->UsesAimStudio())
	{
		if (!tFront.m_pRenderBones || tFront.m_pRenderBones.use_count() != 1)
			tFront.m_pRenderBones = std::make_shared<std::array<matrix3x4, MAXSTUDIOBONES>>();
		memcpy(tFront.m_pRenderBones->data(), tFront.m_aBones, sizeof(tFront.m_aBones));
		bSetup = pPlayer->SetupAimBones(tFront.m_aBones, MAXSTUDIOBONES, iBoneMask, tFront.m_flSimTime);
	}
	m_bSettingUpBones = false;

	pPlayer->SetAbsOrigin(vOldAbsOrigin);
	pPlayer->SetAbsAngles(vOldAbsAngles);
	pPlayer->InvalidateBoneCache();

	if (bResolved)
	{
		pPlayer->m_angEyeAnglesY() = flOldYaw;
		pPlayer->m_angEyeAnglesX() = flOldPitch;
	}

	if (!bSetup)
		tFront.m_bInvalid = true;

	for (int i = 1; i < pTrack->Size(); i++)
		pTrack->At(i).m_bInvalid = true;
}

void CBacktrack::ReportShot(int iIndex)
{
	if (!Vars::Backtrack::PreferOnShot.Value)
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
	auto pPlayer = pEntity ? pEntity->As<CTFPlayer>() : nullptr;
	if (!pPlayer)
		return;

	auto pActiveWeapon = pPlayer->m_hActiveWeapon().Get();
	auto pWeapon = pActiveWeapon ? pActiveWeapon->As<CTFWeaponBase>() : nullptr;
	if (SDK::GetWeaponType(pWeapon) != EWeaponType::HITSCAN)
		return;

	m_mDidShoot[pEntity->entindex()] = true;
}

static constexpr int LATENCY_RING_ROOM = 58;
static constexpr double LATENCY_FILTER_GRACE = 1.5;
static constexpr int LATENCY_MAX_BLOCK_BYTES = 65536;
static constexpr int LATENCY_COPY_LIMIT = 32;

void CBacktrack::SyncLatency(CNetChannel* pNetChan)
{
	if (pNetChan != m_tLatency.m_pNetChan)
	{
		m_tLatency = {};
		m_tLatency.m_pNetChan = pNetChan;
		m_flFakeLatency = 0.f;
	}
}

float CBacktrack::GetFakeTarget(CNetChannel* pNetChan)
{
	if (!Vars::Backtrack::Latency.Value || I::EngineClient->IsPlayingDemo() || pNetChan->IsLoopback())
		return 0.f;

	if (I::ClientState->m_nSignonState != SIGNONSTATE_FULL)
		return 0.f;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive())
		return 0.f;

	static auto cl_updaterate = H::ConVars.FindVar("cl_updaterate");
	static auto sv_maxupdaterate = H::ConVars.FindVar("sv_maxupdaterate");
	float flUpdateRate = cl_updaterate->GetFloat();
	if (sv_maxupdaterate->GetFloat() > 0.f)
		flUpdateRate = std::min(flUpdateRate, sv_maxupdaterate->GetFloat());

	const float flUnlagRoom = m_flMaxUnlag - pNetChan->GetLatency(FLOW_OUTGOING) - ROUND_TO_TICKS(GetFakeInterp()) - 2 * TICK_INTERVAL;
	const float flRingRoom = LATENCY_RING_ROOM / std::max(flUpdateRate, 1.f);
	return std::clamp(GetWishFake(), 0.f, std::min(flUnlagRoom, flRingRoom));
}

const PacketRecord_t* CBacktrack::ChoosePacket(double dNow, float flTarget)
{
	if (m_tLatency.m_dPackets.empty())
		return nullptr;

	const int nNewest = m_tLatency.m_dPackets.front().m_nSequenceNr;
	const PacketRecord_t* pChosen = nullptr;
	const PacketRecord_t* pUnder = nullptr;
	std::array<int, MAX_SUBCHANNELS> aFlips = {};
	for (auto& tRecord : m_tLatency.m_dPackets)
	{
		if (nNewest - tRecord.m_nSequenceNr > LATENCY_RING_ROOM || tRecord.m_nSequenceNr < m_tLatency.m_nPresentedSequenceNr)
			break;

		bool bSafe = true;
		for (int iFlips : aFlips)
		{
			if (iFlips > 0 && !(iFlips & 1))
			{
				bSafe = false;
				break;
			}
		}

		if (bSafe && dNow - tRecord.m_dTime >= flTarget)
		{
			pChosen = &tRecord;
			break;
		}
		if (bSafe)
			pUnder = &tRecord;

		if (tRecord.m_iFlip >= 0)
			aFlips[tRecord.m_iFlip]++;
	}

	return pChosen ? pChosen : pUnder;
}

void CBacktrack::PushLatencySample(double dDelay)
{
	auto& dSamples = m_tLatency.m_dSamples;
	dSamples.push_front(dDelay);
	if (dSamples.size() > 64)
		dSamples.pop_back();

	double dSum = 0.0;
	for (double dSample : dSamples)
		dSum += dSample;
	m_flFakeLatency = float(dSum / dSamples.size());
}

void CBacktrack::AdjustPing(CNetChannel* pNetChan)
{
	m_tLatency.m_nRealInSequenceNr = pNetChan->m_nInSequenceNr;
	m_tLatency.m_nRealInReliableState = pNetChan->m_nInReliableState;
	m_tLatency.m_bFaked = false;

	SyncLatency(pNetChan);
	m_tLatency.m_nSendingSequenceNr = pNetChan->m_nOutSequenceNr;

	if (!Vars::Backtrack::Latency.Value)
	{
		m_tLatency.m_nPresentedSequenceNr = -1;
		m_tLatency.m_dLastFaked = -1.0;
		m_tLatency.m_dSamples.clear();
		m_flFakeLatency = 0.f;
		return;
	}

	const double dNow = F::Ticks.PacketTime(pNetChan->IsLoopback());
	const float flTarget = GetFakeTarget(pNetChan);
	const bool bTransfer = pNetChan->m_ReceiveList()[FRAG_NORMAL_STREAM].buffer || pNetChan->m_ReceiveList()[FRAG_FILE_STREAM].buffer;

	auto pRecord = flTarget > 0.f && !bTransfer ? ChoosePacket(dNow, flTarget) : nullptr;
	if (!pRecord)
	{
		m_tLatency.m_nPresentedSequenceNr = -1;
		PushLatencySample(0.0);
		return;
	}

	m_tLatency.m_nPresentedSequenceNr = pRecord->m_nSequenceNr;
	m_tLatency.m_dLastFaked = dNow;
	PushLatencySample(dNow - pRecord->m_dTime);

	pNetChan->m_nInSequenceNr = pRecord->m_nSequenceNr;
	pNetChan->m_nInReliableState = pRecord->m_nReliableState;
	m_tLatency.m_bFaked = true;
}

void CBacktrack::RestorePing(CNetChannel* pNetChan, bool bSent)
{
	if (m_tLatency.m_bFaked)
	{
		pNetChan->m_nInSequenceNr = m_tLatency.m_nRealInSequenceNr;
		pNetChan->m_nInReliableState = m_tLatency.m_nRealInReliableState;
	}

	if (!bSent)
		return;

	for (auto& tState : m_tLatency.m_aSubChannels)
	{
		if (!tState.m_bFlipPending)
			continue;

		tState.m_bFlipPending = false;
		tState.m_nFlipSequenceNr = m_tLatency.m_nSendingSequenceNr;
		tState.m_nPreviousSequenceNr = m_tLatency.m_nLastSentSequenceNr;
	}

	m_tLatency.m_nLastSentSequenceNr = m_tLatency.m_nSendingSequenceNr;
}

void CBacktrack::PacketStart(CNetChannel* pNetChan, bool bHasHeader)
{
	SyncLatency(pNetChan);

	m_tLatency.m_bPacketHeader = bHasHeader;
	m_tLatency.m_bPacketOpen = true;
	m_tLatency.m_bPacketCopy = false;
	m_tLatency.m_iPacketIndex = -1;
	m_tLatency.m_nReliableBefore = pNetChan->m_nInReliableState;
	m_tLatency.m_nPreviousAck = pNetChan->m_nOutSequenceNrAck;
	m_tLatency.m_aPeeks = {};
}

void CBacktrack::PacketEnd(CNetChannel* pNetChan, double dReceived)
{
	m_tLatency.m_bPacketOpen = false;

	if (m_tLatency.m_bPacketCopy && m_tLatency.m_iPacketIndex >= 0
		&& pNetChan->m_nInReliableState == (m_tLatency.m_nReliableBefore ^ (1 << m_tLatency.m_iPacketIndex)))
		pNetChan->m_nInReliableState = m_tLatency.m_nReliableBefore;
	m_tLatency.m_bPacketCopy = false;

	if (!m_tLatency.m_bPacketHeader || (!Vars::Backtrack::Latency.Value && m_tLatency.m_dPackets.empty()))
		return;

	auto& dPackets = m_tLatency.m_dPackets;
	const int nSequenceNr = pNetChan->m_nInSequenceNr;
	if (!dPackets.empty() && dPackets.front().m_nSequenceNr > nSequenceNr)
	{
		dPackets.clear();
		m_tLatency.m_nPresentedSequenceNr = -1;
	}
	if (!dPackets.empty() && dPackets.front().m_nSequenceNr >= nSequenceNr)
		return;

	int iFlip = -1;
	const int iDiff = m_tLatency.m_nReliableBefore ^ pNetChan->m_nInReliableState;
	for (int i = 0; i < MAX_SUBCHANNELS; i++)
	{
		if (iDiff & (1 << i))
			iFlip = i;
	}

	dPackets.push_front({ nSequenceNr, pNetChan->m_nInReliableState, iFlip, dReceived });
	if (dPackets.size() > 128)
		dPackets.pop_back();
}

bool CBacktrack::IsFiltering(CNetChannel* pNetChan)
{
	if (pNetChan != m_tLatency.m_pNetChan || m_tLatency.m_dLastFaked < 0.0
		|| I::ClientState->m_nSignonState != SIGNONSTATE_FULL)
		return false;

	return F::Ticks.PacketTime(pNetChan->IsLoopback()) - m_tLatency.m_dLastFaked < LATENCY_FILTER_GRACE;
}

bool CBacktrack::PeekBlock(CNetChannel* pNetChan, bf_read& tBuffer, int iStream, BlockPeek_t& tOut)
{
	const int iBegin = tBuffer.m_iCurBit;
	const bool bSingle = tBuffer.ReadOneBit() == 0;

	int iStart = 0, iNum = 0;
	if (!bSingle)
	{
		iStart = int(tBuffer.ReadUBitLong(MAX_FILE_SIZE_BITS - FRAGMENT_BITS));
		iNum = int(tBuffer.ReadUBitLong(3));
	}

	unsigned int uBytes = 0;
	int iFragments = 0;
	if (bSingle || !iStart)
	{
		if (bSingle)
		{
			if (tBuffer.ReadOneBit())
				tBuffer.ReadUBitLong(MAX_FILE_SIZE_BITS);
			uBytes = tBuffer.ReadVarInt32();
		}
		else
		{
			if (tBuffer.ReadOneBit())
			{
				tBuffer.ReadUBitLong(32);
				char sFilename[MAX_OSPATH] = {};
				tBuffer.ReadString(sFilename, sizeof(sFilename));
			}
			if (tBuffer.ReadOneBit())
				tBuffer.ReadUBitLong(MAX_FILE_SIZE_BITS);
			uBytes = tBuffer.ReadUBitLong(MAX_FILE_SIZE_BITS);
		}

		iFragments = int((uBytes + FRAGMENT_SIZE - 1) / FRAGMENT_SIZE);
		if (bSingle)
			iStart = 0, iNum = iFragments;
	}
	else
	{
		auto& tTransfer = pNetChan->m_ReceiveList()[iStream];
		if (!tTransfer.buffer)
			return false;
		uBytes = tTransfer.bytes;
		iFragments = tTransfer.numFragments;
	}

	int iLength = iNum * FRAGMENT_SIZE;
	if (iStart + iNum == iFragments)
	{
		const int iRest = FRAGMENT_SIZE - int(uBytes % FRAGMENT_SIZE);
		if (iRest < FRAGMENT_SIZE)
			iLength -= iRest;
	}

	if (tBuffer.m_bOverflow || iLength <= 0 || iLength > LATENCY_MAX_BLOCK_BYTES)
		return false;

	uint64_t uHash = FNV1A::uHash64;
	for (int i = 0; i < iLength; i++)
	{
		uHash ^= uint64_t(tBuffer.ReadUBitLong(8));
		uHash *= 0x100000001b3ull;
	}
	if (tBuffer.m_bOverflow)
		return false;

	tOut = { true, tBuffer.m_iCurBit - iBegin, { uHash, iStart, iNum, iLength, true } };
	return true;
}

bool CBacktrack::IsCopy(CNetChannel* pNetChan, SubChannel_t& tState)
{
	const int nAck = pNetChan->m_nOutSequenceNrAck;
	if (nAck < tState.m_nFlipSequenceNr)
		return true;

	const bool bCrossing = m_tLatency.m_nPreviousAck < tState.m_nFlipSequenceNr && tState.m_nFlipSequenceNr <= nAck;
	const bool bQueued = m_tLatency.m_nPreviousAck < tState.m_nPreviousSequenceNr;
	return bCrossing && bQueued;
}

bool CBacktrack::PeekPacket(CNetChannel* pNetChan, bf_read& tBuffer, int iFirstStream)
{
	const int iSaved = tBuffer.m_iCurBit;
	const bool bWasOverflow = tBuffer.m_bOverflow;
	bool bIdentical = true, bPresent = false;

	auto& tState = m_tLatency.m_aSubChannels[m_tLatency.m_iPacketIndex];
	for (int iStream = iFirstStream; iStream < MAX_STREAMS; iStream++)
	{
		if (iStream != iFirstStream && !tBuffer.ReadOneBit())
			continue;

		auto& tPeek = m_tLatency.m_aPeeks[iStream];
		if (!PeekBlock(pNetChan, tBuffer, iStream, tPeek))
		{
			bIdentical = false;
			break;
		}

		bPresent = true;
		bIdentical = bIdentical && tPeek.m_tBlock == tState.m_aStreams[iStream];
	}

	tBuffer.m_iCurBit = iSaved;
	tBuffer.m_bOverflow = bWasOverflow;

	if (!bPresent || !bIdentical || !IsCopy(pNetChan, tState))
		return false;

	if (++tState.m_iCopies > LATENCY_COPY_LIMIT)
	{
		tState.m_iCopies = 0;
		return false;
	}

	return true;
}

bool CBacktrack::SkipBlock(CNetChannel* pNetChan, bf_read& tBuffer, int iStream)
{
	if (m_tLatency.m_bPacketOpen)
	{
		m_tLatency.m_bPacketOpen = false;

		const int iSaved = tBuffer.m_iCurBit;
		tBuffer.Seek(iSaved - 3 - (iStream + 1));
		m_tLatency.m_iPacketIndex = int(tBuffer.ReadUBitLong(3));
		tBuffer.Seek(iSaved);

		m_tLatency.m_bPacketCopy = PeekPacket(pNetChan, tBuffer, iStream);
	}

	if (!m_tLatency.m_bPacketCopy || !m_tLatency.m_aPeeks[iStream].m_bPresent)
		return false;

	tBuffer.Seek(tBuffer.m_iCurBit + m_tLatency.m_aPeeks[iStream].m_iBits);
	return true;
}

void CBacktrack::AcceptBlock(int iStream)
{
	if (m_tLatency.m_iPacketIndex < 0 || !m_tLatency.m_aPeeks[iStream].m_bPresent)
		return;

	auto& tState = m_tLatency.m_aSubChannels[m_tLatency.m_iPacketIndex];
	tState.m_aStreams[iStream] = m_tLatency.m_aPeeks[iStream].m_tBlock;
	tState.m_bFlipPending = true;
	tState.m_nFlipSequenceNr = std::numeric_limits<int>::max();
	tState.m_nPreviousSequenceNr = -1;
	tState.m_iCopies = 0;
}

void CBacktrack::CacheDrawInfo(CTFPlayer* pLocal)
{
	auto pResource = H::Entities.GetResource();
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ping)
		|| !pLocal || !pLocal->IsAlive() || !pResource || !pNetChan)
	{
		if (m_bLastDrawValid)
		{
			m_bLastDrawValid = false;
			m_sLastPingText.clear();
			m_sLastScoreboardText.clear();
			m_tDrawCache.Set({});
		}
		return;
	}

	static float flFakeLatency = 0.f;
	{
		static Timer tTimer = {};
		if (tTimer.Run(0.5f))
			flFakeLatency = GetFakeLatency();
	}
	float flFakeLerp = GetFakeInterp() > G::Lerp ? GetFakeInterp() : 0.f;

	float flFake = std::min(flFakeLatency + flFakeLerp, m_flMaxUnlag) * 1000;
	float flLatency = std::max(pNetChan->GetLatency(FLOW_INCOMING) + pNetChan->GetLatency(FLOW_OUTGOING) - flFakeLatency, 0.f) * 1000;
	int iLatencyScoreboard = pResource->m_iPing(I::EngineClient->GetLocalPlayer());

	std::string sPingText = flFake || Vars::Backtrack::Interp.Value > G::Lerp * 1000
		? std::format("Ping {:.0f} (+ {:.0f}) ms", flLatency, flFake)
		: std::format("Ping {:.0f} ms", flLatency);
	std::string sScoreboardText = std::format("Scoreboard {} ms", iLatencyScoreboard);
	if (m_bLastDrawValid && sPingText == m_sLastPingText && sScoreboardText == m_sLastScoreboardText)
		return;

	m_bLastDrawValid = true;
	m_sLastPingText = sPingText;
	m_sLastScoreboardText = sScoreboardText;

	DrawCache_t tCache = {};
	tCache.m_sPingText = std::move(sPingText);
	tCache.m_sScoreboardText = std::move(sScoreboardText);
	tCache.m_bValid = true;
	m_tDrawCache.Set(std::move(tCache));
}

void CBacktrack::Draw()
{
	static IndicatorAnim_t tAnim;
	static std::shared_ptr<const DrawCache_t> s_pLastValidCache;

	const auto pCache = m_tDrawCache.Get();
	if (pCache->m_bValid)
		s_pLastValidCache = pCache;

	const bool bEnabled = (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ping) != 0;
	const bool bVisible = bEnabled && I::EngineClient->IsInGame() && pCache->m_bValid;

	bool bOpening = true;
	const float flAnim = tAnim.Update(bVisible, bOpening);
	if (!tAnim.ShouldDraw(bVisible))
		return;

	const auto& tDraw = pCache->m_bValid ? *pCache : *s_pLastValidCache;

	int x = Vars::Menu::PingDisplay.Value.x;
	int y = Vars::Menu::PingDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.IndicatorScale(1);
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.IndicatorScale(50, Scale_Round))
	{
		x -= H::Draw.IndicatorScale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.IndicatorScale(50, Scale_Round))
	{
		x += H::Draw.IndicatorScale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const int iVtxStart = pDrawList->VtxBuffer.Size;
	const int iCmdStart = pDrawList->CmdBuffer.Size;

	DrawIndicatorText(pDrawList, x, y, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, tDraw.m_sPingText);
	DrawIndicatorText(pDrawList, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, tDraw.m_sScoreboardText);

	TransformIndicatorVertices(pDrawList, iVtxStart, pDrawList->VtxBuffer.Size, iCmdStart, pDrawList->CmdBuffer.Size, flAnim, bOpening, ImGui::GetIO().DisplaySize);
}
