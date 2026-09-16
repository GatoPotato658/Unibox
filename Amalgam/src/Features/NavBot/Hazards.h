#pragma once
#include "../../SDK/SDK.h"
#include "Map.h"
#include <unordered_map>

inline constexpr float HAZARD_COST_SENTRY         = 3500.f;
inline constexpr float HAZARD_COST_ENEMY_INVULN   = 1500.f;
inline constexpr float HAZARD_COST_STICKY         = 1000.f;
inline constexpr float HAZARD_COST_SENTRY_MEDIUM  = 800.f;
inline constexpr float HAZARD_COST_SENTRY_LOW     = 400.f;
inline constexpr float HAZARD_COST_ENEMY_NORMAL   = 300.f;
inline constexpr float HAZARD_COST_ENEMY_DORMANT  = 200.f;

enum class HazardKind : uint8_t
{
	None = 0,
	Sentry,
	SentryMedium,
	SentryLow,
	EnemyNormal,
	EnemyDormant,
	EnemyInvuln,
	Sticky,
};

enum class HazardPolicy : uint8_t
{
	SoftCost,
	HardBlock,
	TempForbid,
};

struct Hazard_t
{
	HazardKind m_eKind = HazardKind::None;
	HazardPolicy m_ePolicy = HazardPolicy::SoftCost;
	float m_flCost = 0.f;
	int m_iLastUpdateTick = 0;
	int m_iExpireTick = 0;
	Vector m_vOrigin = {};
};

class CHazards
{
private:
	std::unordered_map<CNavArea*, Hazard_t> m_mAreaHazards;

	uint64_t m_iGenerationId = 1;

	int m_iLastUpdateTick = 0;
	CNavArea* m_pStandingHazardArea = nullptr;
	bool m_bIgnoreSentries = false;
	float m_flPlayerScanRadius = 350.f;

	void UpdatePlayers(CTFPlayer* pLocal);
	void UpdateBuildings(CTFPlayer* pLocal);
	void UpdateProjectiles(CTFPlayer* pLocal);
	void ExpireStale();

	bool RecordHazard(CNavArea* pArea, HazardKind eKind, HazardPolicy ePolicy, float flCost, const Vector& vOrigin, int iExpireTick);
	static float CostForKind(HazardKind eKind);
	static int PriorityForKind(HazardKind eKind);

public:
	void Update(CTFPlayer* pLocal);
	void Reset();
	void Render();

	float GetCost(CNavArea* pArea) const;
	const Hazard_t* GetHazard(CNavArea* pArea) const;
	bool IsHardBlocked(CNavArea* pArea) const;
	bool HasHazard(CNavArea* pArea) const;
	uint64_t GetGenerationId() const { return m_iGenerationId; }

	void SnapshotCosts(std::unordered_map<CNavArea*, float>& mOut) const;

	void SetIgnoreSentries(bool b) { m_bIgnoreSentries = b; }
	bool IgnoresSentries() const { return m_bIgnoreSentries; }

	void UpdateBotStanding(CNavArea* pLocalArea);
	bool BotStandingOnHazard() const { return m_pStandingHazardArea != nullptr; }

	void AddHazard(CNavArea* pArea, HazardKind eKind, float flCost = 0.f, int iExpireTick = 0, HazardPolicy ePolicy = HazardPolicy::SoftCost);
	void ClearByKind(HazardKind eKind);
	void ClearAll();

	const std::unordered_map<CNavArea*, Hazard_t>& GetHazardMap() const { return m_mAreaHazards; }
};

ADD_FEATURE(CHazards, Hazards);
