#include "AimbotHitscan.h"
#include "SpreadOptimizer.h"

#include "../Aimbot.h"
#include "../../Ticks/Ticks.h"
#include "../../Resolver/Resolver.h"
#include "../../NoSpread/NoSpread.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../NavBot/BotUtils.h"
#include "../../Visuals/Visuals.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"

namespace
{
	class CTraceFilterHitscanRecord : public CTraceFilterHitscan
	{
	public:
		CTraceFilterHitscanRecord(CBaseEntity* pLocal, const CBaseEntity* pTarget)
			: CTraceFilterHitscan(pLocal), m_pTarget(pTarget) {}

		bool ShouldHitEntity(IHandleEntity* pHandleEntity, int nContentsMask) override
		{
			return pHandleEntity != m_pTarget && CTraceFilterHitscan::ShouldHitEntity(pHandleEntity, nContentsMask);
		}

	private:
		const IHandleEntity* m_pTarget;
	};

	constexpr float aPointOffsets[][3] = {
		{ 0, 0, 0 },
		{ 0, 0, 1 }, { 0, 0, -1 },
		{ -1, 0, 0 }, { 1, 0, 0 },
		{ 0, -1, 0 }, { 0, 1, 0 },
		{ -1, -1, 1 }, { 1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 },
		{ -1, -1, -1 }, { 1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 }
	};
}

static inline std::vector<Target_t> GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	Vec3 vLocalPos = pLocal->GetShootPos();
	Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	{
		auto eGroup = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
		{
			eGroup = !SDK::FriendlyFire() || Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Team ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;
			if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::ExtinguishTeam &&
				!SDK::FriendlyFire() && SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0)
				eGroup = EntityEnum::PlayerAll;
		}
		if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
			eGroup = Vars::Aimbot::Healing::AutoHeal.Value ? EntityEnum::PlayerTeam : EntityEnum::Invalid;
		bool bHeal = pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN;

		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam)
			{
				if (bHeal)
				{
					if (pEntity->As<CTFPlayer>()->InCond(TF_COND_STEALTHED)
						|| Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly
						&& !H::Entities.IsFriend(pEntity->entindex()) && !H::Entities.InParty(pEntity->entindex()))
						continue;
				}
				if (!SDK::FriendlyFire() && SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0)
				{
					if (!pEntity->As<CTFPlayer>()->InCond(TF_COND_BURNING))
						continue;
				}
			}

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo, Vars::Aimbot::Hitscan::Hitboxes.Value))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());
			if (bTeam && bHeal)
			{
				iPriority = 0;
				switch (Vars::Aimbot::Healing::HealPriority.Value)
				{
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeFriends:
					if (H::Entities.IsFriend(pEntity->entindex()))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeParty:
					if (H::Entities.InParty(pEntity->entindex()))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeTeam:
					iPriority = std::numeric_limits<int>::max();
				}
			}
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}

		if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
			return vTargets;
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::Sticky, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Bombs)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldBomb))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::Bomb, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}

std::vector<HitscanHitbox_t> CAimbotHitscan::GetHitboxes(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget)
{
	std::vector<HitscanHitbox_t> vHitboxes = {};

	if (pWeapon->GetWeaponID() == TF_WEAPON_LASER_POINTER)
	{
		auto pSentry = pLocal->GetObjectOfType(OBJ_SENTRYGUN)->As<CObjectSentrygun>();
		if (!pSentry) return vHitboxes;

		Vec3 vOrigin = pTarget->m_vecOrigin();
		{
			MoveStorage tMoveStorage;
			F::MoveSim.Initialize(pTarget, tMoveStorage);
			if (!tMoveStorage.m_bFailed)
			{
				for (int i = 1 - TIME_TO_TICKS(F::Backtrack.GetReal()); i <= 0; i++)
				{
					F::MoveSim.RunTick(tMoveStorage);
					vOrigin = tMoveStorage.m_vPredictedOrigin;
				}
			}
		}
		if (!SDK::VisPosWorld(nullptr, pTarget, pSentry->m_vecOrigin() + pSentry->GetOffset() / 2 * pSentry->m_flModelScale(), vOrigin + pTarget->GetOffset() / 2))
			return vHitboxes;

		vHitboxes.emplace_back(nullptr, vOrigin);
		return vHitboxes;
	}

	if (!pTarget->IsPlayer())
	{
		vHitboxes.emplace_back();
		return vHitboxes;
	}

	if (auto pSet = pTarget->As<CBaseAnimating>()->GetAimHitboxSet())
	{
		std::vector<std::tuple<const mstudiobbox_t*, int, int>> vBones;
		for (int nHitbox = 0; nHitbox < pSet->numhitboxes; nHitbox++)
		{
			int iPriority = GetHitboxPriority(nHitbox, pLocal, pWeapon, pTarget);
			if (iPriority == -1)
				continue;

			auto pBox = pSet->pHitbox(nHitbox);
			if (!pBox)
				continue;

			vBones.emplace_back(pBox, nHitbox, iPriority);
		}
		std::sort(vBones.begin(), vBones.end(), [&](const auto& a, const auto& b) -> bool
		{
			return std::get<2>(a) < std::get<2>(b);
		});
		vHitboxes.reserve(vBones.size());
		for (auto& [pBox, nHitbox, _] : vBones)
			vHitboxes.emplace_back(pBox, std::nullopt, nHitbox);
	}

	return vHitboxes;
}

int CAimbotHitscan::GetHitboxPriority(int nHitbox, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget)
{
	if (!F::AimbotGlobal.IsHitboxValid(pTarget, nHitbox, Vars::Aimbot::Hitscan::Hitboxes.Value))
		return -1;

	bool bHeadshot = false;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	{
		auto pSniperRifle = pWeapon->As<CTFSniperRifle>();

		if (G::CanHeadshot
			|| pLocal->InCond(TF_COND_AIMING) && (pSniperRifle->GetRifleType() == RIFLE_JARATE && SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0 || Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot))
			bHeadshot = true;
		break;
	}
	case TF_WEAPON_REVOLVER:
	{
		if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1 && (pWeapon->AmbassadorCanHeadshot() || Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot))
			bHeadshot = true;
	}
	}

	if (Vars::Aimbot::Hitscan::Hitboxes.Value & Vars::Aimbot::Hitscan::HitboxesEnum::BodyaimIfLethal && bHeadshot)
	{
		auto pPlayer = pTarget->As<CTFPlayer>();

		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		{
			auto pSniperRifle = pWeapon->As<CTFSniperRifle>();

			int iDamage = ceilf(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetBodyshotMult(pPlayer));
			if (pPlayer->m_iHealth() <= iDamage)
				bHeadshot = false;
			break;
		}
		case TF_WEAPON_REVOLVER:
		{
			if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1)
			{
				float flDistTo = pTarget->m_vecOrigin().DistTo(pLocal->m_vecOrigin());

				float flMult = SDK::AttribHookValue(1, "mult_dmg", pWeapon);
				int iDamage = ceilf(Math::RemapVal(flDistTo, 90.f, 900.f, 60.f, 21.f) * flMult);
				if (pPlayer->m_iHealth() <= iDamage)
					bHeadshot = false;
			}
		}
		}
	}

	bool bHeadOnly = bHeadshot && Vars::Aimbot::Hitscan::Hitboxes.Value & Vars::Aimbot::Hitscan::HitboxesEnum::HeadshotOnly;

	int iHeadPriority = bHeadshot ? 0 : 1;
	int iBodyPriority = bHeadOnly ? -1 : bHeadshot ? 1 : 0;
	int iMiscPriority = bHeadOnly ? -1 : 2;
	int iLimbPriority = bHeadOnly ? -1 : 3;

	switch (pTarget->GetHitboxToBase(nHitbox))
	{
	case HITBOX_HEAD: return iHeadPriority;
	case HITBOX_SPINE0:
	case HITBOX_SPINE1:
	case HITBOX_SPINE2:
	case HITBOX_SPINE3: return iBodyPriority;
	case HITBOX_PELVIS: return iMiscPriority;
	}

	return iLimbPriority;
};

void CAimbotHitscan::GetHitboxInfo(HitscanHitbox_t& tHitbox, CBaseEntity* pTarget, matrix3x4* aBones, const matrix3x4** pTransform, Vec3* pMins, Vec3* pMaxs)
{
	if (pMins && pMaxs)
	{
		if (tHitbox.m_pBox)
		{
			*pMins = tHitbox.m_pBox->bbmin;
			*pMaxs = tHitbox.m_pBox->bbmax;
		}
		else if (pTarget)
		{
			*pMins = pTarget->m_vecMins();
			*pMaxs = pTarget->m_vecMaxs();
		}
	}

	if (pTransform)
	{
		if (aBones && tHitbox.m_pBox)
			*pTransform = &aBones[tHitbox.m_pBox->bone];
		else if (tHitbox.m_vPos)
			*pTransform = &m_mMatrix, Math::MatrixInitialize(m_mMatrix, *tHitbox.m_vPos, false);
		else if (pTarget)
			*pTransform = &pTarget->m_Collision()->CollisionToWorldTransform();
	}
}

float CAimbotHitscan::GetMultipointScale(CBaseEntity* pTarget, CTFWeaponBase* pWeapon, int nHitbox)
{
	bool bPlayer = pTarget->IsPlayer();
	if (bPlayer && !F::AimbotGlobal.ShouldMultipoint(pTarget, nHitbox, Vars::Aimbot::Hitscan::MultipointHitboxes.Value)
		|| pWeapon->GetWeaponID() == TF_WEAPON_LASER_POINTER)
		return 0.f;

	switch (Vars::Aimbot::General::AimType.Value)
	{
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		if (!Vars::Aimbot::General::AssistStrength.Value)
			return 0.f;
	}

	return !bPlayer ? 0.5f : Vars::Aimbot::Hitscan::MultipointScale.Value / 100.f;
}

TickRecord* CAimbotHitscan::BuildForwardRecord(CTFPlayer* pTarget, const TickRecord* pBaseRecord, const CUserCmd* pCmd)
{
	if (!Vars::Aimbot::Hitscan::ForwardTrack.Value || !pBaseRecord || !pTarget->IsAlive() || pTarget->IsDormant() || pTarget->IsAGhost())
		return nullptr;

	if (Vars::Aimbot::Hitscan::ForwardTrackAirOnly.Value && pTarget->IsOnGround())
		return nullptr;

	auto IsFinite = [](const Vec3& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	};
	if (pBaseRecord->m_bInvalid || !std::isfinite(pBaseRecord->m_flSimTime) || pBaseRecord->m_flSimTime != pTarget->m_flSimulationTime()
		|| !IsFinite(pBaseRecord->m_vOrigin) || !IsFinite(pBaseRecord->m_vVelocity) || !IsFinite(pBaseRecord->m_vMins) || !IsFinite(pBaseRecord->m_vMaxs)
		|| !std::isfinite(pBaseRecord->m_flModelScale) || pBaseRecord->m_flModelScale <= 0.f)
		return nullptr;

	const bool bBrokenLagComp = H::Entities.GetLagCompensation(pTarget->entindex());
	const int iChoke = H::Entities.GetChoke(pTarget->entindex());

	const int iMode = Vars::Aimbot::Hitscan::ForwardTrackMode.Value;
	if (iMode == Vars::Aimbot::Hitscan::ForwardTrackModeEnum::BrokenLagComp && !bBrokenLagComp)
		return nullptr;
	if (iMode == Vars::Aimbot::Hitscan::ForwardTrackModeEnum::ChokedOnly && iChoke <= 0 && !bBrokenLagComp)
		return nullptr;

	constexpr float flMaxPrediction = 0.2f;
	const int iServerTick = F::Backtrack.GetServerTick();
	int iTargetTick = iServerTick;
	if (!bBrokenLagComp)
	{
		iTargetTick = pCmd->tick_count - TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
		const int iRewindTick = F::Backtrack.GetRewindTick() - Vars::Backtrack::Offset.Value;
		if (std::abs(TICKS_TO_TIME(iTargetTick - iRewindTick)) > flMaxPrediction)
			iTargetTick = iRewindTick;
		iTargetTick = std::min(iTargetTick, iServerTick);
	}

	const int iLead = iTargetTick - TIME_TO_TICKS(pBaseRecord->m_flSimTime);
	if (iLead <= 0 || TICKS_TO_TIME(iLead) > flMaxPrediction)
		return nullptr;

	MoveStorage tMoveStorage;
	if (!F::MoveSim.Initialize(pTarget, tMoveStorage, false, true, false, &pBaseRecord->m_vOrigin) || tMoveStorage.m_bFailed)
		return nullptr;

	const auto& tMoveData = tMoveStorage.m_MoveData;
	const Vec3 vStart = tMoveData.m_vecAbsOrigin;
	if (!IsFinite(vStart) || !IsFinite(tMoveData.m_vecVelocity))
		return nullptr;
	for (int i = 0; i < iLead; i++)
	{
		F::MoveSim.RunTick(tMoveStorage, false);
		if (tMoveStorage.m_bFailed || !IsFinite(tMoveData.m_vecAbsOrigin) || !IsFinite(tMoveData.m_vecVelocity))
			return nullptr;
	}

	const Vec3& vOrigin = tMoveData.m_vecAbsOrigin;
	const Vec3 vDelta = vOrigin - pBaseRecord->m_vOrigin;
	if (vOrigin == vStart || !vDelta || !IsFinite(vDelta))
		return nullptr;

	auto& tRecord = m_tForwardRecord.emplace(*pBaseRecord);
	tRecord.m_flSimTime = TICKS_TO_TIME(iTargetTick);
	tRecord.m_vOrigin = vOrigin;
	tRecord.m_vVelocity = tMoveData.m_vecVelocity;
	tRecord.m_bOnShot = false;
	tRecord.m_pRenderBones.reset();
	for (auto& mBone : tRecord.m_aBones)
	{
		mBone[0][3] += vDelta.x;
		mBone[1][3] += vDelta.y;
		mBone[2][3] += vDelta.z;
	}
	return &tRecord;
}

int CAimbotHitscan::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const CUserCmd* pCmd)
{
	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
		return false;

	m_vEyePos = pLocal->GetShootPos();

	float flMaxRangeSqr = powf(pWeapon->GetRange(), 2.f);
	bool bPlayer = tTarget.m_pEntity->IsPlayer();
	bool bWrangler = pWeapon->GetWeaponID() == TF_WEAPON_LASER_POINTER;

	static thread_local std::vector<TickRecord*> vRecords = {};
	vRecords.clear();
	const TickRecord* pNewestRecord = nullptr;
	if (!bWrangler)
	{
		pNewestRecord = bPlayer ? F::Backtrack.GetLatestRecord(tTarget.m_pEntity) : nullptr;
		F::Backtrack.GetValidRecords(tTarget.m_pEntity, vRecords, pLocal);
	}
	if (vRecords.empty())
	{
		auto pEntity = tTarget.m_pEntity;
		F::Backtrack.m_tRecord.m_flSimTime = pEntity->m_flSimulationTime();
		F::Backtrack.m_tRecord.m_vOrigin = pEntity->m_vecOrigin();
		F::Backtrack.m_tRecord.m_vMins = pEntity->m_vecMins();
		F::Backtrack.m_tRecord.m_vMaxs = pEntity->m_vecMaxs();
		F::Backtrack.m_tRecord.m_bOnShot = false;
		F::Backtrack.m_tRecord.m_bInvalid = false;
		F::Backtrack.m_tRecord.m_flModelScale = pEntity->As<CBaseAnimating>()->m_flModelScale();
		F::Backtrack.m_tRecord.m_pRenderBones.reset();

		if (bPlayer)
		{
			const Vec3 vOldAbsOrigin = pEntity->GetAbsOrigin();
			const Vec3 vOldAbsAngles = pEntity->GetAbsAngles();
			pEntity->SetAbsOrigin(pEntity->m_vecOrigin());
			pEntity->SetAbsAngles(pEntity->m_angRotation());
			pEntity->InvalidateBoneCache();

			F::Backtrack.m_tRecord.m_vVelocity = pEntity->As<CTFPlayer>()->m_vecVelocity();
			F::Backtrack.SetSettingUpBones(true);
			bool bSetup = pEntity->As<CBaseAnimating>()->SetupAimBones(F::Backtrack.m_tRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, pEntity->m_flSimulationTime());
			F::Backtrack.SetSettingUpBones(false);

			pEntity->SetAbsOrigin(vOldAbsOrigin);
			pEntity->SetAbsAngles(vOldAbsAngles);
			pEntity->InvalidateBoneCache();

			if (!bSetup)
				return false;
		}
		else
			F::Backtrack.m_tRecord.m_vVelocity = {};

		vRecords.push_back(&F::Backtrack.m_tRecord);
		if (bPlayer && !bWrangler)
			pNewestRecord = &F::Backtrack.m_tRecord;
	}

	auto vHitboxes = GetHitboxes(pLocal, pWeapon, tTarget.m_pEntity);
	if (vHitboxes.empty())
		return false;

	int iReturn = false;

	std::optional<Vec3> vPeekPos = std::nullopt;
	if (Vars::Aimbot::Hitscan::PeekAmount.Value && pWeapon->GetWeaponSpread())
	{
		switch (Vars::Aimbot::Hitscan::PeekCheck.Value)
		{
		case Vars::Aimbot::Hitscan::PeekCheckEnum::DoubletapOnly:
			if (!F::Ticks.GetTicks(pWeapon))
				break;
			[[fallthrough]];
		case Vars::Aimbot::Hitscan::PeekCheckEnum::Always:
			vPeekPos = m_vEyePos - pLocal->m_vecVelocity() * TICKS_TO_TIME(Vars::Aimbot::Hitscan::PeekAmount.Value);
		}
	}

	static int iTargetBone = 0;
	const Vec3* pHoldAngle = F::Ticks.GetShootAngle();
	if (pHoldAngle && bPlayer && vRecords.size() > 1)
	{
		Vec3 vHoldAngle = *pHoldAngle - F::NoSpread.GetOffset();
		std::sort(vRecords.begin(), vRecords.end(), [&](const TickRecord* a, const TickRecord* b) -> bool
		{
			Vec3 vPosA = { a->m_aBones[iTargetBone][0][3], a->m_aBones[iTargetBone][1][3], a->m_aBones[iTargetBone][2][3] };
			Vec3 vPosB = { b->m_aBones[iTargetBone][0][3], b->m_aBones[iTargetBone][1][3], b->m_aBones[iTargetBone][2][3] };
			Vec3 vAnglesA = Math::CalcAngle(m_vEyePos, vPosA);
			Vec3 vAnglesB = Math::CalcAngle(m_vEyePos, vPosB);
			return vHoldAngle.DeltaAngle(vAnglesA).Length2DSqr() < vHoldAngle.DeltaAngle(vAnglesB).Length2DSqr();
		});
	}

	float flBoneScale = std::max(Vars::Aimbot::Hitscan::BoneSizeMinimumScale.Value, Vars::Aimbot::Hitscan::MultipointScale.Value / 100.f);
	float flBoneSubtract = Vars::Aimbot::Hitscan::BoneSizeSubtract.Value;
	Vec3 vMins, vMaxs;
	const matrix3x4* pTransform = &m_mMatrix;
	const bool bCheckHull = bPlayer && !bWrangler;
	const Vec3 vPunch = pLocal->m_vecPunchAngle();
	const Vec3 vHeldOffset = pHoldAngle ? F::NoSpread.GetOffset() : Vec3();
	const bool bOptimizeSpread = bCheckHull && F::SpreadOptimizer.Applies(pLocal, pWeapon);
	CTraceFilterHitscanRecord tRecordFilter(pLocal, tTarget.m_pEntity);
	auto VisRecorded = [&](const Vec3& vFrom, const Vec3& vTo)
	{
		CGameTrace tTrace = {};
		SDK::Trace(vFrom, vTo, MASK_SHOT | CONTENTS_GRATE, &tRecordFilter, &tTrace);
		return !tTrace.DidHit();
	};

	for (size_t i = 0; i <= vRecords.size(); i++)
	{
		const bool bForward = i == vRecords.size();
		if (bForward && !pNewestRecord)
			break;

		auto pRecord = bForward ? BuildForwardRecord(tTarget.m_pEntity->As<CTFPlayer>(), pNewestRecord, pCmd) : vRecords[i];
		if (!pRecord)
			break;

		auto aBones = pRecord->m_aBones;
		const Vec3& vHullMins = pRecord->m_vMins;
		const Vec3& vHullMaxs = pRecord->m_vMaxs;
		if (bCheckHull)
			Math::MatrixInitialize(m_mHullMatrix, pRecord->m_vOrigin, false);

		for (auto& tHitbox : vHitboxes)
		{
			GetHitboxInfo(tHitbox, tTarget.m_pEntity, aBones, &pTransform, &vMins, &vMaxs);
			Vec3 vBoxScale(1.f, 1.f, 1.f);
			if (tHitbox.m_pBox)
			{
				for (int i = 0; i < 3; i++)
					vBoxScale[i] = Vec3((*pTransform)[0][i], (*pTransform)[1][i], (*pTransform)[2][i]).Length();
			}

			const Vec3 vCenter = (vMins + vMaxs) / 2;
			const Vec3 vHalfSize = (vMaxs - vMins) / 2;
			const Vec3 vCheckCenter = vCenter * vBoxScale;
			const Vec3 vCheckHalfSize = (vHalfSize * vBoxScale - flBoneSubtract).Max(0.f) * flBoneScale;
			const Vec3 vCheckMins = vCheckCenter - vCheckHalfSize;
			const Vec3 vCheckMaxs = vCheckCenter + vCheckHalfSize;
			const Vec3 vBoxMins = vMins * vBoxScale;
			const Vec3 vBoxMaxs = vMaxs * vBoxScale;
			const float flPointScale = GetMultipointScale(tTarget.m_pEntity, pWeapon, tHitbox.m_iHitbox);
			const Vec3 vPointHalfSize = vHalfSize * flPointScale;
			const auto iPoints = flPointScale > 0.f ? std::size(aPointOffsets) : 1;

			auto VisPlayerRay = [&](const Vec3& vFrom, const Vec3& vDirection, float flRangeSqr)
			{
				float flEntry;
				return Math::RayToOBB(vFrom, vDirection, vBoxMins, vBoxMaxs, *pTransform, 1.f, &flEntry)
					&& flEntry * flEntry <= flRangeSqr
					&& VisRecorded(vFrom, vFrom + vDirection * (flEntry / vDirection.Length()));
			};

			auto VisPoint = [&](const Vec3& vFrom, const Vec3& vPoint)
			{
				return tHitbox.m_pBox ? VisPlayerRay(vFrom, vPoint - vFrom, FLT_MAX)
					: SDK::VisPos(pLocal, tTarget.m_pEntity, vFrom, vPoint);
			};

			for (size_t iPoint = 0; iPoint < iPoints; iPoint++)
			{
				const auto& aOffset = aPointOffsets[iPoint];
				Vec3 vOrigin = Math::VectorTransform(vCenter + Vec3(aOffset) * vPointHalfSize, *pTransform);
				const float flDistanceSqr = m_vEyePos.DistToSqr(vOrigin);
				if (flDistanceSqr > flMaxRangeSqr)
					continue;

				Vec3 vAngles; bool bChanged = Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, vOrigin), vAngles);
				vAngles -= vHeldOffset;
				if (!F::AimbotGlobal.ShouldAimAtAngle(vAngles))
					continue;

				Vec3 vForward; Math::AngleVectors(vAngles + vPunch, &vForward);
				const bool bHullHit = !bCheckHull || Math::RayToOBB(m_vEyePos, vForward, vHullMins, vHullMaxs, m_mHullMatrix);
				const bool bCanFire = bHullHit && (!bChanged || Math::RayToOBB(m_vEyePos, vForward, vCheckMins, vCheckMaxs, *pTransform))
					&& (tHitbox.m_pBox ? VisPlayerRay(m_vEyePos, vForward, flMaxRangeSqr)
						: VisPoint(m_vEyePos, bChanged ? m_vEyePos + vForward * sqrtf(flDistanceSqr) : vOrigin));
				if (bCanFire)
				{
					if (vPeekPos && !VisPoint(*vPeekPos, vOrigin))
						continue;

					if (bOptimizeSpread && tHitbox.m_pBox)
					{
						Vec3 vSpreadPos = vOrigin;
						if (!F::SpreadOptimizer.Optimize(pLocal, pWeapon, tTarget.m_pEntity, aBones, pRecord->m_flModelScale,
							pRecord->m_vOrigin + vHullMins, pRecord->m_vOrigin + vHullMaxs, m_vEyePos, vSpreadPos, tRecordFilter))
							goto skip;

						if (!pHoldAngle && vSpreadPos != vOrigin)
						{
							Vec3 vSpreadAngles; Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, vSpreadPos), vSpreadAngles);
							if (F::AimbotGlobal.ShouldAimAtAngle(vSpreadAngles) && (!vPeekPos || VisRecorded(*vPeekPos, vSpreadPos)))
								vAngles = vSpreadAngles, vOrigin = vSpreadPos;
						}
					}

					tTarget.m_vAngleTo = vAngles;
					tTarget.m_pRecord = pRecord;
					tTarget.m_vPos = vOrigin;
					if (tHitbox.m_pBox)
					{
						tTarget.m_bBacktrack = !bForward;
						tTarget.m_nAimedHitbox = tHitbox.m_iHitbox;
						iTargetBone = tHitbox.m_pBox->bone;
					}
					return true;
				}
				else if (bChanged && VisPoint(m_vEyePos, vOrigin) && (!vPeekPos || VisPoint(*vPeekPos, vOrigin)))
				{
					if (iReturn != 2 || vAngles.DeltaAngle(G::CurrentUserCmd->viewangles).Length2DSqr() < tTarget.m_vAngleTo.DeltaAngle(G::CurrentUserCmd->viewangles).Length2DSqr())
						tTarget.m_vAngleTo = vAngles, tTarget.m_vPos = vOrigin;
					iReturn = 2;
				}
			}
		}

		skip: continue;
	}

	return iReturn;
}

bool CAimbotHitscan::ShouldFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, const Target_t& tTarget)
{
	if (!Vars::Aimbot::General::AutoShoot.Value)
		return false;
	if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::LegitForBots
		&& !F::BotUtils.IsLegitAimReady(tTarget.m_pEntity->entindex())
		&& !(pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC
			&& pWeapon->As<CTFSniperRifle>()->m_flChargedDamage() > 0.f
			&& pLocal->m_hGroundEntity()))
		return false;

	if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot
		&& tTarget.m_pEntity->IsPlayer())
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
			if (!G::CanHeadshot && pLocal->InCond(TF_COND_AIMING) && pWeapon->As<CTFSniperRifle>()->GetRifleType() != RIFLE_JARATE)
				return false;
			break;
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			if (!G::CanHeadshot)
				return false;
			break;
		case TF_WEAPON_REVOLVER:
			if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1 && !pWeapon->AmbassadorCanHeadshot())
				return false;
		}
	}

	if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForCharge)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		{
			auto pSniperRifle = pWeapon->As<CTFSniperRifle>();
			if (!pLocal->InCond(TF_COND_AIMING) || pSniperRifle->m_flChargedDamage() == 150.f)
				break;

			if (tTarget.m_pEntity->IsPlayer())
			{
				auto pPlayer = tTarget.m_pEntity->As<CTFPlayer>();
				if (tTarget.m_nAimedHitbox == HITBOX_HEAD && (pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE_CLASSIC || pSniperRifle->m_flChargedDamage() == 150.f))
				{
					int iDamage = ceilf(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetHeadshotMult(pPlayer));
					if (pPlayer->m_iHealth() <= iDamage && (G::CanHeadshot || pLocal->IsCritBoosted()))
						break;
				}
				else
				{
					int iDamage = ceilf(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetBodyshotMult(pPlayer));
					if (pPlayer->m_iHealth() <= iDamage)
						break;
				}
			}
			else if (tTarget.m_pEntity->IsBuilding())
			{
				auto pBuilding = tTarget.m_pEntity->As<CBaseObject>();
				int iDamage = ceilf(std::max(pSniperRifle->m_flChargedDamage(), 50.f));
				if (pBuilding->m_iHealth() <= iDamage)
					break;
			}
			else
				break;

			return false;
		}
		}
	}

	return true;
}

bool CAimbotHitscan::Aim(const Vec3& vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod)
{
	if (Vec3* pHoldAngle = F::Ticks.GetShootAngle())
	{
		vOut = *pHoldAngle;
		return true;
	}

	bool bReturn = false;
	if (auto pLocal = H::Entities.GetLocal())
		vToAngle -= pLocal->m_vecPunchAngle();
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::LegitForBots:
		vOut = vToAngle;
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
		vOut = vCurAngle.LerpAngle(vToAngle, F::Aimbot.GetSmoothStrength(vCurAngle, vToAngle));
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);

		float flMouseDelta = vMouseDelta.Length2DSqr(), flTargetDelta = vTargetDelta.Length2DSqr();
		vTargetDelta = vTargetDelta.Normalized() * sqrtf(std::min(flMouseDelta, flTargetDelta));
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, F::Aimbot.GetSmoothStrength(vCurAngle, vToAngle));
		bReturn = true;
		break;
	}

		Math::ClampAngles(vOut);
	return bReturn;
}

void CAimbotHitscan::Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod)
{
	Vec3 vOldView = pCmd->viewangles;
	auto MarkSteering = [&](const Vec3& vNew)
		{
			if (G::AimbotSteering)
				return;
			Vec3 vDelta = vNew.DeltaAngle(vOldView);
			if (std::fabs(vDelta.x) > 0.01f || std::fabs(vDelta.y) > 0.01f || std::fabs(vDelta.z) > 0.01f)
				G::AimbotSteering = true;
		};

	bool bUnsure = F::Ticks.IsTimingUnsure();
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		pCmd->viewangles = vAngles;
		I::EngineClient->SetViewAngles(vAngles);
		MarkSteering(vAngles);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bUnsure)
		{
			SDK::FixMovement(pCmd, vAngles);
			pCmd->viewangles = vAngles;
			G::SilentAngles = true;
			MarkSteering(vAngles);
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::LegitForBots:
	{
		auto pLocal = H::Entities.GetLocal();
		if (pLocal && G::AimTarget.m_iEntIndex)
		{
			F::BotUtils.AimLegit(pLocal, pCmd, G::AimTarget.m_iEntIndex, vAngles);
			vAngles = pCmd->viewangles;
			MarkSteering(vAngles);
		}
		break;
	}
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngles);
		pCmd->viewangles = vAngles;
		G::SilentAngles = true;
		MarkSteering(vAngles);
	}
}

static inline void DrawVisuals(CTFPlayer* pLocal, Target_t& tTarget, int nWeaponID)
{
	if (G::Attacking == 1)
	{
		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
	}
	else
	{
		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount, 0 };
	}

	if (G::Attacking == 1 && nWeaponID != TF_WEAPON_LASER_POINTER)
	{
		bool bLine = Vars::Visuals::Line::TracersEnabled.Value;
		bool bBoxes = Vars::Visuals::Hitbox::BonesEnabled.Value & Vars::Visuals::Hitbox::BonesEnabledEnum::OnShot;
		if (bLine || bBoxes)
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			if (bLine)
			{
				Vec3 vEyePos = pLocal->GetShootPos();
				float flDist = vEyePos.DistTo(tTarget.m_vPos);
				Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo + pLocal->m_vecPunchAngle(), &vForward);

				if (Vars::Colors::LineIgnoreZ.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::LineIgnoreZ.Value);
				if (Vars::Colors::Line.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::Line.Value, true);
			}
			if (bBoxes)
			{
				auto vBoxes = F::Visuals.GetHitboxes(tTarget.m_pRecord->m_aBones, tTarget.m_pEntity->As<CBaseAnimating>(), {}, tTarget.m_nAimedHitbox);
				G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());
			}
		}
	}
}

void CAimbotHitscan::BacktrackToCrosshair(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const Vec3 vShootPos = pLocal->GetShootPos();
	Vec3 vForward;
	Math::AngleVectors(pCmd->viewangles, &vForward);

	CrosshairRecord_t tBest = {};
	static thread_local std::vector<TickRecord*> vRecords;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		const auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive()
			|| pPlayer->IsAGhost()
			|| pPlayer->IsInvulnerable())
			continue;

		F::Backtrack.GetValidRecords(pEntity, vRecords);
		if (vRecords.empty())
			continue;

		const CrosshairRecord_t tRecord = F::AimbotGlobal.GetCrosshairRecord(pLocal, pEntity, vShootPos, vForward, pWeapon->GetRange(), vRecords);
		if (tRecord.m_flSimTime != -1.f && tRecord.m_flDist < tBest.m_flDist)
			tBest = tRecord;
	}

	if (tBest.m_flSimTime == -1.f)
		return;

	pCmd->tick_count = TIME_TO_TICKS(tBest.m_flSimTime) + TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
}

void CAimbotHitscan::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_tForwardRecord.reset();

	const int nWeaponID = pWeapon->GetWeaponID();

	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = Vars::Aimbot::General::AimType.Value;

	switch (nWeaponID)
	{
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		if (G::Attacking && !iRealAimType && iLastAimType)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
	}
	iStaticAimType = Vars::Aimbot::General::AimType.Value;

	if (F::AimbotGlobal.ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && (nWeaponID != TF_WEAPON_MINIGUN || pWeapon->As<CTFMinigun>()->m_iWeaponState() == AC_STATE_FIRING || pWeapon->As<CTFMinigun>()->m_iWeaponState() == AC_STATE_SPINNING))
		return;

	switch (nWeaponID)
	{
	case TF_WEAPON_MINIGUN:
		if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::AutoRev)
			pCmd->buttons |= IN_ATTACK2;
		if (pWeapon->As<CTFMinigun>()->m_iWeaponState() != AC_STATE_FIRING && pWeapon->As<CTFMinigun>()->m_iWeaponState() != AC_STATE_SPINNING)
			return;
		break;
	}

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionHitscan.Value);
	if (vTargets.empty())
		return;

	switch (nWeaponID)
	{
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	{
		bool bScoped = pLocal->InCond(TF_COND_ZOOMED);
		if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::AutoScope && !bScoped)
		{
			pCmd->buttons |= IN_ATTACK2;
			return;
		}
		else if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::ScopedOnly && !bScoped)
			return;
		else if (!bScoped && SDK::AttribHookValue(0, "sniper_only_fire_zoomed", pWeapon))
			return;
		break;
	}
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		if (iRealAimType)
			pCmd->buttons |= IN_ATTACK;
	}

	if (!G::AimTarget.m_iEntIndex)
		G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	for (auto& tTarget : vTargets)
	{
		if (nWeaponID == TF_WEAPON_MEDIGUN && pWeapon->As<CWeaponMedigun>()->m_hHealingTarget().Get() == tTarget.m_pEntity)
		{
			if (G::LastUserCmd->buttons & IN_ATTACK)
				pCmd->buttons |= IN_ATTACK;
			return;
		}

		const auto iResult = CanHit(tTarget, pLocal, pWeapon, pCmd);
		if (!iResult) continue;
		if (iResult == 2)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::LegitForBots)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			Aim(pCmd, tTarget.m_vAngleTo);
		}

		if (ShouldFire(pLocal, pWeapon, pCmd, tTarget))
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_MEDIGUN:
				if (!(G::LastUserCmd->buttons & IN_ATTACK))
					pCmd->buttons |= IN_ATTACK;
				break;
			case TF_WEAPON_SNIPERRIFLE_CLASSIC:
				if (pWeapon->As<CTFSniperRifle>()->m_flChargedDamage() && pLocal->m_hGroundEntity())
					pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_LASER_POINTER:
				pCmd->buttons |= IN_ATTACK;
				break;
			default:
				pCmd->buttons |= IN_ATTACK;
			}

			if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::Tapfire && pWeapon->GetWeaponSpread() != 0.f && !pLocal->InCond(TF_COND_RUNE_PRECISION)
				&& m_vEyePos.DistToSqr(tTarget.m_vPos) > powf(Vars::Aimbot::Hitscan::TapfireDistance.Value, 2))
			{
				const float flTimeSinceLastShot = (pLocal->m_nTickBase() * TICK_INTERVAL) - pWeapon->m_flLastFireTime();
				if (flTimeSinceLastShot <= (pWeapon->GetBulletsPerShot() > 1 ? 0.25f : 1.25f))
					pCmd->buttons &= ~IN_ATTACK;
			}
		}

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		if (G::Attacking == 1 && nWeaponID != TF_WEAPON_LASER_POINTER)
		{
			F::Aimbot.m_eRanType = EWeaponType::HITSCAN;
			if (tTarget.m_pEntity->IsPlayer())
				F::Resolver.HitscanRan(pLocal, tTarget.m_pEntity->As<CTFPlayer>(), pWeapon, tTarget.m_nAimedHitbox, m_vEyePos, tTarget.m_vPos);

			if (tTarget.m_bBacktrack)
				pCmd->tick_count = TIME_TO_TICKS(tTarget.m_pRecord->m_flSimTime) + TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
		}
		DrawVisuals(pLocal, tTarget, nWeaponID);

		Aim(pCmd, tTarget.m_vAngleTo);
		if (G::SilentAngles)
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_MEDIGUN:

				G::SilentAngles = false, G::PSilentAngles = true;
			}
		}
		break;
	}
}
