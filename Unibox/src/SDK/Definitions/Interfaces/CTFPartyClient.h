#pragma once
#include "Interface.h"

class CTFParty;


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

	CTFGroupMatchCriteria* m_localGroupCriteria()
	{
		static const int iOffset = S::CTFPartyClient_LoadSavedCasualCriteria.ReadAt<int>(37);
		return reinterpret_cast<CTFGroupMatchCriteria*>(uintptr_t(this) + iOffset);
	}

	CTFParty* GetParty()
	{
		static const int iOffset = S::CTFPartyClient_LoadSavedCasualCriteria.ReadAt<uint8_t>(3);
		return *reinterpret_cast<CTFParty**>(uintptr_t(this) + iOffset);
	}

	bool BIsLocalPlayerLeader()
	{
		static const int iOffset = S::CTFPartyClient_LoadSavedCasualCriteria.ReadAt<uint8_t>(16);
		return *reinterpret_cast<bool*>(uintptr_t(this) + iOffset);
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
