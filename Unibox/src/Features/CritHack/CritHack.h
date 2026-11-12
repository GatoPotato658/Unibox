#pragma once
#include "../../SDK/SDK.h"
#include <mutex>

Enum(CritRequest, Any, Crit, Skip);

struct HealthHistory_t
{
	int m_iNewHealth = 0;
	int m_iOldHealth = 0;
	int m_iSpawnCounter = -1;

	struct HealthStorage_t
	{
		int m_iOldHealth = 0;
		float m_flTime = 0.f;
	};
	std::unordered_map<int, HealthStorage_t> m_mHistory = {};
};

struct CritTotals_t
{
	int m_iRanged = 0;
	int m_iCrit = 0;
	int m_iMelee = 0;

	int Total() const;
	float Observed() const;
	int CritForObserved(float flObserved) const;
};

class CCritHistory
{
private:
	struct Event_t
	{
		CritTotals_t m_tDamage = {};
		float m_flTime = 0.f;
	};

	struct Sample_t
	{
		float m_flValue = -1.f;
		float m_flTime = 0.f;
		int m_iFrom = 0;
	};

	void Settle(float flNow);
	void Record();
	void PushState(const CritTotals_t& tState);
	bool Explains(const Sample_t& tSample) const;
	int ExactCorrection(const Sample_t& tSample) const;

	CritTotals_t m_tBase = {};
	CritTotals_t m_tEvents = {};
	Event_t m_aPending[256] = {};
	int m_iHead = 0;
	int m_iCount = 0;
	int m_iCritCorrection = 0;
	int m_iResetServer = 0;
	float m_flResetTime = 0.f;
	bool m_bWaitServer = false;
	CritTotals_t m_aStates[256] = {};
	int m_iRecorded = 0;
	int m_iUpdateStart = 0;
	int m_iPreviousUpdateStart = 0;
	Sample_t m_aSamples[8] = {};

public:
	void Reset(int iPreviousServer, float flNow);
	void Add(int iDamage, bool bMelee, bool bCrit, float flNow);
	void Reconcile(int iServer, float flNow);
	void Sample(float flObserved, float flNow);
	bool Resolve(float flNow);
	CritTotals_t Totals() const;

	int m_iServer = 0;
	int m_iRound = 0;
	int m_iResidual = 0;
};

class CCritHack
{
private:
	int GetCritCommand(CTFWeaponBase* pWeapon, int iCommandNumber, bool bCrit = true, bool bSafe = true);
	bool IsCritCommand(int iCommandNumber, CTFWeaponBase* pWeapon, bool bCrit = true, bool bSafe = true);
	bool IsCritSeed(int iSeed, CTFWeaponBase* pWeapon, bool bCrit = true, bool bSafe = true);
	int CommandToSeed(int iCommandNumber);

	void UpdateWeaponInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	int GetCritRequest(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	void Reset();
	void ResetHistory();
	void StoreHealthHistory(int iIndex, int iHealth, CTFPlayer* pPlayer = nullptr);
	void StoreHistory(CTFPlayer* pLocal);

	int m_iCritDamage = 0;
	int m_iRangedDamage = 0;
	int m_iMeleeDamage = 0;
	CCritHistory m_tHistory = {};
	int m_iObservedWeapon = 0;
	float m_flObservedValue = -1.f;
	int m_iObservedRound = 0;
	std::unordered_map<int, HealthHistory_t> m_mHealthHistory = {};

	bool m_bCritBanned = false;
	float m_flDamageTilFlip = 0;

	float m_flDamage = 0.f;
	float m_flCost = 0.f;
	int m_iAvailableCrits = 0;
	int m_iPotentialCrits = 0;
	int m_iNextCrit = 0;

	int m_iEntIndex = 0;
	bool m_bMelee = false;
	float m_flCritChance = 0.f;
	float m_flMultCritChance = 1.f;

	struct CritDrawCache_t
	{
		std::string m_sLeftText = {};
		std::string m_sRightText = {};
		Color_t m_tLeftColor = {};
		Color_t m_tRightColor = {};
		Color_t m_tBarColor = {};
		float m_flTargetProgress = 0.f;
		bool m_bDrawFooter = false;
		bool m_bValid = false;
	};
	CritDrawCache_t m_tDrawCache = {};
	std::mutex m_tDrawMutex = {};

public:
	float GetCost(CTFWeaponBase* pWeapon);
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Event(IGameEvent* pEvent, uint32_t uHash, CTFPlayer* pLocal);
	void Store();
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();

	bool WeaponCanCrit(CTFWeaponBase* pWeapon, bool bWeaponOnly = false);
	int PredictCmdNum(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool ShouldForceEffects(CTFPlayer* pLocal);
	
	bool m_bForce = false;
	bool m_bCachedForceEffects = false;

	float GetCritDamage() { return m_iCritDamage; }
	float GetRangedDamage() { return m_iRangedDamage; }
};

ADD_FEATURE(CCritHack, CritHack);