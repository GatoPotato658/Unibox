#include "CheatDetection.h"

#include "../CritHack/CritHack.h"
#include "../Players/PlayerUtils.h"
#include "../Output/Output.h"

#define MAX_LEGAL_CHOKE 24

bool CCheatDetection::ShouldScan()
{
	if (!Vars::CheatDetection::Methods.Value /*|| I::EngineClient->IsPlayingDemo()*/)
		return false;

	static int iStaticTickcount = I::GlobalVars->tickcount;
	const int iLastTickcount = iStaticTickcount;
	const int iCurrTickcount = iStaticTickcount = I::GlobalVars->tickcount;
	if (iCurrTickcount != iLastTickcount + 1)
		return false;

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (pNetChan && (pNetChan->GetTimeSinceLastReceived() > TICK_INTERVAL * 2 || pNetChan->IsTimingOut()))
		return false;

	return true;
}

bool CCheatDetection::InvalidPitch(CTFPlayer* pEntity)
{
	return Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::InvalidPitch
		&& fabsf(pEntity->m_angEyeAnglesX()) > 89.05f;
}

bool CCheatDetection::IsChoking(CTFPlayer* pEntity)
{
	bool bReturn = mData[pEntity].m_PacketChoking.m_bInfract;
	mData[pEntity].m_PacketChoking.m_bInfract = false;

	return Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::PacketChoking && bReturn;
}

bool CCheatDetection::IsFlicking(CTFPlayer* pEntity)
{
	auto& vAngles = mData[pEntity].m_AimFlicking.m_vAngles;
	if (!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::AimFlicking) || vAngles.size() < 3)
		return false;

	const float flNoise = Vars::CheatDetection::MaxNoise.Value * (TICK_INTERVAL / 0.015f);
	for (size_t i = 1; i + 1 < vAngles.size(); i++)
	{
		if (Math::CalcFov(vAngles[i].m_vAngle, vAngles[i + 1].m_vAngle) < Vars::CheatDetection::MinFlick.Value
			|| Math::CalcFov(vAngles[i - 1].m_vAngle, vAngles[i + 1].m_vAngle) > flNoise)
			continue;

		for (size_t j = 0; j <= i; j++)
		{
			if (vAngles[j].m_bAttacking)
			{
				vAngles.clear();
				return true;
			}
		}
	}
	return false;
}

bool CCheatDetection::IsDuckSpeed(CTFPlayer* pEntity)
{
	if (!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::DuckSpeed)
		|| !pEntity->IsDucking() || !pEntity->IsOnGround() || pEntity->m_MoveType() != MOVETYPE_WALK)
	{
		mData[pEntity].m_DuckSpeed.m_iStartTick = 0;
		return false;
	}

	float flSpeed = pEntity->m_vecVelocity().Length2D();
	if (auto pGround = pEntity->m_hGroundEntity().Get())
		flSpeed = (pEntity->m_vecVelocity() - pGround->GetAbsVelocity()).Length2D();

	if (flSpeed < pEntity->m_flMaxspeed() * 0.5f)
	{
		mData[pEntity].m_DuckSpeed.m_iStartTick = 0;
		return false;
	}

	if (!mData[pEntity].m_DuckSpeed.m_iStartTick)
		mData[pEntity].m_DuckSpeed.m_iStartTick = I::GlobalVars->tickcount;

	if (I::GlobalVars->tickcount - mData[pEntity].m_DuckSpeed.m_iStartTick > TIME_TO_TICKS(1))
	{
		mData[pEntity].m_DuckSpeed.m_iStartTick = 0;
		return true;
	}

	return false;
}

bool CCheatDetection::IsLagCompAbusing(CTFPlayer* pEntity)
{
	bool bReturn = mData[pEntity].m_LagCompAbuse.m_bInfract;
	mData[pEntity].m_LagCompAbuse.m_bInfract = false;

	return Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::LagCompAbuse && bReturn;
}

bool CCheatDetection::IsCritManipulating(CTFPlayer* pEntity)
{
	auto& tCritTracker = mData[pEntity].m_CritTracker;
	if (!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::CritManipulation))
	{
		tCritTracker = {};
		return false;
	}

	bool bReturn = tCritTracker.m_bInfract;
	tCritTracker.m_bInfract = false;
	return bReturn;
}

void CCheatDetection::TrackCritEvent(CTFPlayer* pEntity, CTFWeaponBase* pWeapon, bool bCrit)
{
	if (!pWeapon)
		return;

	auto& tCritTracker = mData[pEntity].m_CritTracker;

	if (pEntity->IsCritBoosted())
	{
		tCritTracker.m_mWeaponHistory.erase(pWeapon->GetWeaponID());
		return;
	}

	if (SDK::GetWeaponType(pWeapon) != EWeaponType::HITSCAN || !F::CritHack.WeaponCanCrit(pWeapon))
		return;

	switch (pWeapon->m_iItemDefinitionIndex())
	{
	case Spy_m_TheAmbassador:
	case Spy_m_FestiveAmbassador:
	case Spy_m_TheDiamondback:
	case Engi_m_TheFrontierJustice:
	case Engi_m_FestiveFrontierJustice:
		return;
	}

	auto& tHistory = tCritTracker.m_mWeaponHistory[pWeapon->GetWeaponID()];
	tHistory.m_vHistory.emplace_back(bCrit);
	if (bCrit)
		tHistory.m_iCrits++;

	const int iWindow = std::max(1, Vars::CheatDetection::CritWindow.Value);
	while ((int)tHistory.m_vHistory.size() > iWindow)
	{
		if (tHistory.m_vHistory.front())
			tHistory.m_iCrits--;
		tHistory.m_vHistory.pop_front();
	}

	if ((int)tHistory.m_vHistory.size() < iWindow)
		return;

	const float flCritRate = (float(tHistory.m_iCrits) / float(tHistory.m_vHistory.size())) * 100.f;
	if (flCritRate >= Vars::CheatDetection::CritThreshold.Value)
	{
		tHistory.m_vHistory.clear();
		tHistory.m_iCrits = 0;
		tCritTracker.m_bInfract = true;
	}
}

void CCheatDetection::Infract(CTFPlayer* pEntity, const char* sReason)
{
	auto& tInfo = mData[pEntity];

	bool bMark = false;
	if (Vars::CheatDetection::DetectionsRequired.Value)
	{
		tInfo.m_iDetections++;
		bMark = tInfo.m_iDetections >= Vars::CheatDetection::DetectionsRequired.Value;
	}

	if (bMark || I::GlobalVars->tickcount - tInfo.m_iLastOutputTick >= TIME_TO_TICKS(0.5f))
	{
		tInfo.m_iLastOutputTick = I::GlobalVars->tickcount;
		F::Output.CheatDetection(tInfo.m_sName.c_str(), bMark ? "marked" : "infracted", sReason);
	}
	if (bMark)
	{
		const int iDetections = std::max(tInfo.m_iDetections, Vars::CheatDetection::DetectionsRequired.Value);
		tInfo.m_iDetections = 0;
		F::PlayerUtils.AddTag(
			tInfo.m_uAccountID,
			F::PlayerUtils.TagToIndex(CHEATER_TAG),
			true,
			tInfo.m_sName.c_str(),
			sReason,
			iDetections,
			true);
	}
}

void CCheatDetection::Run()
{
	if (!ShouldScan() || !I::EngineClient->IsConnected() || I::EngineClient->IsPlayingDemo())
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iIndex = pPlayer->entindex();

		if (iIndex == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost()
			|| pPlayer->IsDormant() || pResource->IsFakePlayer(iIndex)
			|| F::PlayerUtils.HasTag(iIndex, F::PlayerUtils.TagToIndex(CHEATER_TAG)))
		{
			auto& tInfo = mData[pPlayer];
			tInfo.m_PacketChoking = {};
			tInfo.m_AimFlicking = {};
			tInfo.m_LagCompAbuse = {};
			tInfo.m_DuckSpeed = {};
			tInfo.m_CritTracker = {};
			continue;
		}

		if (!H::Entities.GetDeltaTime(iIndex))
			continue;

		auto& tInfo = mData[pPlayer];
		tInfo.m_uAccountID = pResource->m_iAccountID(iIndex);
		tInfo.m_sName = F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex));

		if (InvalidPitch(pPlayer))
			Infract(pPlayer, "invalid pitch");
		if (IsChoking(pPlayer))
			Infract(pPlayer, "choking packets");
		if (IsFlicking(pPlayer))
			Infract(pPlayer, "flicking");
		if (IsDuckSpeed(pPlayer))
			Infract(pPlayer, "duck speed");
		if (IsLagCompAbusing(pPlayer))
			Infract(pPlayer, "lag-comp abuse");
		if (IsCritManipulating(pPlayer))
			Infract(pPlayer, "crit manipulation");
	}
}

void CCheatDetection::Reset()
{
	mData.clear();
}

void CCheatDetection::ReportPacket(CTFPlayer* pEntity, int iChoke, int iDelta)
{
	auto& tInfo = mData[pEntity];

	auto& tChoking = tInfo.m_PacketChoking;
	if (Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::PacketChoking)
	{
		if (iChoke > MAX_LEGAL_CHOKE)
			tChoking.m_vChokes.clear();
		else
		{
			tChoking.m_vChokes.push_back(iChoke);
			if (tChoking.m_vChokes.size() > 6)
				tChoking.m_vChokes.pop_front();

			if (tChoking.m_vChokes.size() == 6)
			{
				int iLarge = 0, iMin = std::numeric_limits<int>::max(), iMax = 0;
				for (int i : tChoking.m_vChokes)
				{
					if (i < Vars::CheatDetection::MinChoking.Value)
						continue;
					iLarge++;
					iMin = std::min(iMin, i), iMax = std::max(iMax, i);
				}

				if (iLarge == 6 || iLarge >= 4 && iMax - iMin <= 4)
				{
					tChoking.m_vChokes.clear();
					tChoking.m_bInfract = true;
				}
			}
		}
	}
	else
		tChoking.m_vChokes.clear();

	auto& tLagComp = tInfo.m_LagCompAbuse;
	if (Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::LagCompAbuse)
	{
		const int iMinDelta = std::max(2, Vars::CheatDetection::LagCompMinimumDelta.Value);
		if (iDelta >= iMinDelta && iDelta > iChoke + 1)
			tLagComp.m_vViolationTicks.emplace_back(I::GlobalVars->tickcount);

		const int iWindowTicks = std::max(1, TIME_TO_TICKS(std::max(0.1f, Vars::CheatDetection::LagCompWindow.Value)));
		const int iRequiredBursts = std::max(1, Vars::CheatDetection::LagCompBurstCount.Value);
		while (!tLagComp.m_vViolationTicks.empty() && I::GlobalVars->tickcount - tLagComp.m_vViolationTicks.front() > iWindowTicks)
			tLagComp.m_vViolationTicks.pop_front();

		if ((int)tLagComp.m_vViolationTicks.size() >= iRequiredBursts)
		{
			tLagComp.m_vViolationTicks.clear();
			tLagComp.m_bInfract = true;
		}
	}
	else
		tLagComp = {};

	auto& vAngles = tInfo.m_AimFlicking.m_vAngles;
	if (Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::AimFlicking)
	{
		vAngles.emplace_front(pEntity->GetEyeAngles(), false);
		if (vAngles.size() > 6)
			vAngles.pop_back();
	}
	else
		vAngles.clear();
}

void CCheatDetection::ReportDamage(IGameEvent* pEvent)
{
	const bool bAimFlicking = Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::AimFlicking;
	const bool bCritTracking = Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::CritManipulation;
	if (!bAimFlicking && !bCritTracking)
		return;

	int iIndex = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
	if (!iIndex || iIndex == I::EngineClient->GetLocalPlayer())
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex)->As<CTFPlayer>();
	if (!pEntity || !pEntity->IsPlayer() || pEntity->IsDormant())
		return;

	if (bAimFlicking)
	{
		auto& vAngles = mData[pEntity].m_AimFlicking.m_vAngles;
		if (!vAngles.empty())
			vAngles.front().m_bAttacking = true;
	}

	if (bCritTracking)
	{
		CTFWeaponBase* pWeapon = nullptr;
		const int iWeaponID = pEvent->GetInt("weaponid");
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pSlotWeapon = pEntity->GetWeaponFromSlot(i);
			if (pSlotWeapon && pSlotWeapon->GetWeaponID() == iWeaponID)
			{
				pWeapon = pSlotWeapon;
				break;
			}
		}
		TrackCritEvent(pEntity, pWeapon, pEvent->GetBool("crit"));
	}
}
