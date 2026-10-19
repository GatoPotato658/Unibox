#include "SkinChanger.h"
#include "SkinData.h"

#include <algorithm>
#include <bit>
#include <iterator>

#ifndef TEXTMODE

MAKE_SIGNATURE(CEconItemSchema_GetAttributeDefinition, "client.dll", "85 D2 78 ? 3B 91 E8 02 00 00 7D ? 48 8B 81 D8 02 00 00 8B D2 48 8B 04 D0 C3 33 C0 C3", 0x0);
MAKE_SIGNATURE(CAttributeList_SetRuntimeAttributeValue, "client.dll", "48 89 5C 24 10 55 56 57 48 8B EC 48 83 EC 50 44", 0x0);

#endif

namespace
{
	template <class T, size_t N>
	bool Contains(const T(&aList)[N], int iValue)
	{
		return std::find(std::begin(aList), std::end(aList), iValue) != std::end(aList);
	}

	int RedirectIndex(int nWeaponIndex)
	{
		switch (nWeaponIndex)
		{
		case Soldier_m_RocketLauncher: return Soldier_m_RocketLauncherR;
		case Scout_m_Scattergun: return Scout_m_ScattergunR;
		case Pyro_m_FlameThrower: return Pyro_m_FlameThrowerR;
		case Demoman_m_GrenadeLauncher: return Demoman_m_GrenadeLauncherR;
		case Demoman_s_StickybombLauncher: return Demoman_s_StickybombLauncherR;
		case Heavy_m_Minigun: return Heavy_m_MinigunR;
		case Engi_t_Wrench: return Engi_t_WrenchR;
		case Medic_s_MediGun: return Medic_s_MediGunR;
		case Sniper_m_SniperRifle: return Sniper_m_SniperRifleR;
		case Sniper_s_SMG: return Sniper_s_SMGR;
		case Spy_t_Knife: return Spy_t_KnifeR;
		case Spy_m_Revolver: return Spy_m_RevolverR;
		case Scout_s_ScoutsPistol:
		case Engi_s_EngineersPistol: return Engi_s_PistolR;
		case Soldier_s_SoldiersShotgun:
		case Pyro_s_PyrosShotgun:
		case Heavy_s_HeavysShotgun:
		case Engi_m_EngineersShotgun: return Soldier_s_ShotgunR;
		case Scout_t_Bat: return Scout_t_BatR;
		case Soldier_t_Shovel: return Soldier_t_ShovelR;
		case Pyro_t_FireAxe: return Pyro_t_FireAxeR;
		case Demoman_t_Bottle: return Demoman_t_BottleR;
		case Medic_t_Bonesaw: return Medic_t_BonesawR;
		case Sniper_t_Kukri: return Sniper_t_KukriR;
		default: return nWeaponIndex;
		}
	}

	int BotkillerOffset(int iKey)
	{
		switch (iKey)
		{
		case Sniper_m_SniperRifle: return 0;
		case Heavy_m_Minigun: return 1;
		case Spy_t_Knife: return 2;
		case Engi_t_Wrench: return 3;
		case Medic_s_MediGun: return 4;
		case Demoman_s_StickybombLauncher: return 5;
		case Pyro_m_FlameThrower: return 6;
		case Scout_m_Scattergun: return 7;
		case Soldier_m_RocketLauncher: return 8;
		default: return -1;
		}
	}

	int BotkillerTargetDef(int iKey, int iHead)
	{
		static constexpr int aBases[] = { 792, 801, 881, 890, 899, 908, 957, 966 };
		if (iHead < 1 || iHead > int(std::size(aBases)))
			return 0;

		const int iOffset = BotkillerOffset(iKey);
		if (iOffset < 0)
			return 0;

		return aBases[iHead - 1] + iOffset;
	}

	const SkinData::Kit_t* FindKit(int iKit)
	{
		for (const auto& tKit : SkinData::kKits)
		{
			if (tKit.iId == iKit)
				return &tKit;
		}
		return nullptr;
	}

	const SkinData::PaintBase_t* FindPaintBase(int iDef)
	{
		for (const auto& tBase : SkinData::kPaintBases)
		{
			if (tBase.iDef == iDef)
				return &tBase;
		}
		return nullptr;
	}

	int TemplateDef(int iKit, int iBaseDef)
	{
		const auto pKit = FindKit(iKit);
		if (!pKit)
			return 0;

		switch (pKit->iGroup)
		{
		case SkinData::KitGeneric: return Contains(SkinData::kGenericDefs, iBaseDef) ? iBaseDef : 0;
		case SkinData::KitDragonSlayer: return Contains(SkinData::kDragonSlayerDefs, iBaseDef) ? iBaseDef : 0;
		case SkinData::KitMk1:
			for (const auto& tWeapon : SkinData::kMk1Weapons)
			{
				if (tWeapon.iKit == iKit && tWeapon.iBaseDef == iBaseDef)
					return tWeapon.iPaintDef;
			}
			return 0;
		default: return 0;
		}
	}

	struct PaintPlan_t
	{
		bool bListed = false;
		bool bSupported = false;
		int iTargetDef = 0;
	};

	PaintPlan_t PlanPaint(int iKey, int iKit, bool bAllowUnsupported)
	{
		PaintPlan_t tPlan = {};
		const auto pBase = FindPaintBase(iKey);
		const int iTemplate = pBase ? TemplateDef(iKit, pBase->iBaseDef) : 0;
		tPlan.bSupported = iTemplate && pBase->iKind == SkinData::BasePlain;
		tPlan.bListed = tPlan.bSupported || bAllowUnsupported;
		tPlan.iTargetDef = iTemplate;
		return tPlan;
	}

	unsigned HashSkin(int iKey, const Skin_t& tSkin)
	{
		const int aFields[] = { iKey, tSkin.iPaintKit, tSkin.iWear, tSkin.iSeed, tSkin.iQuality, tSkin.iStatTrak, tSkin.iBotkiller, tSkin.bAustralium, tSkin.bFestive, tSkin.iKillstreak, tSkin.iSheen, tSkin.iUnusual, tSkin.iKillEffect, tSkin.iPaintColor };
		unsigned uHash = 2166136261u;
		for (int iField : aFields)
		{
			uHash ^= unsigned(iField);
			uHash *= 16777619u;
		}
		return uHash;
	}

	unsigned ConfigHash(const std::unordered_map<int, Skin_t>& mSkins, bool bAllowUnsupported)
	{
		unsigned uHash = bAllowUnsupported ? 0x9E3779B9u : 0x85EBCA6Bu;
		for (const auto& [iKey, tSkin] : mSkins)
		{
			if (!tSkin.Empty())
				uHash += HashSkin(iKey, tSkin);
		}
		return uHash ? uHash : 1;
	}

#ifndef TEXTMODE
	constexpr int kAttrPaintkit = 834;
	constexpr int kAttrWear = 725;
	constexpr int kAttrSeedLo = 866;
	constexpr int kAttrSeedHi = 867;
	constexpr int kAttrInspect = 731;
	constexpr int kAttrUnusualStatic = 370;
	constexpr int kAttrFestive = 2053;
	constexpr int kAttrAustralium = 2027;
	constexpr int kAttrLootRarity = 2022;
	constexpr int kAttrStyleOverride = 542;
	constexpr int kAttrKillstreakTier = 2025;
	constexpr int kAttrKillstreakSheen = 2014;
	constexpr int kAttrKillEffect = 2013;
	constexpr int kAttrItemTint = 142;
	constexpr int kAttrKillEater = 214;
	constexpr int kAttrKillEaterType = 292;

	constexpr float kWear[] = { 0.2f, 0.4f, 0.6f, 0.8f, 1.f };

	constexpr int kQualityStrange = 11;

	int UnusualParticle(int iUnusual)
	{
		switch (iUnusual)
		{
		case WeaponUnusualHot: return 701;
		case WeaponUnusualIsotope: return 702;
		case WeaponUnusualCool: return 703;
		case WeaponUnusualEnergyOrb: return 704;
		default: return 0;
		}
	}

	std::unordered_map<int, void*> s_mAttrDefs;

	void* ResolveAttrDef(int iIndex)
	{
		if (const auto it = s_mAttrDefs.find(iIndex); it != s_mAttrDefs.end())
			return it->second;
		void* pDef = nullptr;
		__try
		{
			if (auto pSchema = CEconItemSchema::GetInstance())
				pDef = S::CEconItemSchema_GetAttributeDefinition.Call<void*>(pSchema, iIndex);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			pDef = nullptr;
		}
		if (pDef)
			s_mAttrDefs[iIndex] = pDef;
		return pDef;
	}

	class CAttributeList
	{
	public:
		void SetAttribute(int iIndex, float flValue)
		{
			const auto pDef = ResolveAttrDef(iIndex);
			if (!pDef)
				return;

			S::CAttributeList_SetRuntimeAttributeValue.Call<void>(this, pDef, flValue);
		}

		void SetInt(int iIndex, int iValue)
		{
			SetAttribute(iIndex, std::bit_cast<float>(iValue));
		}
	};

	int ResolveEconOffset(const char* sProp)
	{
		const char* aTables[] = { "CTFWeaponBase", "CBaseCombatWeapon", "CEconEntity" };
		for (const char* sTable : aTables)
		{
			if (const int nOffset = U::NetVars.GetNetVar(sTable, sProp))
				return nOffset;
		}
		return 0;
	}

	CAttributeList* AttributeList(CTFWeaponBase* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = ResolveEconOffset("m_AttributeList");
		if (nOffset <= 0)
			return nullptr;
		return reinterpret_cast<CAttributeList*>(uintptr_t(pWeapon) + nOffset);
	}

	uint16_t* DefIndex(CBaseEntity* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = ResolveEconOffset("m_iItemDefinitionIndex");
		if (nOffset <= 0)
			return nullptr;
		return reinterpret_cast<uint16_t*>(uintptr_t(pWeapon) + nOffset);
	}

	int* EntityQuality(CBaseEntity* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = ResolveEconOffset("m_iEntityQuality");
		if (nOffset <= 0)
			return nullptr;
		return reinterpret_cast<int*>(uintptr_t(pWeapon) + nOffset);
	}

	int ApplySkin(CTFWeaponBase* pWeapon, int iKey, const Skin_t& tSkin, bool bAllowUnsupported)
	{
		int iTarget = iKey;

		auto pList = AttributeList(pWeapon);
		if (!pList)
			return iTarget;

		if (tSkin.iPaintKit >= 0)
		{
			const auto tPlan = PlanPaint(iKey, tSkin.iPaintKit, bAllowUnsupported);
			if (tPlan.bListed)
			{
				if (tPlan.iTargetDef)
					iTarget = tPlan.iTargetDef;

				pList->SetInt(kAttrPaintkit, tSkin.iPaintKit);
				pList->SetAttribute(kAttrWear, kWear[std::clamp(tSkin.iWear, 0, int(std::size(kWear)) - 1)]);
				pList->SetAttribute(kAttrInspect, 1.f);
				pList->SetInt(kAttrSeedLo, tSkin.iSeed);
				pList->SetInt(kAttrSeedHi, 0);
			}
		}

		if (const int iBotkiller = BotkillerTargetDef(iKey, tSkin.iBotkiller))
			iTarget = iBotkiller;

		if (tSkin.bAustralium)
		{
			if (CSkinChanger::IsGoldenPan(iKey))
				iTarget = Misc_t_GoldFryingPan;
			else if (CSkinChanger::CanAustralium(iKey))
			{
				pList->SetInt(kAttrAustralium, 1);
				pList->SetInt(kAttrLootRarity, 1);
				pList->SetAttribute(kAttrStyleOverride, 1.f);
			}
		}

		if (tSkin.bFestive)
			pList->SetAttribute(kAttrFestive, 1.f);

		if (tSkin.iKillstreak)
			pList->SetAttribute(kAttrKillstreakTier, float(tSkin.iKillstreak));

		int iSheen = tSkin.iSheen;
		if (!iSheen && tSkin.iKillstreak > 1)
			iSheen = 1;
		if (iSheen)
			pList->SetAttribute(kAttrKillstreakSheen, float(iSheen));

		if (const int iUnusual = UnusualParticle(tSkin.iUnusual))
			pList->SetAttribute(kAttrUnusualStatic, float(iUnusual));
		if (tSkin.iKillEffect > 0)
			pList->SetAttribute(kAttrKillEffect, (float)tSkin.iKillEffect);
		if (tSkin.iPaintColor >= 0)
			pList->SetAttribute(kAttrItemTint, (float)tSkin.iPaintColor);

		if (tSkin.iStatTrak >= 0 || tSkin.iQuality >= 0)
		{
			const int iQuality = tSkin.iStatTrak >= 0 ? kQualityStrange : tSkin.iQuality;
			if (auto pQuality = EntityQuality(pWeapon))
			{
				if (*pQuality != iQuality)
					*pQuality = iQuality;
			}
			if (iQuality == kQualityStrange)
			{
				pList->SetInt(kAttrKillEater, std::max(tSkin.iStatTrak, 0));
				pList->SetAttribute(kAttrKillEaterType, 0.f);
			}
		}

		return iTarget;
	}
#endif
}

int CSkinChanger::Key(int iDefIndex)
{
	return RedirectIndex(iDefIndex);
}

const char* CSkinChanger::WeaponName(int iKey)
{
	for (const auto& tName : SkinData::kWeaponNames)
	{
		if (tName.iDef == iKey)
			return tName.sName;
	}
	return nullptr;
}

bool CSkinChanger::CanAustralium(int iKey)
{
	return Contains(SkinData::kAustraliumDefs, iKey) || IsGoldenPan(iKey);
}

bool CSkinChanger::IsGoldenPan(int iKey)
{
	return iKey == Misc_t_FryingPan;
}

bool CSkinChanger::CanFestive(int iKey)
{
	return Contains(SkinData::kFestiveDefs, iKey);
}

bool CSkinChanger::CanPaint(int iKey, bool bAllowUnsupported)
{
	if (bAllowUnsupported)
		return true;
	const auto pBase = FindPaintBase(iKey);
	return pBase && pBase->iKind == SkinData::BasePlain;
}

bool CSkinChanger::CanBotkiller(int iKey)
{
	return BotkillerOffset(iKey) >= 0;
}

int CSkinChanger::BotkillerDef(int iKey, int iHead)
{
	return BotkillerTargetDef(iKey, iHead);
}

void CSkinChanger::GetKits(int iKey, bool bAllowUnsupported, int iCurrent, std::vector<std::string>& vNames, std::vector<int>& vIds)
{
	vNames = { "None" };
	vIds = { -1 };
	if (iKey < 0)
		return;

	for (int iPass = 0; iPass < 2; iPass++)
	{
		for (const auto& tKit : SkinData::kKits)
		{
			if ((tKit.iGroup == SkinData::KitMk1) != (iPass == 1))
				continue;

			const auto tPlan = PlanPaint(iKey, tKit.iId, bAllowUnsupported);
			if (!tPlan.bListed && tKit.iId != iCurrent)
				continue;

			std::string sName = tKit.sName;
			if (!tPlan.bListed)
				sName += " (unsupported, enable option)";
			else if (!tPlan.iTargetDef)
				sName += " (won't render)";
			vNames.push_back(std::move(sName));
			vIds.push_back(tKit.iId);
		}
	}
}

HeldWeapon_t CSkinChanger::GetHeldWeapon()
{
	std::lock_guard tLock(m_tMutex);
	return m_tHeld;
}

Skin_t CSkinChanger::Get(int iKey)
{
	std::lock_guard tLock(m_tMutex);
	auto it = m_mSkins.find(iKey);
	return it != m_mSkins.end() ? it->second : Skin_t{};
}

void CSkinChanger::Set(int iKey, const Skin_t& tSkin)
{
	if (iKey < 0)
		return;

	std::lock_guard tLock(m_tMutex);
	if (tSkin.Empty())
		m_mSkins.erase(iKey);
	else
		m_mSkins[iKey] = tSkin;
}

void CSkinChanger::Clear()
{
	std::lock_guard tLock(m_tMutex);
	m_mSkins.clear();
}

std::unordered_map<int, Skin_t> CSkinChanger::GetSkins()
{
	std::lock_guard tLock(m_tMutex);
	return m_mSkins;
}

void CSkinChanger::SetSkins(std::unordered_map<int, Skin_t> mSkins)
{
	std::lock_guard tLock(m_tMutex);
	m_mSkins = std::move(mSkins);
}

#ifndef TEXTMODE

void CSkinChanger::ScheduleRefresh()
{
	m_bRefreshPending = true;
	m_flDirtyTime = I::GlobalVars ? I::GlobalVars->realtime : 0.f;
}

int CSkinChanger::OriginalDef(CBaseEntity* pWeapon, int iHandle, int iCurrent) const
{
	auto it = m_mTracked.find(iHandle);
	if (it != m_mTracked.end() && it->second.pEntity == pWeapon && it->second.iApplied == iCurrent)
		return it->second.iOriginal;
	return iCurrent;
}

void CSkinChanger::RunFrame()
{
	Vars::Visuals::SkinChanger::Enabled.Value = false;
	if (m_bWasEnabled)
	{
		m_bWasEnabled = false;
		m_uLastHash = 0;
		m_mTracked.clear();
		ScheduleRefresh();
	}
	return;
	m_bWasEnabled = true;

	const bool bFullPacket = I::ClientState && I::ClientState->m_nDeltaTick == -1;

	const auto mSkins = GetSkins();
	const bool bAllowUnsupported = Vars::Visuals::SkinChanger::AllowUnsupported.Value;
	if (const unsigned uHash = ConfigHash(mSkins, bAllowUnsupported); uHash != m_uLastHash)
	{
		m_uLastHash = uHash;
		if (!bFullPacket)
			ScheduleRefresh();
	}

	CTFPlayer* pLocal = nullptr;
	if (I::EngineClient && I::ClientEntityList)
	{
		const int iLocal = I::EngineClient->GetLocalPlayer();
		if (auto pEntity = iLocal > 0 ? I::ClientEntityList->GetClientEntity(iLocal) : nullptr)
			pLocal = pEntity->As<CTFPlayer>();
	}
	if (!pLocal || !pLocal->IsPlayer())
	{
		m_mTracked.clear();
		return;
	}

	std::unordered_map<int, Tracked_t> mTracked;
	auto& vWeapons = pLocal->m_hMyWeapons();
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		const auto& hWeapon = vWeapons[i];
		if (!hWeapon.IsValid())
			continue;
		auto pWeapon = hWeapon.Get();
		if (!pWeapon)
			continue;
		auto pDef = DefIndex(pWeapon);
		if (!pDef)
			continue;

		const int iHandle = hWeapon.ToInt();
		const int iOriginal = OriginalDef(pWeapon, iHandle, *pDef);
		const int iKey = Key(iOriginal);

		int iTarget = iOriginal;
		if (auto it = mSkins.find(iKey); it != mSkins.end() && !it->second.Empty())
			iTarget = ApplySkin(pWeapon, iKey, it->second, bAllowUnsupported);

		if (*pDef != iTarget)
			*pDef = uint16_t(iTarget);
		mTracked[iHandle] = { pWeapon, iOriginal, iTarget };
	}
	m_mTracked = std::move(mTracked);
}

void CSkinChanger::Apply()
{
	const bool bFullPacket = I::ClientState && I::ClientState->m_nDeltaTick == -1;
	if (bFullPacket)
		m_bRefreshPending = false;

	RunFrame();
}

void CSkinChanger::Service()
{
	if (!I::EngineClient || !I::ClientState || !I::GlobalVars
		|| !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo())
	{
		m_bRefreshPending = false;
	}
	else
	{
		const float flNow = I::GlobalVars->realtime;
		if (m_bRefreshPending && flNow - m_flDirtyTime >= 0.35f && flNow - m_flLastRefresh >= 1.f)
		{
			m_bRefreshPending = false;
			if (I::ClientState->m_nDeltaTick != -1)
			{
				m_flLastRefresh = flNow;
				I::ClientState->ForceFullUpdate();
			}
		}
	}

	RunFrame();
}

void CSkinChanger::CacheMenuInfo(CTFWeaponBase* pWeapon)
{
	HeldWeapon_t tHeld = {};
	if (pWeapon)
	{
		if (auto pDef = DefIndex(pWeapon))
		{
			int iOriginal = *pDef;
			for (const auto& [iHandle, tTracked] : m_mTracked)
			{
				if (tTracked.pEntity == pWeapon && tTracked.iApplied == *pDef)
				{
					iOriginal = tTracked.iOriginal;
					break;
				}
			}
			tHeld = { true, Key(iOriginal) };
		}
	}

	std::lock_guard tLock(m_tMutex);
	m_tHeld = tHeld;
}

#else

void CSkinChanger::ScheduleRefresh() {}
int CSkinChanger::OriginalDef(CBaseEntity*, int, int iCurrent) const { return iCurrent; }
void CSkinChanger::RunFrame() {}
void CSkinChanger::Apply() {}
void CSkinChanger::Service() {}
void CSkinChanger::CacheMenuInfo(CTFWeaponBase*) {}

#endif
