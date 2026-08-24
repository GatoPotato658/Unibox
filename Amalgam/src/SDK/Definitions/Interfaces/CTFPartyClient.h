#pragma once
#include "Interface.h"

class CTFParty;

MAKE_SIGNATURE(CTFPartyClient_SendPartyChat, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 57 48 83 EC ? 48 C7 C3", 0x0);
MAKE_SIGNATURE(CTFPartyClient_LoadSavedCasualCriteria, "client.dll", "48 83 79 ? ? C6 81 ? ? ? ? ? 74 ? 80 79 ? ? 74 ? C6 81 ? ? ? ? ? 48 8B 15", 0x0);
MAKE_SIGNATURE(CTFPartyClient_BInQueueForMatchGroup, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B F9 8B DA 8B CA E8 ? ? ? ? 84 C0", 0x0);
MAKE_SIGNATURE(CTFPartyClient_RequestQueueForMatch, "client.dll", "40 55 56 48 81 EC ? ? ? ? 48 63 F2", 0x0);
MAKE_SIGNATURE(CTFPartyClient_CancelMatchQueueRequest, "client.dll", "40 55 56 48 83 EC ? 48 63 F2", 0x0);
MAKE_SIGNATURE(CTFPartyClient_PromoteToLeader, "client.dll", "48 89 54 24 10 53 48 83 EC 50 48 8B D9 48 8B 49 30 48 85 C9 0F 84 ?? ?? ?? ?? 80 7B 40 00 0F 84 ?? ?? ?? ?? 48 81 C1 B8 00 00 00 48 8D 54 24 68 48 8B 01 FF 50 20 83 F8 FF 0F 84 ?? ?? ?? ?? BA AF 19 00 00 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 8B 4C 24 40 48 8B 43 38", 0x0);
MAKE_SIGNATURE(CCasualCriteriaHelper_CCasualCriteriaHelper, "client.dll", "48 89 5C 24 ? 56 57 41 57 48 83 EC ? 0F 57 C0", 0x0);
MAKE_SIGNATURE(CTFGroupMatchCriteriaProto_DefaultInstance, "client.dll", "48 8B 05 ? ? ? ? 48 8B 50 ? 48 8D 4C 24 ? E8 ? ? ? ? 44 8B 44 24", 0x0);
MAKE_SIGNATURE(Protobuf_RepeatedStrings_Reserve, "client.dll", "89 54 24 10 53 48 83 EC 20 8B 41 10 48 8B D9 3B C2 7D ?? 03 C0", 0x0);
MAKE_SIGNATURE(Protobuf_RepeatedStrings_NewElement, "client.dll", "48 83 EC 28 B9 20 00 00 00 E8 ?? ?? ?? ?? 48 85 C0 74 ?? 48 C7 40 18 ?? ?? ?? ?? 48 C7 40 10 ?? ?? ?? ?? C6 00 00 48 83 C4 28 C3", 0x0);
MAKE_SIGNATURE(Protobuf_String_Assign, "client.dll", "48 89 5C 24 10 48 89 6C 24 18 56 57 41 57 48 83 EC 20 48 8B 69 18 49 8B F0", 0x0);

class CTFCasualMatchCriteria
{
public:

};

class CCasualCriteriaHelper
{
public:
	CCasualCriteriaHelper(const CTFCasualMatchCriteria* criteria)
	{
		S::CCasualCriteriaHelper_CCasualCriteriaHelper.Call<CCasualCriteriaHelper*>(this, criteria);
	}

	bool AnySelected() const { return !m_mapsBits.IsAllClear(); }

	CLargeVarBitVec m_mapsBits;
};

class CTFProtoRepeatedString
{
public:
	void** m_ppElements;
	int m_iCurrentSize;
	int m_iTotalSize;
	int m_iCapacity;

	std::string* GetElement(const int iIndex) const
	{
		if (!m_ppElements || iIndex < 0 || iIndex >= m_iCurrentSize)
			return nullptr;
		return reinterpret_cast<std::string*>(m_ppElements[iIndex]);
	}

	bool Matches(const std::vector<std::string>& vValues) const
	{
		if (m_iCurrentSize != static_cast<int>(vValues.size()))
			return false;

		for (int i = 0; i < m_iCurrentSize; i++)
		{
			const auto pString = GetElement(i);
			if (!pString || strcmp(pString->c_str(), vValues[i].c_str()))
				return false;
		}
		return true;
	}

	void Clear()
	{
		for (int i = 0; i < m_iCurrentSize; i++)
		{
			if (auto pString = GetElement(i))
				S::Protobuf_String_Assign.Call<std::string*, std::string*, const char*, size_t>(pString, "", 0);
		}
		m_iCurrentSize = 0;
	}

	bool Add(const char* sValue)
	{
		std::string* pString = nullptr;
		if (m_iCurrentSize >= m_iTotalSize)
		{
			if (m_iTotalSize == m_iCapacity)
				S::Protobuf_RepeatedStrings_Reserve.Call<void, void*, int>(this, m_iCapacity + 1);

			pString = S::Protobuf_RepeatedStrings_NewElement.Call<std::string*>();
			if (!pString || !m_ppElements)
				return false;

			m_ppElements[m_iCurrentSize] = pString;
			m_iTotalSize++;
			m_iCurrentSize++;
		}
		else
		{
			pString = GetElement(m_iCurrentSize);
			if (!pString)
				return false;
			m_iCurrentSize++;
		}

		S::Protobuf_String_Assign.Call<std::string*, std::string*, const char*, size_t>(pString, sValue ? sValue : "", sValue ? strlen(sValue) : 0);
		return true;
	}
};

class CTFGroupMatchCriteriaProto
{
public:
	byte pad0[16];
	uint32 has_bits[1];
	mutable int cached_size;
	bool late_join_ok;
	uint32 custom_ping_tolerance;
	std::string* mvm_mannup_tour;
	byte mvm_mannup_missions[24];
	byte mvm_bootcamp_missions[24];
	CTFCasualMatchCriteria* casual_criteria;

	static CTFGroupMatchCriteriaProto* GetDefaultInstance()
	{
		auto default_instance = *reinterpret_cast<CTFGroupMatchCriteriaProto**>(U::Memory.RelToAbs(S::CTFGroupMatchCriteriaProto_DefaultInstance()));
		return default_instance;
	}

	CTFCasualMatchCriteria* GetCasualCriteria()
	{
		return casual_criteria ? casual_criteria : GetDefaultInstance()->casual_criteria;
	}

	CTFProtoRepeatedString* GetMannUpMissions()
	{
		return reinterpret_cast<CTFProtoRepeatedString*>(mvm_mannup_missions);
	}

	CTFProtoRepeatedString* GetBootcampMissions()
	{
		return reinterpret_cast<CTFProtoRepeatedString*>(mvm_bootcamp_missions);
	}

	void SetMannUpTour(const char* sTour)
	{
		const bool bEmpty = !sTour || !*sTour;
		const auto pTour = mvm_mannup_tour;
		if (bEmpty ? (!pTour || pTour->empty()) : (pTour && !strcmp(pTour->c_str(), sTour)))
			return;

		if (!bEmpty && !pTour)
		{
			mvm_mannup_tour = new std::string(sTour);
			return;
		}
		S::Protobuf_String_Assign.Call<std::string*, std::string*, const char*, size_t>(pTour, bEmpty ? "" : sTour, bEmpty ? 0 : strlen(sTour));
	}
};

class CTFGroupMatchCriteria
{
public:
	VIRTUAL(Proto, CTFGroupMatchCriteriaProto*, 1, this);
};

class CTFPartyClient
{
public:
	SIGNATURE_ARGS(SendPartyChat, void, CTFPartyClient, (const char* sMessage), this, sMessage);
	SIGNATURE(LoadSavedCasualCriteria, void, CTFPartyClient, this);
	SIGNATURE_ARGS(BInQueueForMatchGroup, bool, CTFPartyClient, (int eMatchGroup), this, eMatchGroup);
	SIGNATURE_ARGS(RequestQueueForMatch, void, CTFPartyClient, (int eMatchGroup), this, eMatchGroup);
	SIGNATURE_ARGS(CancelMatchQueueRequest, void, CTFPartyClient, (int eMatchGroup), this, eMatchGroup);
	SIGNATURE_ARGS(PromoteToLeader, bool, CTFPartyClient, (uint64 uNewLeaderSteamID), this, uNewLeaderSteamID);

	OFFSET_EMBED(m_localGroupCriteria, CTFGroupMatchCriteria*, 432);

	CTFParty* GetParty()
	{
		return *reinterpret_cast<CTFParty**>(uintptr_t(this) + 48);
	}

	bool BIsLocalPlayerLeader()
	{
		return *reinterpret_cast<bool*>(uintptr_t(this) + 64);
	}

	bool AnySelected()
	{
		auto pCriteria = m_localGroupCriteria();
		auto pProto = pCriteria->Proto();
		auto pCasualCriteria = pProto->GetCasualCriteria();
		CCasualCriteriaHelper tHelper(pCasualCriteria);
		return tHelper.AnySelected();
	}
};

MAKE_INTERFACE_NULL(CTFPartyClient, TFPartyClient);