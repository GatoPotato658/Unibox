#pragma once
#include "../../../../../SDK/SDK.h"

Enum(MVMTask, None,
	Tank,
	Combat,
	Money,
	Frontline,
	Ammo,
	Health
)

class CMVMController
{
private:
	bool m_bActive = false;
	Timer m_tAnchorRefresh = {};
	std::vector<Vector> m_vSpawnAnchors = {};

	bool IsSupportedClass(CTFPlayer* pLocal) const;
	bool PrimaryHasAmmo() const;
	bool DesiredCombatWeaponCanFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon) const;
	bool GetTankTarget(CBaseEntity*& pOut) const;
	bool GetRobotTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity*& pOut) const;
	bool GetMoneyTarget(CTFPlayer* pLocal, CBaseEntity*& pOut) const;
	bool GetFrontlineTarget(CTFPlayer* pLocal, Vector& vOut);
	void RefreshSpawnAnchors(CTFPlayer* pLocal);
	bool RunTank(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTank);
	bool RunCombat(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget);
	bool RunMoney(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseEntity* pMoney);
	bool RunFrontline(CTFPlayer* pLocal);

public:
	MVMTaskEnum::MVMTaskEnum m_eTask = MVMTaskEnum::None;

	void Update();
	void Reset();
	bool IsActive() const { return m_bActive; }
	bool WantsPrimary(CTFPlayer* pLocal) const;
	bool WantsScoutSecondary(CTFPlayer* pLocal) const;
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
};

ADD_FEATURE(CMVMController, MVMController);
