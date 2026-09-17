#include "SkinChanger.h"

#ifndef TEXTMODE

MAKE_SIGNATURE(CEconItemSchema_GetAttributeDefinition, "client.dll", "89 54 24 ? 53 48 83 EC ? 48 8B D9 48 8D 54 24 ? 48 81 C1 ? ? ? ? E8 ? ? ? ? 8B D0 3B 83 ? ? ? ? 73 ? 8B 83 ? ? ? ? 83 F8 ? 74 ? 3B D0 7F ? 48 81 C3 ? ? ? ? 44 8B C2 83 FA ? 74 ? 48 8B 03 8B CA", 0x0);
MAKE_SIGNATURE(CAttributeList_SetRuntimeAttributeValue, "client.dll", "48 89 5C 24 10 55 56 57 48 8B EC 48 83 EC 50 44", 0x0);

namespace
{
	constexpr uint16_t kPaintkit = 834;
	constexpr uint16_t kWear = 725;
	constexpr uint16_t kInspect = 731;
	constexpr uint16_t kUnusualWeapon = 370;
	constexpr uint16_t kFestive = 2053;
	constexpr uint16_t kAustralium = 2027;
	constexpr uint16_t kLootRarity = 2022;
	constexpr uint16_t kStyleOverride = 542;
	constexpr uint16_t kKillstreakTier = 2025;
	constexpr uint16_t kKillstreakSheen = 2013;

	constexpr int kUnusualHot = 701;
	constexpr int kUnusualIsotope = 702;
	constexpr int kUnusualCool = 703;
	constexpr int kUnusualEnergyOrb = 704;

	float IntToStupidFloat(int v)
	{
		return *reinterpret_cast<float*>(&v);
	}

	class CAttributeList
	{
	public:
		void SetAttribute(int iIndex, float flValue)
		{
			auto pSchema = CEconItemSchema::GetInstance();
			if (!pSchema)
				return;

			auto pDef = S::CEconItemSchema_GetAttributeDefinition.Call<void*>(pSchema, iIndex);
			if (!pDef)
				return;

			S::CAttributeList_SetRuntimeAttributeValue.Call<void>(this, pDef, flValue);
		}
	};

	void RedirectIndex(int& nWeaponIndex)
	{
		switch (nWeaponIndex)
		{
		case Soldier_m_RocketLauncher: nWeaponIndex = Soldier_m_RocketLauncherR; break;
		case Scout_m_Scattergun: nWeaponIndex = Scout_m_ScattergunR; break;
		case Pyro_m_FlameThrower: nWeaponIndex = Pyro_m_FlameThrowerR; break;
		case Demoman_m_GrenadeLauncher: nWeaponIndex = Demoman_m_GrenadeLauncherR; break;
		case Demoman_s_StickybombLauncher: nWeaponIndex = Demoman_s_StickybombLauncherR; break;
		case Heavy_m_Minigun: nWeaponIndex = Heavy_m_MinigunR; break;
		case Engi_t_Wrench: nWeaponIndex = Engi_t_WrenchR; break;
		case Medic_s_MediGun: nWeaponIndex = Medic_s_MediGunR; break;
		case Sniper_m_SniperRifle: nWeaponIndex = Sniper_m_SniperRifleR; break;
		case Sniper_s_SMG: nWeaponIndex = Sniper_s_SMGR; break;
		case Spy_t_Knife: nWeaponIndex = Spy_t_KnifeR; break;
		case Spy_m_Revolver: nWeaponIndex = Spy_m_RevolverR; break;
		case Engi_s_EngineersPistol: nWeaponIndex = Engi_s_PistolR; break;
		case Soldier_s_SoldiersShotgun:
		case Pyro_s_PyrosShotgun:
		case Heavy_s_HeavysShotgun:
		case Engi_m_EngineersShotgun: nWeaponIndex = Soldier_s_ShotgunR; break;
		case Scout_t_Bat: nWeaponIndex = Scout_t_BatR; break;
		case Soldier_t_Shovel: nWeaponIndex = Soldier_t_ShovelR; break;
		case Pyro_t_FireAxe: nWeaponIndex = Pyro_t_FireAxeR; break;
		case Demoman_t_Bottle: nWeaponIndex = Demoman_t_BottleR; break;
		case Medic_t_Bonesaw: nWeaponIndex = Medic_t_BonesawR; break;
		case Sniper_t_Kukri: nWeaponIndex = Sniper_t_KukriR; break;
		default: break;
		}
	}

	int UnusualParticle()
	{
		switch (Vars::Visuals::SkinChanger::Unusual.Value)
		{
		case Vars::Visuals::SkinChanger::UnusualEnum::Hot: return kUnusualHot;
		case Vars::Visuals::SkinChanger::UnusualEnum::Isotope: return kUnusualIsotope;
		case Vars::Visuals::SkinChanger::UnusualEnum::Cool: return kUnusualCool;
		case Vars::Visuals::SkinChanger::UnusualEnum::EnergyOrb: return kUnusualEnergyOrb;
		default: return 0;
		}
	}

	int ConfigHash()
	{
		return (Vars::Visuals::SkinChanger::Enabled.Value ? 1 : 0)
			| (Vars::Visuals::SkinChanger::PaintKit.Value << 1)
			| (Vars::Visuals::SkinChanger::Australium.Value ? 1 << 16 : 0)
			| (Vars::Visuals::SkinChanger::Festive.Value ? 1 << 17 : 0)
			| (Vars::Visuals::SkinChanger::Killstreak.Value << 18)
			| (Vars::Visuals::SkinChanger::Sheen.Value << 20)
			| (Vars::Visuals::SkinChanger::Unusual.Value << 24);
	}

	void ApplySkin(CTFWeaponBase* pWeapon)
	{
		if (!pWeapon)
			return;

		int& nWeaponIndex = *reinterpret_cast<int*>(uintptr_t(pWeapon) + 3344);
		RedirectIndex(nWeaponIndex);

		auto pList = reinterpret_cast<CAttributeList*>(uintptr_t(pWeapon) + 3512);
		if (!pList)
			return;

		if (const int iKit = Vars::Visuals::SkinChanger::PaintKit.Value)
		{
			pList->SetAttribute(kPaintkit, IntToStupidFloat(iKit));
			pList->SetAttribute(kWear, 0.f);
			pList->SetAttribute(kInspect, 1.f);
		}

		if (Vars::Visuals::SkinChanger::Australium.Value)
		{
			pList->SetAttribute(kAustralium, 1.f);
			pList->SetAttribute(kLootRarity, 1.f);
			pList->SetAttribute(kStyleOverride, 1.f);
		}

		if (Vars::Visuals::SkinChanger::Festive.Value)
			pList->SetAttribute(kFestive, 1.f);

		if (const int iTier = Vars::Visuals::SkinChanger::Killstreak.Value)
			pList->SetAttribute(kKillstreakTier, float(iTier));

		int iSheen = Vars::Visuals::SkinChanger::Sheen.Value;
		if (!iSheen && Vars::Visuals::SkinChanger::Killstreak.Value)
			iSheen = 1;
		if (iSheen)
			pList->SetAttribute(kKillstreakSheen, float(iSheen));

		if (const int iUnusual = UnusualParticle())
			pList->SetAttribute(kUnusualWeapon, float(iUnusual));
	}
}

void CSkinChanger::Apply()
{
	if (!Vars::Visuals::SkinChanger::Enabled.Value)
	{
		if (m_bWasEnabled)
		{
			m_bWasEnabled = false;
			m_iLastHash = 0;
			if (I::ClientState)
				I::ClientState->ForceFullUpdate();
		}
		return;
	}

	m_bWasEnabled = true;

	const int iHash = ConfigHash();
	if (iHash != m_iLastHash)
	{
		m_iLastHash = iHash;
		if (I::ClientState)
			I::ClientState->ForceFullUpdate();
	}

	if (!I::EngineClient || !I::ClientEntityList)
		return;

	const int iLocal = I::EngineClient->GetLocalPlayer();
	if (iLocal <= 0)
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iLocal);
	if (!pEntity)
		return;

	auto pLocal = pEntity->As<CTFPlayer>();
	if (!pLocal || !pLocal->IsPlayer())
		return;

	auto& vWeapons = pLocal->m_hMyWeapons();
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		if (!vWeapons[i].IsValid())
			continue;
		ApplySkin(vWeapons[i].Get());
	}
}

#else

void CSkinChanger::Apply() {}

#endif
