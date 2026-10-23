#include "SkinChanger.h"
#include "SkinData.h"
#include "../../Misc/AutoItem/AutoItem.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <random>
#include <iterator>

#ifndef TEXTMODE

MAKE_SIGNATURE(CEconItemSchema_GetAttributeDefinition, "client.dll", "85 D2 78 ? 3B 91 E8 02 00 00 7D ? 48 8B 81 D8 02 00 00 8B D2 48 8B 04 D0 C3 33 C0 C3", 0x0);
MAKE_SIGNATURE(CAttributeList_SetRuntimeAttributeValue, "client.dll", "48 89 5C 24 10 55 56 57 48 8B EC 48 83 EC 50 44", 0x0);
MAKE_SIGNATURE(CEconItemView_Constructor, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 48 8D 05 ? ? ? ? 66 C7 41 10 FF FF 48 89 01 48 8B F9 48 8D 05", 0x0);
MAKE_SIGNATURE(CEconItemView_Assign, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 0F B7 42 48 48 8B DA 66 89 41 48 48 8B F9 8B 42 4C 89 41 4C 8B 42 50 89 41 50 8B 6A", 0x0);
MAKE_SIGNATURE(CEconItemView_GetSOCData, "client.dll", "40 57 48 83 EC 20 48 8B 41 78 48 8B F9 48 85 C0 0F 85 A3 00 00 00 48 8B 0D ? ? ? ? 48 89 5C 24 30 48 8B 01 FF 90 D0", 0x0);
MAKE_SIGNATURE(CEconItem_Constructor, "client.dll", "48 89 5C 24 08 57 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 48 83 C1 08 E8 ? ? ? ? 48 8B 5C 24 30 48 8D 05 ? ? ? ? 33 C9 48 89 07", 0x0);
MAKE_SIGNATURE(CTFPlayerInventory_AddEconItem, "client.dll", "40 53 48 83 EC 30 0F B6 5C 24 60 88 5C 24 20 E8 ? ? ? ? 84 C0 74 20 84 DB 74 14", 0x0);
MAKE_SIGNATURE(CPlayerInventory_RemoveItem, "client.dll", "40 53 56 57 48 83 EC 30 44 8B 41 70 33 DB 48 8B F9 8B F3 45 85 C0 0F 8E", 0x0);
MAKE_SIGNATURE(CTFPlayerInventory_GetMaxItems, "client.dll", "40 53 48 83 EC ? 48 8B 89 ? ? ? ? BB", 0x0);

MAKE_SIGNATURE(CTFWearable_Constructor, "client.dll", "40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? C6 83 A0 0E 00 00 00 48 8D 05", 0x0);
MAKE_SIGNATURE(MemAlloc_Zero, "client.dll", "48 89 5C 24 08 57 48 83 EC 20 48 8B 05 ? ? ? ? 48 8B F9 48 8B D7 48 8B 08 48 8B 01 FF 50 08 4C 8B C7 33 D2 48 8B C8 48 8B D8 E8", 0x0);
MAKE_SIGNATURE(CEconEntity_InitializeAttributes, "client.dll", "48 89 5C 24 08 57 48 83 EC 40 48 8B D9 48 81 C1 38 0C 00 00 48 8B D3 48 8B 01 FF 50 20 83 BB 94 0C 00 00 01 74 1E 48 8D", 0x0);
MAKE_SIGNATURE(CEconItemView_GetPlayerDisplayModel, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 0F B7 59 48 41 8B E8 48 63 F2 4C 8B F1 E8", 0x0);
MAKE_SIGNATURE(CBaseEntity_SetOwnerEntity, "client.dll", "40 53 48 83 EC 20 48 8B D9 48 85 D2 74 17 48 8B 02 48 8B CA FF 50 10 8B 10 89 93 64 07 00 00 48 83 C4 20 5B C3 C7 81 64 07 00 00 FF FF FF FF", 0x0);
MAKE_SIGNATURE(CUtlVector_SetCountHandle, "client.dll", "48 89 5C 24 08 57 48 83 EC 20 48 63 DA 48 03 D9 49 63 F8 8B 43 10 3B C7 7D 12 44 8B C7 8B D0 44 2B C0 48 8B CB E8 ? ? ? ? EB 05 7E 03 89 7B 10 39 7B 08 7D 2D 83 7B 0C 00 7C 27 48 8B 03 48 8B CF 48 C1 E1 02", 0x0);

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
		case Sniper_m_SniperRifle:
		case Sniper_m_SniperRifleR: return 0;
		case Heavy_m_Minigun:
		case Heavy_m_MinigunR: return 1;
		case Spy_t_Knife:
		case Spy_t_KnifeR: return 2;
		case Engi_t_Wrench:
		case Engi_t_WrenchR: return 3;
		case Medic_s_MediGun:
		case Medic_s_MediGunR: return 4;
		case Demoman_s_StickybombLauncher:
		case Demoman_s_StickybombLauncherR: return 5;
		case Pyro_m_FlameThrower:
		case Pyro_m_FlameThrowerR: return 6;
		case Scout_m_Scattergun:
		case Scout_m_ScattergunR: return 7;
		case Soldier_m_RocketLauncher:
		case Soldier_m_RocketLauncherR: return 8;
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

	int TemplateDef(const SkinData::Kit_t& tKit, int iBaseDef)
	{
		switch (tKit.iGroup)
		{
		case SkinData::KitGeneric: return Contains(SkinData::kGenericDefs, iBaseDef) ? iBaseDef : 0;
		case SkinData::KitDragonSlayer: return Contains(SkinData::kDragonSlayerDefs, iBaseDef) ? iBaseDef : 0;
		case SkinData::KitMk1:
			for (const auto& tWeapon : SkinData::kMk1Weapons)
			{
				if (tWeapon.iKit == tKit.iId && tWeapon.iBaseDef == iBaseDef)
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

	PaintPlan_t PlanPaint(int iKey, const SkinData::Kit_t& tKit, bool bAllowUnsupported)
	{
		PaintPlan_t tPlan = {};
		const auto pBase = FindPaintBase(iKey);
		tPlan.iTargetDef = pBase ? TemplateDef(tKit, pBase->iBaseDef) : 0;
		tPlan.bSupported = tPlan.iTargetDef && pBase->iKind == SkinData::BasePlain;
		tPlan.bListed = tPlan.bSupported || bAllowUnsupported;
		return tPlan;
	}

	PaintPlan_t PlanPaint(int iKey, int iKit, bool bAllowUnsupported)
	{
		const auto pKit = FindKit(iKit);
		return pKit ? PlanPaint(iKey, *pKit, bAllowUnsupported) : PaintPlan_t{};
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
	constexpr int kAttrAttachedParticle = 134;
	constexpr int kAttrTauntParticle = 2041;
	constexpr size_t kItemViewAttributeList = 0xF0;

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

	int CachedEconOffset(int& iOffset, const char* sProp)
	{
		if (!iOffset)
			iOffset = ResolveEconOffset(sProp);
		return iOffset;
	}

	CAttributeList* AttributeList(CTFWeaponBase* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = 0;
		if (!CachedEconOffset(nOffset, "m_AttributeList"))
			return nullptr;
		return reinterpret_cast<CAttributeList*>(uintptr_t(pWeapon) + nOffset);
	}

	uint16_t* DefIndex(CBaseEntity* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = 0;
		if (!CachedEconOffset(nOffset, "m_iItemDefinitionIndex"))
			return nullptr;
		return reinterpret_cast<uint16_t*>(uintptr_t(pWeapon) + nOffset);
	}

	int* EntityQuality(CBaseEntity* pWeapon)
	{
		if (!pWeapon)
			return nullptr;
		static int nOffset = 0;
		if (!CachedEconOffset(nOffset, "m_iEntityQuality"))
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

			const auto tPlan = PlanPaint(iKey, tKit, bAllowUnsupported);
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
	std::lock_guard tLock(m_tMutex);

	if (!Vars::Visuals::SkinChanger::Enabled.Value)
	{
		if (m_bWasEnabled)
		{
			m_bWasEnabled = false;
			m_uLastHash = 0;
			m_mTracked.clear();
			ScheduleRefresh();
		}
		return;
	}

	m_bWasEnabled = true;

	const bool bFullPacket = I::ClientState && I::ClientState->m_nDeltaTick == -1;

	const bool bAllowUnsupported = Vars::Visuals::SkinChanger::AllowUnsupported.Value;
	if (const unsigned uHash = ConfigHash(m_mSkins, bAllowUnsupported); uHash != m_uLastHash)
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
		if (auto it = m_mSkins.find(iKey); it != m_mSkins.end() && !it->second.Empty())
			iTarget = ApplySkin(pWeapon, iKey, it->second, bAllowUnsupported);

		if (*pDef != iTarget)
			*pDef = uint16_t(iTarget);
		mTracked[iHandle] = { pWeapon, iOriginal, iTarget };
	}
	m_mTracked = std::move(mTracked);
}

void CSkinChanger::Apply()
{
	{
		std::lock_guard tLock(m_tMutex);
		const bool bFullPacket = I::ClientState && I::ClientState->m_nDeltaTick == -1;
		if (bFullPacket)
		{
			m_bRefreshPending = false;
			for (auto& uKey : m_aWearableViewKey)
				uKey = 0;
		}
	}

	RunFrame();
}

void CSkinChanger::Service()
{
	bool bInGame = false;
	{
		std::lock_guard tLock(m_tMutex);
		if (!I::EngineClient || !I::ClientState || !I::GlobalVars
			|| !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo())
		{
			m_bRefreshPending = false;
		}
		else
		{
			bInGame = true;
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
	}

	RunFrame();
	SyncInventory();
	UpdateWearables(bInGame ? H::Entities.GetLocal() : nullptr);
}

void CSkinChanger::RequestFullUpdate()
{
	std::lock_guard tLock(m_tMutex);
	m_bRefreshPending = true;
	m_flDirtyTime = -1.f;
}

namespace
{
	template <class T>
	T& Field(void* pBase, size_t uOffset)
	{
		return *reinterpret_cast<T*>(uintptr_t(pBase) + uOffset);
	}

	constexpr int kInventoryOwner = 0x08;
	constexpr int kInventoryItemsData = 0x60;
	constexpr int kInventoryItemsCount = 0x70;
	constexpr int kInventoryGotItems = 0x94;
	constexpr int kInventorySOCache = 0x98;
	constexpr int kInventoryLoadout = 0x828;
	constexpr int kInventoryLoadoutChanged = 0xEB0;
	constexpr int kLoadoutSlots = 19;
	constexpr int kLoadoutMaxClass = 9;
	constexpr size_t kEconItemViewSize = SIZE_OF_ITEMVIEW;
	constexpr size_t kEconItemSize = 0x80;

	constexpr int kViewDefIndex = 0x48;
	constexpr int kViewQuality = 0x4C;
	constexpr int kViewItemIDHi = 0x60;
	constexpr int kViewItemIDLo = 0x64;
	constexpr int kViewInventoryPos = 0x6C;
	constexpr int kViewNonSOCItem = 0x78;
	constexpr int kViewNonSOCItemID = 0x80;
	constexpr int kViewTeamNumber = 0xD0;
	constexpr int kViewInitialized = 0xD4;

	constexpr int kItemID = 0x48;
	constexpr int kItemAccountID = 0x50;
	constexpr int kItemInventoryPos = 0x54;
	constexpr int kItemDefIndex = 0x58;
	constexpr int kItemLevel = 0x5A;
	constexpr int kItemQuality = 0x5B;
	constexpr int kItemFlags = 0x5C;
	constexpr int kItemOrigin = 0x5D;
	constexpr int kItemDirtyBits = 0x5F;
	constexpr int kItemEquipClass = 0x60;
	constexpr int kItemEquipSlot = 0x62;
	constexpr uint8_t kItemHasEquipInstance = 0x2;

	constexpr uint64_t kClientItemMagic = 0x554E49ull;
	constexpr int kClientItemMagicShift = 40;
	constexpr uint32_t kClientSerialMask = 0xFFFFFFu;
	constexpr uint32_t kPositionPlaced = 0x80000000u;
	constexpr uint32_t kPositionUnacknowledged = 0x40000000u;
	constexpr int kQualityUnique = 6;
	constexpr int kQualityUnusual = 5;

	uint64_t ClientItemID(uint32_t uSerial, uint16_t iDef)
	{
		return (kClientItemMagic << kClientItemMagicShift) | (uint64_t(uSerial & kClientSerialMask) << 16) | iDef;
	}

	uint16_t ClientItemDef(uint64_t uItemID)
	{
		return uint16_t(uItemID & 0xFFFF);
	}

	uint32_t ClientItemSerial(uint64_t uItemID)
	{
		return uint32_t((uItemID >> 16) & kClientSerialMask);
	}

	CEconItemView* ConstructItemView(void* pMemory)
	{
		if (!S::CEconItemView_Constructor())
			return nullptr;
		return S::CEconItemView_Constructor.Call<CEconItemView*>(pMemory);
	}

	void DestroyItemView(void* pView)
	{
		using Destructor_t = void*(__fastcall*)(void*, uint32_t);
		(*reinterpret_cast<Destructor_t**>(pView))[0](pView, 0u);
	}

	void AssignItemView(CEconItemView* pDst, const CEconItemView* pSrc)
	{
		if (S::CEconItemView_Assign())
			S::CEconItemView_Assign.Call<void*>(pDst, pSrc);
	}

	void* GetSOCData(void* pView)
	{
		return S::CEconItemView_GetSOCData() ? S::CEconItemView_GetSOCData.Call<void*>(pView) : nullptr;
	}

	CEconItem* CreateEconItem()
	{
		if (!S::CEconItem_Constructor() || !S::MemAlloc_Zero())
			return nullptr;

		auto* pMemory = S::MemAlloc_Zero.Call<void*>(kEconItemSize);
		return pMemory ? S::CEconItem_Constructor.Call<CEconItem*>(pMemory) : nullptr;
	}

	void DestroyEconItem(void* pItem)
	{
		if (!pItem)
			return;

		using Destructor_t = void*(__fastcall*)(void*, uint32_t);
		(*reinterpret_cast<Destructor_t**>(pItem))[0](pItem, 1u);
	}

	bool AddEconItem(CTFPlayerInventory* pInventory, CEconItem* pItem)
	{
		if (!S::CTFPlayerInventory_AddEconItem())
			return false;
		return S::CTFPlayerInventory_AddEconItem.Call<bool>(pInventory, pItem, false, false, false);
	}

	void RemoveItem(CTFPlayerInventory* pInventory, uint64_t uID)
	{
		if (S::CPlayerInventory_RemoveItem())
			S::CPlayerInventory_RemoveItem.Call<void>(pInventory, uID);
	}

	int MaxItems(CTFPlayerInventory* pInventory)
	{
		return S::CTFPlayerInventory_GetMaxItems() ? S::CTFPlayerInventory_GetMaxItems.Call<int>(pInventory) : 0;
	}

	uint64_t ViewItemID(const CEconItemView* pView)
	{
		const auto* pBytes = reinterpret_cast<const uint8_t*>(pView);
		const uint32_t uHi = *reinterpret_cast<const uint32_t*>(pBytes + kViewItemIDHi);
		const uint32_t uLo = *reinterpret_cast<const uint32_t*>(pBytes + kViewItemIDLo);
		return (uint64_t(uHi) << 32) | uLo;
	}

	CEconItemView* FindItemView(CTFPlayerInventory* pInventory, uint64_t uID)
	{
		if (!pInventory)
			return nullptr;

		auto* pData = Field<uint8_t*>(pInventory, kInventoryItemsData);
		const int iCount = Field<int>(pInventory, kInventoryItemsCount);
		if (!pData)
			return nullptr;

		for (int i = 0; i < iCount; i++)
		{
			auto* pView = reinterpret_cast<CEconItemView*>(pData + size_t(i) * kEconItemViewSize);
			if (ViewItemID(pView) == uID)
				return pView;
		}
		return nullptr;
	}

	uint32_t PositionToken(uint32_t uPosition)
	{
		return uPosition ? (uPosition | kPositionPlaced) : 0;
	}

	void EquipItem(void* pItem, int iClass, int iSlot)
	{
		if (!pItem)
			return;

		Field<uint8_t>(pItem, kItemDirtyBits) |= kItemHasEquipInstance;
		Field<uint16_t>(pItem, kItemEquipClass) = uint16_t(iClass);
		Field<uint16_t>(pItem, kItemEquipSlot) = uint16_t(iSlot);
	}

	void UnequipItem(void* pItem)
	{
		if (!pItem)
			return;

		Field<uint8_t>(pItem, kItemDirtyBits) &= uint8_t(~kItemHasEquipInstance);
		Field<uint16_t>(pItem, kItemEquipClass) = 0;
		Field<uint16_t>(pItem, kItemEquipSlot) = 0xFFFF;
	}

	void ApplyUnusual(CEconItemView* pView, void* pItem, int iUnusual)
	{
		const int iQuality = iUnusual > 0 ? kQualityUnusual : kQualityUnique;
		if (pItem)
			Field<uint8_t>(pItem, kItemQuality) = uint8_t(iQuality);
		Field<int>(pView, kViewQuality) = iQuality;

		auto pList = reinterpret_cast<CAttributeList*>(uintptr_t(pView) + kItemViewAttributeList);
		pList->SetAttribute(kAttrAttachedParticle, float(std::max(iUnusual, 0)));
	}
}

CTFPlayerInventory* CSkinChanger::GetLocalInventory() const
{
	auto* pManager = I::TFInventoryManager();
	return pManager ? pManager->GetLocalInventory() : nullptr;
}

namespace
{
	constexpr int kEconEntityAttrManager = 0xC38;
	constexpr int kAttrContainerItem = 144;
	constexpr int kEconEntityClientside = 0xE21;
	constexpr int kMaxWearables = 8;
	constexpr int kRenderGroupOpaqueEntity = 2;
	constexpr float kClientThinkAlways = -1293.f;
	constexpr size_t kTFWearableSize = 0xEE0;

	constexpr int kLoadoutHead = 7;
	constexpr int kLoadoutMisc = 8;
	constexpr int kLoadoutMisc2 = 11;

	bool ResizeWearables(CTFPlayer* pPlayer, int iCount)
	{
		if (!pPlayer || !S::CUtlVector_SetCountHandle())
			return false;

		static const int nOffset = U::NetVars.GetNetVar("CBasePlayer", "m_szLastPlaceName") + 56;
		if (nOffset <= 0)
			return false;

		S::CUtlVector_SetCountHandle.Call<void>(pPlayer, nOffset, iCount);
		return pPlayer->m_hMyWearables().Count() == iCount;
	}

	bool IsWearableDef(uint16_t iDef)
	{
		for (const auto& tItem : CSkinChanger::GetItems())
		{
			if (tItem.iDef == iDef)
				return tItem.sClass == "tf_wearable" || tItem.sClass.starts_with("tf_wearable_");
		}
		return false;
	}

	const ItemInfo_t* FindItemInfo(uint16_t iDef)
	{
		static std::unordered_map<uint16_t, const ItemInfo_t*> s_mLookup = {};
		static size_t s_uSize = 0;
		static const ItemInfo_t* s_pData = nullptr;

		const auto& vItems = CSkinChanger::GetItems();
		if (s_uSize != vItems.size() || s_pData != vItems.data())
		{
			s_mLookup.clear();
			for (const auto& tItem : vItems)
				s_mLookup[tItem.iDef] = &tItem;
			s_uSize = vItems.size();
			s_pData = vItems.data();
		}

		auto it = s_mLookup.find(iDef);
		return it != s_mLookup.end() ? it->second : nullptr;
	}
}

namespace
{
	constexpr uint32_t kMsgUnlockCrate = 1007;
	constexpr uint32_t kMsgUnlockCrateSize = 46;
	constexpr int kMessageHeader = 8;
	constexpr int kMessagePayload = 16;
	constexpr int kMessageSize = 24;
	constexpr int kOriginFoundInCrate = 8;
	constexpr int kAttrCrateSeries = 187;
	constexpr uint64_t kCrateOpenDelay = 600;
	constexpr uint64_t kAwaitPositionTimeout = 20000;
	constexpr double kRareChance = 0.01;

	constexpr int kSchemaDefs = 312;
	constexpr int kSchemaDefCount = 336;
	constexpr int kSchemaRevolvingNodes = 960;
	constexpr int kSchemaRevolvingSlots = 968;
	constexpr int kSchemaRevolvingLast = 982;
	constexpr int kSchemaLootNodes = 1000;
	constexpr int kSchemaLootSlots = 1008;
	constexpr int kSchemaLootLast = 1028;
	constexpr size_t kRevolvingNodeSize = 24;
	constexpr size_t kLootNodeSize = 32;
	constexpr int kDefStaticAttrs = 48;
	constexpr int kDefStaticAttrCount = 64;
	constexpr size_t kStaticAttrSize = 16;
	constexpr int kLootEntries = 40;
	constexpr int kLootEntryCount = 56;
	constexpr size_t kLootEntrySize = 16;
	constexpr int kListenerData = 64;
	constexpr int kListenerCount = 80;
	constexpr int kCacheOwner = 56;
	constexpr int kEventSourceCreated = 4;

	bool ReadMessage(void* pMessage, uint32_t& uType, uint32_t& uSize, uint64_t& uKey, uint64_t& uCrate)
	{
		__try
		{
			auto* pHeader = Field<uint8_t*>(pMessage, kMessageHeader);
			auto* pPayload = Field<uint8_t*>(pMessage, kMessagePayload);
			if (!pHeader || !pPayload)
				return false;

			uType = *reinterpret_cast<uint32_t*>(pHeader);
			uSize = Field<uint32_t>(pMessage, kMessageSize);
			if (uType != kMsgUnlockCrate || uSize != kMsgUnlockCrateSize)
				return false;

			uKey = *reinterpret_cast<uint64_t*>(pPayload);
			uCrate = *reinterpret_cast<uint64_t*>(pPayload + 8);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void* FindItemDef(void* pSchema, uint16_t iDef)
	{
		const int iCount = Field<int>(pSchema, kSchemaDefCount);
		auto* pData = Field<uint8_t*>(pSchema, kSchemaDefs);
		if (!pData)
			return nullptr;

		for (int i = 0; i < iCount; i++)
		{
			auto* pEntry = pData + size_t(i) * 24;
			if (Field<int>(pEntry, 16) < -1)
				continue;

			auto* pItemDef = Field<void*>(pEntry, 8);
			if (pItemDef && Field<uint16_t>(pItemDef, 0x10) == iDef)
				return pItemDef;
		}
		return nullptr;
	}

	int CrateSeries(void* pDef)
	{
		auto* pData = Field<uint8_t*>(pDef, kDefStaticAttrs);
		const int iCount = Field<int>(pDef, kDefStaticAttrCount);
		if (!pData)
			return 0;

		for (int i = 0; i < iCount; i++)
		{
			auto* pAttribute = pData + size_t(i) * kStaticAttrSize;
			if (Field<uint16_t>(pAttribute, 0) != kAttrCrateSeries)
				continue;

			const uint32_t uRaw = Field<uint32_t>(pAttribute, 8);
			return uRaw < 0x00800000u ? int(uRaw) : int(std::bit_cast<float>(uRaw));
		}
		return 0;
	}

	const char* RevolvingListName(void* pSchema, int iSeries)
	{
		auto* pNodes = Field<uint8_t*>(pSchema, kSchemaRevolvingNodes);
		const uint32_t uSlots = Field<uint32_t>(pSchema, kSchemaRevolvingSlots);
		const uint16_t uLast = Field<uint16_t>(pSchema, kSchemaRevolvingLast);
		if (!pNodes || uLast == 0xFFFF)
			return nullptr;

		for (uint32_t i = 0; i < uSlots && i <= uLast; i++)
		{
			auto* pNode = pNodes + size_t(i) * kRevolvingNodeSize;
			if (Field<uint16_t>(pNode, 0) == i || Field<int>(pNode, 8) != iSeries)
				continue;

			return Field<const char*>(pNode, 16);
		}
		return nullptr;
	}

	void* LootListByIndex(void* pSchema, int iIndex)
	{
		auto* pNodes = Field<uint8_t*>(pSchema, kSchemaLootNodes);
		const int iSlots = Field<int>(pSchema, kSchemaLootSlots);
		const int iLast = Field<int>(pSchema, kSchemaLootLast);
		if (!pNodes || iIndex < 0 || iIndex >= iSlots || iLast == -1 || iIndex > iLast)
			return nullptr;

		auto* pNode = pNodes + size_t(iIndex) * kLootNodeSize;
		return Field<int>(pNode, 0) == iIndex ? nullptr : Field<void*>(pNode, 24);
	}

	void* LootListByName(void* pSchema, const char* sName)
	{
		if (!sName || !sName[0])
			return nullptr;

		auto* pNodes = Field<uint8_t*>(pSchema, kSchemaLootNodes);
		const int iSlots = Field<int>(pSchema, kSchemaLootSlots);
		const int iLast = Field<int>(pSchema, kSchemaLootLast);
		if (!pNodes || iLast == -1)
			return nullptr;

		for (int i = 0; i < iSlots && i <= iLast; i++)
		{
			auto* pNode = pNodes + size_t(i) * kLootNodeSize;
			if (Field<int>(pNode, 0) == i)
				continue;

			const char* sKey = Field<const char*>(pNode, 16);
			if (sKey && !strcmp(sKey, sName))
				return Field<void*>(pNode, 24);
		}
		return nullptr;
	}

	void CollectLootList(void* pSchema, void* pList, std::vector<uint16_t>& vCommon, std::vector<uint16_t>& vRare, int iDepth)
	{
		if (!pList || iDepth > 8)
			return;

		auto* pEntries = Field<uint8_t*>(pList, kLootEntries);
		const int iCount = Field<int>(pList, kLootEntryCount);
		if (!pEntries)
			return;

		for (int i = 0; i < iCount; i++)
		{
			const int iEntry = Field<int>(pEntries + size_t(i) * kLootEntrySize, 0);
			if (iEntry > 0 && iEntry <= 0xFFFF)
				(iDepth ? vRare : vCommon).push_back(uint16_t(iEntry));
			else if (iEntry < 0)
				CollectLootList(pSchema, LootListByIndex(pSchema, -1 - iEntry), vCommon, vRare, iDepth + 1);
		}
	}

	int RandomUnusualEffect()
	{
		static std::vector<int> s_vEffects = {};
		if (s_vEffects.empty() && I::Localize)
		{
			for (int i = 1; i <= 3242; i++)
			{
				const char* sName = I::Localize->FindAsUTF8(std::format("#Attrib_Particle{}", i).c_str());
				if (!sName || !sName[0])
					continue;

				const std::string sEffect = sName;
				if (sEffect == "Invalid Particle" || sEffect.starts_with("Particle ") || sEffect.find("Attrib_Particle") != std::string::npos)
					continue;
				s_vEffects.push_back(i);
			}
		}
		if (s_vEffects.empty())
			return 0;

		static std::mt19937 s_tRandom{ std::random_device{}() };
		return s_vEffects[std::uniform_int_distribution<size_t>(0, s_vEffects.size() - 1)(s_tRandom)];
	}

	void NotifyCreated(CTFPlayerInventory* pInventory, void* pItem)
	{
		auto* pCache = Field<uint8_t*>(pInventory, kInventorySOCache);
		if (!pCache)
			return;

		auto** ppListeners = Field<void**>(pCache, kListenerData);
		const int iCount = Field<int>(pCache, kListenerCount);
		if (!ppListeners || iCount <= 0)
			return;

		std::vector<void*> vListeners(ppListeners, ppListeners + iCount);
		using Created_t = void(__fastcall*)(void*, void*, void*, int);
		for (void* pListener : vListeners)
		{
			if (pListener)
				(*reinterpret_cast<Created_t**>(pListener))[0](pListener, pCache + kCacheOwner, pItem, kEventSourceCreated);
		}
	}
}

bool CSkinChanger::OnSendMessage(void* pMessage)
{
	if (!pMessage || !ClientInventoryActive())
		return false;

	uint32_t uType = 0, uSize = 0;
	uint64_t uKey = 0, uCrate = 0;
	if (!ReadMessage(pMessage, uType, uSize, uKey, uCrate))
		return false;

	std::lock_guard tLock(m_tMutex);
	m_vPendingCrates.push_back({ uKey, uCrate, GetTickCount64() });
	return true;
}

void CSkinChanger::ProcessCrates(CTFPlayerInventory* pInventory)
{
	std::vector<PendingCrate_t> vDue = {};
	{
		std::lock_guard tLock(m_tMutex);
		const uint64_t uNow = GetTickCount64();
		for (auto it = m_vPendingCrates.begin(); it != m_vPendingCrates.end();)
		{
			if (uNow - it->uTick < kCrateOpenDelay)
			{
				++it;
				continue;
			}
			vDue.push_back(*it);
			it = m_vPendingCrates.erase(it);
		}
	}

	for (const auto& tCrate : vDue)
		OpenCrate(pInventory, tCrate);
}

bool CSkinChanger::OpenCrate(CTFPlayerInventory* pInventory, const PendingCrate_t& tCrate)
{
	auto* pSchema = CEconItemSchema::GetInstance();
	auto* pCrateView = FindItemView(pInventory, tCrate.uCrate);
	if (!pSchema || !pCrateView)
		return false;

	auto* pCrateDef = FindItemDef(pSchema, Field<uint16_t>(pCrateView, kViewDefIndex));
	const int iSeries = pCrateDef ? CrateSeries(pCrateDef) : 0;
	auto* pList = iSeries > 0 ? LootListByName(pSchema, RevolvingListName(pSchema, iSeries)) : nullptr;
	if (!pList)
		return false;

	std::vector<uint16_t> vCommon = {}, vRare = {};
	CollectLootList(pSchema, pList, vCommon, vRare, 0);

	auto fFilter = [](std::vector<uint16_t>& vDefs)
	{
		std::vector<uint16_t> vAvailable = {};
		for (uint16_t iDef : vDefs)
		{
			if (FindItemInfo(iDef) && std::find(vAvailable.begin(), vAvailable.end(), iDef) == vAvailable.end())
				vAvailable.push_back(iDef);
		}
		vDefs = std::move(vAvailable);
	};
	fFilter(vCommon);
	fFilter(vRare);
	if (vCommon.empty() && vRare.empty())
		return false;

	static std::mt19937 s_tRandom{ std::random_device{}() };
	const bool bRare = !vRare.empty() && (vCommon.empty() || std::uniform_real_distribution<double>(0.0, 1.0)(s_tRandom) < kRareChance);
	auto& vPool = bRare ? vRare : vCommon;
	const uint16_t iDef = vPool[std::uniform_int_distribution<size_t>(0, vPool.size() - 1)(s_tRandom)];
	const int iUnusual = bRare && IsWearableDef(iDef) ? RandomUnusualEffect() : 0;

	uint32_t uAccountID = Field<uint32_t>(pInventory, kInventoryOwner);
	if (!uAccountID && I::SteamUser)
		uAccountID = I::SteamUser->GetSteamID().GetAccountID();

	auto* pItem = CreateEconItem();
	if (!pItem)
		return false;

	uint32_t uSerial = 0;
	{
		std::lock_guard tLock(m_tMutex);
		uSerial = NextSerial();
	}
	if (!uSerial)
	{
		DestroyEconItem(pItem);
		return false;
	}

	const uint64_t uID = ClientItemID(uSerial, iDef);
	Field<uint64_t>(pItem, kItemID) = uID;
	Field<uint32_t>(pItem, kItemAccountID) = uAccountID;
	Field<uint32_t>(pItem, kItemInventoryPos) = 0;
	Field<uint16_t>(pItem, kItemDefIndex) = iDef;
	Field<uint8_t>(pItem, kItemLevel) = 1;
	Field<uint8_t>(pItem, kItemQuality) = uint8_t(iUnusual > 0 ? kQualityUnusual : kQualityUnique);
	Field<uint8_t>(pItem, kItemFlags) = 0;
	Field<uint8_t>(pItem, kItemOrigin) = uint8_t(kOriginFoundInCrate);

	{
		std::lock_guard tLock(m_tMutex);
		m_mClientItemsByID[uID] = pItem;
		m_vBackpack.push_back({ uSerial, iDef, iUnusual });
		m_mAwaitingPosition[uID] = GetTickCount64();
	}

	NotifyCreated(pInventory, pItem);
	if (auto* pView = FindItemView(pInventory, uID))
	{
		Field<void*>(pView, kViewNonSOCItem) = pItem;
		Field<uint64_t>(pView, kViewNonSOCItemID) = uID;
		ApplyUnusual(pView, pItem, iUnusual);
		m_mAppliedUnusual[uID] = iUnusual;
	}

	ApplyLocalDelete(tCrate.uCrate);
	if (tCrate.uKey != ~0ull)
		ApplyLocalDelete(tCrate.uKey);
	return true;
}

bool CSkinChanger::ClientInventoryActive()
{
	std::lock_guard tLock(m_tMutex);
	return !m_vBackpack.empty();
}

bool CSkinChanger::IsClientItem(uint64_t uItemID)
{
	return (uItemID >> kClientItemMagicShift) == kClientItemMagic;
}

void* CSkinChanger::GetClientSOCData(uint64_t uItemID)
{
	std::lock_guard tLock(m_tMutex);
	auto it = m_mClientItemsByID.find(uItemID);
	return it != m_mClientItemsByID.end() ? it->second : nullptr;
}

std::map<int, uint64_t> CSkinChanger::GetLoadout()
{
	std::lock_guard tLock(m_tMutex);
	return m_mLoadout;
}

void CSkinChanger::SetLoadout(std::map<int, uint64_t> mLoadout)
{
	std::lock_guard tLock(m_tMutex);
	m_mLoadout = std::move(mLoadout);
}

void CSkinChanger::ApplyLocalLoadout(uint64_t uItemID, int iClass, int iSlot)
{
	auto* pInventory = GetLocalInventory();
	if (!pInventory || iClass < 1 || iClass > kLoadoutMaxClass || iSlot < 0 || iSlot >= kLoadoutSlots)
		return;

	const int iIndex = iClass * kLoadoutSlots + iSlot;
	const uint64_t uNew = uItemID == ~0ull ? 0 : uItemID;
	auto& uSlot = Field<uint64_t>(pInventory, kInventoryLoadout + size_t(iIndex) * sizeof(uint64_t));
	const uint64_t uPrevious = uSlot;
	uSlot = uNew;
	Field<bool>(pInventory, kInventoryLoadoutChanged + iClass) = true;

	if (uNew)
	{
		if (auto* pView = FindItemView(pInventory, uNew))
			EquipItem(GetSOCData(pView), iClass, iSlot);
	}
	if (uPrevious && uPrevious != uNew)
	{
		if (auto* pView = FindItemView(pInventory, uPrevious))
			UnequipItem(GetSOCData(pView));
	}

	std::lock_guard tLock(m_tMutex);
	m_mLoadout[iIndex] = uNew;
}

void CSkinChanger::ApplyLocalPosition(uint64_t uItemID, uint32_t uPosition)
{
	auto* pInventory = GetLocalInventory();
	auto* pView = FindItemView(pInventory, uItemID);
	if (!pView)
		return;

	const uint32_t uToken = PositionToken(uPosition);
	Field<uint32_t>(pView, kViewInventoryPos) = uToken;
	if (auto* pItem = GetSOCData(pView))
		Field<uint32_t>(pItem, kItemInventoryPos) = uToken;
}

void CSkinChanger::RemoveClientItem(CTFPlayerInventory* pInventory, uint64_t uItemID)
{
	if (pInventory && FindItemView(pInventory, uItemID))
		RemoveItem(pInventory, uItemID);

	std::lock_guard tLock(m_tMutex);
	if (auto it = m_mClientItemsByID.find(uItemID); it != m_mClientItemsByID.end())
	{
		m_vGraveyard.push_back(it->second);
		m_mClientItemsByID.erase(it);
	}
	m_mAppliedUnusual.erase(uItemID);
	m_mAwaitingPosition.erase(uItemID);
	std::erase_if(m_mLoadout, [uItemID](const auto& tPair) { return tPair.second == uItemID; });
}

void CSkinChanger::ApplyLocalDelete(uint64_t uItemID)
{
	auto* pInventory = GetLocalInventory();
	if (!IsClientItem(uItemID))
	{
		if (pInventory && FindItemView(pInventory, uItemID))
			RemoveItem(pInventory, uItemID);

		std::lock_guard tLock(m_tMutex);
		std::erase_if(m_mLoadout, [uItemID](const auto& tPair) { return tPair.second == uItemID; });
		return;
	}

	{
		std::lock_guard tLock(m_tMutex);
		const uint32_t uSerial = ClientItemSerial(uItemID);
		std::erase_if(m_vBackpack, [uSerial](const BackpackItem_t& tItem) { return tItem.uSerial == uSerial; });
	}
	RemoveClientItem(pInventory, uItemID);
}

void CSkinChanger::ClearClientItems()
{
	auto* pInventory = GetLocalInventory();

	std::vector<uint64_t> vIDs = {};
	{
		std::lock_guard tLock(m_tMutex);
		for (const auto& [uID, pItem] : m_mClientItemsByID)
			vIDs.push_back(uID);
	}

	for (uint64_t uID : vIDs)
	{
		if (pInventory && FindItemView(pInventory, uID))
			RemoveItem(pInventory, uID);
	}

	std::lock_guard tLock(m_tMutex);
	for (auto& [uID, pItem] : m_mClientItemsByID)
		m_vGraveyard.push_back(pItem);
	m_mClientItemsByID.clear();
	m_mAppliedUnusual.clear();
	m_mAwaitingPosition.clear();
	m_tFailedItems.clear();
	m_mInventoryCounts.clear();
}

void CSkinChanger::ReapplyLoadout(CTFPlayerInventory* pInventory)
{
	std::map<int, uint64_t> mLoadout = {};
	{
		std::lock_guard tLock(m_tMutex);
		mLoadout = m_mLoadout;
	}

	for (const auto& [iIndex, uItemID] : mLoadout)
	{
		const int iClass = iIndex / kLoadoutSlots, iSlot = iIndex % kLoadoutSlots;
		if (iClass < 1 || iClass > kLoadoutMaxClass)
			continue;

		auto& uSlot = Field<uint64_t>(pInventory, kInventoryLoadout + size_t(iIndex) * sizeof(uint64_t));
		if (uSlot == uItemID)
			continue;

		auto* pView = uItemID ? FindItemView(pInventory, uItemID) : nullptr;
		if (uItemID && !pView)
			continue;

		uSlot = uItemID;
		Field<bool>(pInventory, kInventoryLoadoutChanged + iClass) = true;
		if (pView)
			EquipItem(GetSOCData(pView), iClass, iSlot);
	}
}

void CSkinChanger::SyncInventory()
{
	const uint64_t uNow = GetTickCount64();
	if (uNow - m_uLastSync < 250)
		return;
	m_uLastSync = uNow;

	auto* pInventory = GetLocalInventory();
	if (!pInventory || !Field<bool>(pInventory, kInventoryGotItems) || !Field<void*>(pInventory, kInventorySOCache))
		return;

	ProcessCrates(pInventory);

	auto* pData = Field<uint8_t*>(pInventory, kInventoryItemsData);
	const int iCount = Field<int>(pInventory, kInventoryItemsCount);
	const int iMax = std::max(MaxItems(pInventory), 0);
	if (!pData)
		return;

	std::unordered_set<uint64_t> tPresent = {};
	std::unordered_map<uint16_t, int> mDefCount = {};
	std::vector<bool> vUsed(size_t(iMax) + 1, false);
	tPresent.reserve(size_t(iCount));
	for (int i = 0; i < iCount; i++)
	{
		auto* pView = reinterpret_cast<CEconItemView*>(pData + size_t(i) * kEconItemViewSize);
		tPresent.insert(ViewItemID(pView));
		if (const uint16_t iDef = Field<uint16_t>(pView, kViewDefIndex))
			mDefCount[iDef]++;

		const uint32_t uToken = Field<uint32_t>(pView, kViewInventoryPos);
		if (uToken && !(uToken & kPositionUnacknowledged))
		{
			if (const size_t uPosition = uToken & 0xFFFF; uPosition <= size_t(iMax))
				vUsed[uPosition] = true;
		}
	}

	std::vector<BackpackItem_t> vWanted = {};
	std::unordered_set<uint64_t> tWantedIDs = {};
	std::vector<uint64_t> vRemove = {};
	{
		std::lock_guard tLock(m_tMutex);
		vWanted = m_vBackpack;
		for (const auto& tItem : vWanted)
			tWantedIDs.insert(ClientItemID(tItem.uSerial, tItem.iDef));
		for (const auto& [uID, pItem] : m_mClientItemsByID)
		{
			if (!tWantedIDs.contains(uID))
				vRemove.push_back(uID);
		}
		for (auto it = m_tFailedItems.begin(); it != m_tFailedItems.end();)
			it = tWantedIDs.contains(*it) ? std::next(it) : m_tFailedItems.erase(it);
	}

	for (uint64_t uID : vRemove)
	{
		if (tPresent.erase(uID))
		{
			if (--mDefCount[ClientItemDef(uID)] <= 0)
				mDefCount.erase(ClientItemDef(uID));
		}
		RemoveClientItem(pInventory, uID);
	}

	uint32_t uAccountID = Field<uint32_t>(pInventory, kInventoryOwner);
	if (!uAccountID && I::SteamUser)
		uAccountID = I::SteamUser->GetSteamID().GetAccountID();

	uint32_t uNextPosition = 1;
	auto fFreePosition = [&]() -> uint32_t
	{
		while (uNextPosition <= uint32_t(iMax) && vUsed[uNextPosition])
			uNextPosition++;
		return uNextPosition <= uint32_t(iMax) ? uNextPosition : 0;
	};
	for (const auto& tItem : vWanted)
	{
		if (!uAccountID)
			break;

		const uint16_t iDef = tItem.iDef;
		const uint64_t uID = ClientItemID(tItem.uSerial, iDef);
		const int iUnusual = std::max(tItem.iUnusual, 0);

		void* pItem = GetClientSOCData(uID);
		if (tPresent.contains(uID))
		{
			const auto itApplied = m_mAppliedUnusual.find(uID);
			if (pItem && (itApplied != m_mAppliedUnusual.end() ? itApplied->second : 0) != iUnusual)
			{
				if (auto* pView = FindItemView(pInventory, uID))
				{
					ApplyUnusual(pView, pItem, iUnusual);
					m_mAppliedUnusual[uID] = iUnusual;
				}
			}
			continue;
		}
		if (m_tFailedItems.contains(uID))
			continue;

		const uint32_t uPosition = fFreePosition();
		if (!uPosition)
			break;

		const bool bCreated = !pItem;
		if (bCreated)
		{
			pItem = CreateEconItem();
			if (!pItem)
			{
				m_tFailedItems.insert(uID);
				continue;
			}

			Field<uint64_t>(pItem, kItemID) = uID;
			Field<uint16_t>(pItem, kItemDefIndex) = iDef;
			Field<uint8_t>(pItem, kItemLevel) = 1;
			Field<uint8_t>(pItem, kItemFlags) = 0;
			Field<uint8_t>(pItem, kItemOrigin) = 2;
		}
		Field<uint32_t>(pItem, kItemAccountID) = uAccountID;
		Field<uint32_t>(pItem, kItemInventoryPos) = PositionToken(uPosition);
		Field<uint8_t>(pItem, kItemQuality) = uint8_t(iUnusual > 0 ? kQualityUnusual : kQualityUnique);

		{
			std::lock_guard tLock(m_tMutex);
			m_mClientItemsByID[uID] = pItem;
		}

		AddEconItem(pInventory, static_cast<CEconItem*>(pItem));
		auto* pView = FindItemView(pInventory, uID);
		if (!pView)
		{
			std::lock_guard tLock(m_tMutex);
			m_mClientItemsByID.erase(uID);
			DestroyEconItem(pItem);
			m_tFailedItems.insert(uID);
			continue;
		}

		Field<void*>(pView, kViewNonSOCItem) = pItem;
		Field<uint64_t>(pView, kViewNonSOCItemID) = uID;
		ApplyUnusual(pView, pItem, iUnusual);
		m_mAppliedUnusual[uID] = iUnusual;

		vUsed[uPosition] = true;
		tPresent.insert(uID);
		mDefCount[iDef]++;
	}

	std::vector<std::pair<uint64_t, uint64_t>> vAwaiting = {};
	{
		std::lock_guard tLock(m_tMutex);
		vAwaiting.assign(m_mAwaitingPosition.begin(), m_mAwaitingPosition.end());
	}
	for (const auto& [uID, uTick] : vAwaiting)
	{
		if (uNow - uTick < kAwaitPositionTimeout)
			continue;

		bool bDone = !tPresent.contains(uID);
		if (!bDone)
		{
			auto* pView = FindItemView(pInventory, uID);
			const uint32_t uToken = pView ? Field<uint32_t>(pView, kViewInventoryPos) : 0;
			bDone = !pView || (uToken && !(uToken & kPositionUnacknowledged));
			if (!bDone)
			{
				if (const uint32_t uPosition = fFreePosition())
				{
					Field<uint32_t>(pView, kViewInventoryPos) = PositionToken(uPosition);
					if (auto* pItem = GetSOCData(pView))
						Field<uint32_t>(pItem, kItemInventoryPos) = PositionToken(uPosition);
					vUsed[uPosition] = true;
					bDone = true;
				}
			}
		}
		if (bDone)
		{
			std::lock_guard tLock(m_tMutex);
			m_mAwaitingPosition.erase(uID);
		}
	}

	ReapplyLoadout(pInventory);

	std::lock_guard tLock(m_tMutex);
	m_mInventoryCounts = std::move(mDefCount);
}

CEconItemView* CSkinChanger::GetWearableView(int iClass, int iSlot, int iTeam, int iDef, int iUnusual)
{
	if (iSlot < 0 || iSlot >= 19 || iDef <= 0)
		return nullptr;

	const uint64_t uKey = (uint64_t(uint32_t(iClass) & 0xF) << 44) | (uint64_t(uint32_t(iTeam) & 0xF) << 40)
		| (uint64_t(uint32_t(iUnusual) & 0xFFFF) << 24) | uint64_t(uint32_t(iDef) & 0xFFFF);
	auto& tView = m_aWearableViews[iSlot];
	auto* pView = reinterpret_cast<CEconItemView*>(tView.aData);
	if (m_aWearableViewBuilt[iSlot] && m_aWearableViewKey[iSlot] == uKey && Field<uint16_t>(pView, kViewDefIndex) == uint16_t(iDef))
		return pView;

	if (m_aWearableViewBuilt[iSlot])
	{
		DestroyItemView(pView);
		m_aWearableViewBuilt[iSlot] = false;
	}
	if (!ConstructItemView(tView.aData))
		return nullptr;
	m_aWearableViewBuilt[iSlot] = true;

	Field<uint16_t>(pView, kViewDefIndex) = uint16_t(iDef);
	Field<int>(pView, kViewTeamNumber) = iTeam;
	Field<bool>(pView, kViewInitialized) = true;

	const uint32_t uHigh = 0xFFFF0000u | uint32_t(iClass);
	const uint32_t uLow = 0x80000000u | (uint32_t(iDef) << 8) | uint32_t(iSlot);
	auto pMirror = reinterpret_cast<uint32_t*>(tView.aData + 88);
	pMirror[0] = uLow;
	pMirror[1] = uHigh;
	auto pID = reinterpret_cast<uint32_t*>(tView.aData + 96);
	pID[0] = uHigh;
	pID[1] = uLow;

	if (iUnusual > 0)
	{
		auto pList = reinterpret_cast<CAttributeList*>(tView.aData + kItemViewAttributeList);
		pList->SetAttribute(kAttrAttachedParticle, float(iUnusual));
	}
	m_aWearableViewKey[iSlot] = uKey;
	return pView;
}

CEconWearable* CSkinChanger::CreateWearable(CTFPlayer* pLocal, CEconItemView* pView, int iClass, int iTeam)
{
	if (!pLocal || !pView || !S::CTFWearable_Constructor() || !S::CEconEntity_InitializeAttributes()
		|| !S::CEconItemView_GetPlayerDisplayModel() || !S::CBaseEntity_SetOwnerEntity() || !S::MemAlloc_Zero())
		return nullptr;

	const char* sModel = S::CEconItemView_GetPlayerDisplayModel.Call<const char*>(pView, iClass, iTeam);
	if (!sModel || !sModel[0] || !I::ModelInfoClient)
		return nullptr;

	auto* pWearable = S::CTFWearable_Constructor.Call<CEconWearable*>(S::MemAlloc_Zero.Call<void*>(kTFWearableSize));
	if (!pWearable)
		return nullptr;

	AssignItemView(reinterpret_cast<CEconItemView*>(uintptr_t(pWearable) + kEconEntityAttrManager + kAttrContainerItem), pView);
	*reinterpret_cast<byte*>(uintptr_t(pWearable) + kEconEntityClientside) = 1;
	S::CBaseEntity_SetOwnerEntity.Call<void>(pWearable, pLocal);
	S::CEconEntity_InitializeAttributes.Call<void>(pWearable);

	if (!pWearable->InitializeAsClientEntity(sModel, kRenderGroupOpaqueEntity))
	{
		pWearable->ReleaseClientEntity();
		return nullptr;
	}

	pWearable->Spawn();
	pWearable->SetNextClientThink(kClientThinkAlways);
	pWearable->Equip(pLocal);
	return pWearable;
}

bool CSkinChanger::AppendWearable(CTFPlayer* pLocal, CEconWearable* pWearable)
{
	if (!pLocal || !pWearable)
		return false;

	const int iHandle = pWearable->GetRefEHandle().ToInt();
	auto& tVector = pLocal->m_hMyWearables();
	for (int i = 0; i < tVector.Count(); i++)
	{
		if (tVector[i].ToInt() == iHandle)
			return true;
	}

	if (tVector.Count() >= kMaxWearables)
		return false;

	const int iIndex = tVector.Count();
	if (!ResizeWearables(pLocal, iIndex + 1))
		return false;

	tVector[iIndex] = CHandle<CEconWearable>(pWearable);
	return true;
}

void CSkinChanger::RemoveWearable(CTFPlayer* pLocal, Wearable_t& tWearable)
{
	auto* pEntity = tWearable.hEntity.Get();
	tWearable.hEntity.Term();
	tWearable.iDef = -1;
	tWearable.iClass = -1;
	tWearable.iTeam = -1;
	tWearable.iUnusual = -1;
	if (!pEntity)
		return;

	if (pLocal)
	{
		const int iHandle = pEntity->GetRefEHandle().ToInt();
		auto& tVector = pLocal->m_hMyWearables();
		for (int i = tVector.Count() - 1; i >= 0; i--)
		{
			if (tVector[i].ToInt() != iHandle)
				continue;

			for (int j = i; j < tVector.Count() - 1; j++)
				tVector[j] = tVector[j + 1];

			ResizeWearables(pLocal, tVector.Count() - 1);
			break;
		}

		pEntity->UnEquip(pLocal);
	}

	pEntity->ReleaseClientEntity();
}

void CSkinChanger::ResetWearables(CTFPlayer* pLocal)
{
	auto* pOwner = pLocal ? pLocal : m_hWearableOwner.Get();
	bool bAny = false;
	for (auto& tWearable : m_aWearables)
	{
		bAny = bAny || tWearable.hEntity.Get() != nullptr;
		RemoveWearable(pOwner, tWearable);
	}

	if (bAny && pOwner)
		pOwner->SetBodygroupsDirty();
	m_hWearableOwner.Term();
}

void CSkinChanger::ClearWearables()
{
	for (auto& tWearable : m_aWearables)
		tWearable = {};
	for (auto& uKey : m_aWearableViewKey)
		uKey = 0;
	m_hWearableOwner.Term();
}

void CSkinChanger::UpdateWearables(CTFPlayer* pLocal)
{
	if (!Vars::Visuals::SkinChanger::Enabled.Value)
	{
		if (m_hWearableOwner.IsValid() || pLocal)
			ResetWearables(pLocal);
		return;
	}

	if (!pLocal || !pLocal->IsAlive())
	{
		if (m_hWearableOwner.IsValid() || pLocal)
			ResetWearables(pLocal);
		return;
	}

	if (auto* pOwner = m_hWearableOwner.Get(); pOwner && pOwner != pLocal)
		ResetWearables(pOwner);
	m_hWearableOwner.Set(pLocal);

	const int iClass = pLocal->m_iClass();
	const int iTeam = pLocal->m_iTeamNum();
	if (iClass < 1 || iClass > 9)
	{
		ResetWearables(pLocal);
		return;
	}

	auto* pInventory = GetLocalInventory();
	if (!pInventory)
		return;

	const bool bFullUpdate = I::ClientState && I::ClientState->m_nDeltaTick == -1;
	bool bBodygroupsDirty = false;
	for (int iSlot = kLoadoutHead; iSlot <= kLoadoutMisc2; iSlot++)
	{
		auto& tWearable = m_aWearables[iSlot];
		auto* pEntity = tWearable.hEntity.Get();

		auto* pLoadoutView = pInventory->GetItemInLoadout(iClass, iSlot);
		const bool bClientItem = pLoadoutView && IsClientItem(pLoadoutView->UUID());
		const int iDef = bClientItem ? pLoadoutView->GetDefinitionIndex() : 0;
		if (iDef <= 0 || !IsWearableDef(uint16_t(iDef)))
		{
			if (pEntity)
			{
				RemoveWearable(pLocal, tWearable);
				bBodygroupsDirty = true;
			}
			continue;
		}

		const int iUnusual = GetItemUnusual(pLoadoutView->UUID());
		if (pEntity)
		{
			if (tWearable.iDef != iDef || tWearable.iClass != iClass || tWearable.iTeam != iTeam || tWearable.iUnusual != iUnusual)
			{
				RemoveWearable(pLocal, tWearable);
				bBodygroupsDirty = true;
			}
			else
			{
				if (pEntity->GetOwnerEntity() != pLocal)
					pEntity->Equip(pLocal);
				AppendWearable(pLocal, pEntity);
				continue;
			}
		}

		auto* pView = GetWearableView(iClass, iSlot, iTeam, iDef, iUnusual);
		if (!pView)
			continue;

		auto* pCreated = CreateWearable(pLocal, pView, iClass, iTeam);
		if (!pCreated)
			continue;

		if (!AppendWearable(pLocal, pCreated))
		{
			pCreated->UnEquip(pLocal);
			pCreated->ReleaseClientEntity();
			continue;
		}

		tWearable.hEntity.Set(pCreated);
		tWearable.iDef = iDef;
		tWearable.iClass = iClass;
		tWearable.iTeam = iTeam;
		tWearable.iUnusual = iUnusual;
		bBodygroupsDirty = true;
	}

	if (bBodygroupsDirty)
		pLocal->SetBodygroupsDirty();
}

void CSkinChanger::CacheMenuInfo(CTFWeaponBase* pWeapon)
{
	std::lock_guard tLock(m_tMutex);

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

	m_tHeld = tHeld;
}

namespace
{
	size_t ReadableSpan(const void* pAddress)
	{
		if (!pAddress)
			return 0;

		MEMORY_BASIC_INFORMATION tInfo = {};
		if (!VirtualQuery(pAddress, &tInfo, sizeof(tInfo)) || tInfo.State != MEM_COMMIT)
			return 0;
		if (tInfo.Protect & (PAGE_NOACCESS | PAGE_GUARD))
			return 0;

		return uintptr_t(tInfo.BaseAddress) + tInfo.RegionSize - uintptr_t(pAddress);
	}

	bool IsReadableMemory(const void* pAddress, size_t uSize)
	{
		return uSize && ReadableSpan(pAddress) >= uSize;
	}

	std::string ReadString(const char* sValue)
	{
		const size_t uSpan = ReadableSpan(sValue);
		if (!uSpan || !sValue[0])
			return {};

		size_t uLength = 0;
		const size_t uMaxLength = std::min<size_t>(uSpan, 128);
		while (uLength < uMaxLength && sValue[uLength])
			uLength++;
		return std::string(sValue, uLength);
	}

	std::string PrettifyItemName(const std::string& sName)
	{
		std::string sReturn = sName;
		if (!sReturn.empty() && sReturn[0] == '#')
			sReturn.erase(0, 1);
		if (sReturn.starts_with("Upgradeable "))
			sReturn.erase(0, 12);
		if (sReturn.starts_with("TF_WEAPON_"))
			sReturn.erase(0, 10);
		else if (sReturn.starts_with("TF_"))
			sReturn.erase(0, 3);
		if (sReturn.starts_with("ItemName_"))
			sReturn.erase(0, 9);
		std::replace(sReturn.begin(), sReturn.end(), '_', ' ');
		return sReturn;
	}

	std::string LocalizeItemName(const std::string& sName)
	{
		if (sName.empty())
			return {};

		if (I::Localize)
		{
			const std::string sToken = sName[0] == '#' ? sName.substr(1) : sName;
			for (const std::string& sLookup : { sName, sToken })
			{
				const char* sLocalized = I::Localize->FindAsUTF8(sLookup.c_str());
				if (sLocalized && sLocalized[0] && sLookup != sLocalized && sToken != sLocalized)
					return sLocalized;
			}
		}

		return PrettifyItemName(sName);
	}

	bool IsUnresolvedItemName(const std::string& sName)
	{
		return !sName.empty() && sName[0] == '#';
	}
}

const std::vector<ItemInfo_t>& CSkinChanger::GetItems()
{
	static std::vector<ItemInfo_t> s_vItems = {};
	static CEconItemSchema* s_pSchema = nullptr;
	static int s_iSchemaCount = -1;
	static bool s_bLocalizeReady = false;
	static int s_iRetries = 0;
	static bool s_bUnresolved = false;

	const auto pSchema = CEconItemSchema::GetInstance();
	const int iCount = pSchema && IsReadableMemory(pSchema, 344) ? pSchema->GetItemDefCount() : 0;
	const bool bLocalizeReady = I::Localize != nullptr;
	if (iCount <= 0)
		return s_vItems;

	const bool bRetry = s_bUnresolved && ++s_iRetries <= 8;
	if (!bRetry && iCount == s_iSchemaCount && pSchema == s_pSchema && bLocalizeReady == s_bLocalizeReady)
		return s_vItems;

	s_pSchema = pSchema;
	s_iSchemaCount = iCount;
	s_bLocalizeReady = bLocalizeReady;
	if (s_iRetries > 8)
		s_iRetries = 0;
	s_bUnresolved = false;
	s_vItems.clear();
	s_vItems.reserve(iCount);

	std::unordered_map<uint16_t, bool> mSeen = {};
	for (int i = 0; i < iCount; i++)
	{
		const auto pEntry = pSchema->GetItemDefEntry(i);
		if (!IsReadableMemory(pEntry, 24))
			continue;
		if (*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEntry) + 16) < -1)
			continue;

		const auto pDef = *reinterpret_cast<CTFItemDefinition**>(reinterpret_cast<uintptr_t>(pEntry) + 8);
		if (!IsReadableMemory(pDef, 0x188))
			continue;

		const uint16_t iDef = pDef->GetDefinitionIndex();
		if (!iDef || mSeen.contains(iDef))
			continue;

		const std::string sItemName = ReadString(pDef->m_pszItemName());
		const std::string sName = sItemName.empty() ? ReadString(pDef->m_pszName()) : sItemName;

		ItemInfo_t tItem = {};
		tItem.iDef = iDef;
		tItem.sName = sItemName.empty() ? PrettifyItemName(sName) : LocalizeItemName(sName);
		tItem.sClass = ReadString(pDef->m_pszItemClass());
		tItem.sImage = ReadString(pDef->m_pszImageInventory());
		if (tItem.sName.empty())
			continue;
		if (IsUnresolvedItemName(tItem.sName))
			s_bUnresolved = true;

		mSeen[iDef] = true;
		s_vItems.push_back(std::move(tItem));
	}

	std::sort(s_vItems.begin(), s_vItems.end(), [](const ItemInfo_t& a, const ItemInfo_t& b)
	{
		return a.sName < b.sName;
	});
	return s_vItems;
}

bool CSkinChanger::InBackpack(uint16_t iDef)
{
	std::lock_guard tLock(m_tMutex);
	return std::any_of(m_vBackpack.begin(), m_vBackpack.end(), [iDef](const BackpackItem_t& tItem) { return tItem.iDef == iDef; });
}

uint32_t CSkinChanger::NextSerial()
{
	for (uint32_t i = 0; i < kClientSerialMask; i++)
	{
		const uint32_t uSerial = m_uNextSerial;
		m_uNextSerial = m_uNextSerial >= kClientSerialMask ? 1 : m_uNextSerial + 1;
		if (uSerial && std::none_of(m_vBackpack.begin(), m_vBackpack.end(), [uSerial](const BackpackItem_t& tItem) { return tItem.uSerial == uSerial; }))
			return uSerial;
	}
	return 0;
}

void CSkinChanger::AddToBackpack(uint16_t iDef, int iUnusual)
{
	if (!iDef || iUnusual > 0xFFFF)
		return;

	std::lock_guard tLock(m_tMutex);
	if (const uint32_t uSerial = NextSerial())
		m_vBackpack.push_back({ uSerial, iDef, std::max(iUnusual, 0) });
}

void CSkinChanger::RemoveFromBackpack(uint16_t iDef)
{
	std::lock_guard tLock(m_tMutex);
	const auto it = std::find_if(m_vBackpack.rbegin(), m_vBackpack.rend(), [iDef](const BackpackItem_t& tItem) { return tItem.iDef == iDef; });
	if (it != m_vBackpack.rend())
		m_vBackpack.erase(std::next(it).base());
}

void CSkinChanger::ClearBackpack()
{
	std::lock_guard tLock(m_tMutex);
	m_vBackpack.clear();
}

std::vector<BackpackItem_t> CSkinChanger::GetBackpack()
{
	std::lock_guard tLock(m_tMutex);
	return m_vBackpack;
}

void CSkinChanger::SetBackpack(std::vector<BackpackItem_t> vBackpack)
{
	std::lock_guard tLock(m_tMutex);
	m_vBackpack = std::move(vBackpack);
	for (const auto& tItem : m_vBackpack)
		m_uNextSerial = std::max(m_uNextSerial, tItem.uSerial + 1);
	if (m_uNextSerial > kClientSerialMask)
		m_uNextSerial = 1;
}

int CSkinChanger::GetItemUnusual(uint64_t uItemID)
{
	std::lock_guard tLock(m_tMutex);
	const uint32_t uSerial = ClientItemSerial(uItemID);
	const auto it = std::find_if(m_vBackpack.begin(), m_vBackpack.end(), [uSerial](const BackpackItem_t& tItem) { return tItem.uSerial == uSerial; });
	return it != m_vBackpack.end() ? it->iUnusual : 0;
}

void CSkinChanger::OnLevelShutdown()
{
	ResetWearables(m_hWearableOwner.Get());
}

void CSkinChanger::Unload()
{
	ResetWearables(m_hWearableOwner.Get());
	ClearClientItems();
	m_vGraveyard.clear();
	for (int i = 0; i < 19; i++)
	{
		if (m_aWearableViewBuilt[i])
			DestroyItemView(m_aWearableViews[i].aData);
		m_aWearableViewBuilt[i] = false;
		m_aWearableViewKey[i] = 0;
	}
}

std::vector<const ItemInfo_t*> CSkinChanger::GetInventoryItems()
{
	std::vector<uint16_t> vDefs = {};
	{
		std::lock_guard tLock(m_tMutex);
		vDefs.reserve(m_mInventoryCounts.size());
		for (const auto& [iDef, iCount] : m_mInventoryCounts)
			vDefs.push_back(iDef);
	}

	std::vector<const ItemInfo_t*> vItems = {};
	vItems.reserve(vDefs.size());
	for (uint16_t iDef : vDefs)
	{
		if (const ItemInfo_t* pInfo = FindItemInfo(iDef))
			vItems.push_back(pInfo);
	}

	std::sort(vItems.begin(), vItems.end(), [](const ItemInfo_t* a, const ItemInfo_t* b)
	{
		return a->sName != b->sName ? a->sName < b->sName : a->iDef < b->iDef;
	});
	return vItems;
}

int CSkinChanger::GetInventoryCount(uint16_t iDef)
{
	std::lock_guard tLock(m_tMutex);
	const auto it = m_mInventoryCounts.find(iDef);
	return it != m_mInventoryCounts.end() ? it->second : 0;
}

#else

void CSkinChanger::ScheduleRefresh() {}
int CSkinChanger::OriginalDef(CBaseEntity*, int, int iCurrent) const { return iCurrent; }
void CSkinChanger::RunFrame() {}
void CSkinChanger::Apply() {}
void CSkinChanger::Service() {}
void CSkinChanger::CacheMenuInfo(CTFWeaponBase*) {}
void CSkinChanger::OnLevelShutdown() {}
void CSkinChanger::Unload() {}
const std::vector<ItemInfo_t>& CSkinChanger::GetItems() { static std::vector<ItemInfo_t> vItems; return vItems; }
void CSkinChanger::RequestFullUpdate() {}
void CSkinChanger::AddToBackpack(uint16_t, int) {}
void CSkinChanger::RemoveFromBackpack(uint16_t) {}
bool CSkinChanger::InBackpack(uint16_t) { return false; }
std::vector<BackpackItem_t> CSkinChanger::GetBackpack() { return {}; }
void CSkinChanger::SetBackpack(std::vector<BackpackItem_t>) {}
int CSkinChanger::GetInventoryCount(uint16_t) { return 0; }
int CSkinChanger::GetItemUnusual(uint64_t) { return 0; }
uint32_t CSkinChanger::NextSerial() { return 0; }
void CSkinChanger::ClearBackpack() {}
std::vector<const ItemInfo_t*> CSkinChanger::GetInventoryItems() { return {}; }
bool CSkinChanger::ClientInventoryActive() { return false; }
bool CSkinChanger::IsClientItem(uint64_t) { return false; }
void* CSkinChanger::GetClientSOCData(uint64_t) { return nullptr; }
void CSkinChanger::ApplyLocalLoadout(uint64_t, int, int) {}
void CSkinChanger::ApplyLocalPosition(uint64_t, uint32_t) {}
void CSkinChanger::ApplyLocalDelete(uint64_t) {}
void CSkinChanger::ClearClientItems() {}
bool CSkinChanger::OnSendMessage(void*) { return false; }
void CSkinChanger::ProcessCrates(CTFPlayerInventory*) {}
bool CSkinChanger::OpenCrate(CTFPlayerInventory*, const PendingCrate_t&) { return false; }
void CSkinChanger::SyncInventory() {}
void CSkinChanger::RemoveClientItem(CTFPlayerInventory*, uint64_t) {}
void CSkinChanger::ReapplyLoadout(CTFPlayerInventory*) {}
std::map<int, uint64_t> CSkinChanger::GetLoadout() { return {}; }
void CSkinChanger::SetLoadout(std::map<int, uint64_t>) {}
CTFPlayerInventory* CSkinChanger::GetLocalInventory() const { return nullptr; }
void CSkinChanger::UpdateWearables(CTFPlayer*) {}
void CSkinChanger::ResetWearables(CTFPlayer*) {}
void CSkinChanger::RemoveWearable(CTFPlayer*, Wearable_t&) {}
void CSkinChanger::ClearWearables() {}
CEconItemView* CSkinChanger::GetWearableView(int, int, int, int, int) { return nullptr; }
CEconWearable* CSkinChanger::CreateWearable(CTFPlayer*, CEconItemView*, int, int) { return nullptr; }
bool CSkinChanger::AppendWearable(CTFPlayer*, CEconWearable*) { return false; }

#endif
