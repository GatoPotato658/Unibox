#include "PlayerUtils.h"
#include "SteamProfileCache.h"
#include "../Output/Output.h"

#include <boost/property_tree/json_parser.hpp>

static bool IsPlaceholderName(const std::string& sName)
{
	if (sName.empty())
		return true;

	std::string sLower;
	sLower.reserve(sName.size());
	for (char ch : sName)
		sLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

	return sLower == "unknown" || sLower == "[unknown]" || sLower == "<unknown>" || sLower == "unknown player";
}

uint32_t CPlayerlistUtils::GetAccountID(int iIndex)
{
	auto pResource = H::Entities.GetResource();
	if (pResource && pResource->m_bValid(iIndex) && !pResource->IsFakePlayer(iIndex))
		return pResource->m_iAccountID(iIndex);
	return 0;
}

int CPlayerlistUtils::GetIndex(uint32_t uAccountID)
{
	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return 0;
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		if (pResource->m_bValid(n) && !pResource->IsFakePlayer(n) && pResource->m_iAccountID(n) == uAccountID)
			return n;
	}
	return 0;
}

PriorityLabel_t* CPlayerlistUtils::GetTag(int iID)
{
	if (iID > -1 && iID < m_vTags.size())
		return &m_vTags[iID];
	return nullptr;
}

int CPlayerlistUtils::GetTag(const std::string& sTag)
{
	if (sTag.empty())
		return -1;

	auto Normalize = [](const std::string& sInput) -> std::string
	{
		std::string sNormalized;
		sNormalized.reserve(sInput.size());
		for (unsigned char ch : sInput)
		{
			const unsigned char uch = ch;
			if (std::isspace(uch) || uch == '_' || uch == '-')
				continue;
			sNormalized.push_back(static_cast<char>(std::tolower(uch)));
		}
		return sNormalized;
	};

	const std::string sLookup = Normalize(sTag);
	if (sLookup.empty())
		return -1;

	for (size_t i = 0; i < m_vTags.size(); i++)
	{
		if (Normalize(m_vTags[i].m_sName) == sLookup)
			return static_cast<int>(i);
	}

	return -1;
}



void CPlayerlistUtils::AddTag(uint32_t uAccountID, int iID, bool bSave, const char* sName, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags, const char* sReason, int iDetections, bool bAuto)
{
	if (!uAccountID)
		return;

	const bool bHadTag = HasTag(uAccountID, iID);
	if (!bHadTag)
	{
		mPlayerTags[uAccountID].push_back(iID);
		m_bSave = bSave;
		if (auto pTag = GetTag(iID); pTag && sName)
			F::Output.TagsChanged(sName, "Added", pTag->m_tColor.ToHexA().c_str(), pTag->m_sName.c_str());
	}

	if (IndexToTag(iID) == CHEATER_TAG)
		UpdateCheaterRecord(uAccountID, sName, sReason, iDetections, bAuto);
}
void CPlayerlistUtils::AddTag(int iIndex, int iID, bool bSave, const char* sName, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags, const char* sReason, int iDetections, bool bAuto)
{
	AddTag(GetAccountID(iIndex), iID, bSave, sName, mPlayerTags, sReason, iDetections, bAuto);
}
void CPlayerlistUtils::AddTag(uint32_t uAccountID, int iID, bool bSave, const char* sName, const char* sReason, int iDetections, bool bAuto)
{
	AddTag(uAccountID, iID, bSave, sName, m_mPlayerTags, sReason, iDetections, bAuto);
}
void CPlayerlistUtils::AddTag(int iIndex, int iID, bool bSave, const char* sName, const char* sReason, int iDetections, bool bAuto)
{
	AddTag(iIndex, iID, bSave, sName, m_mPlayerTags, sReason, iDetections, bAuto);
}

void CPlayerlistUtils::RemoveTag(uint32_t uAccountID, int iID, bool bSave, const char* sName, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags)
{
	if (!uAccountID)
		return;

	auto& vTags = mPlayerTags[uAccountID];
	bool bRemoved = false;
	for (auto it = vTags.begin(); it != vTags.end(); it++)
	{
		if (iID == *it)
		{
			vTags.erase(it);
			m_bSave = bSave;
			if (auto pTag = GetTag(iID); pTag && sName)
				F::Output.TagsChanged(sName, "Removed", pTag->m_tColor.ToHexA().c_str(), pTag->m_sName.c_str());
			bRemoved = true;
			break;
		}
	}
	if (vTags.empty())
		mPlayerTags.erase(uAccountID);

	if (bRemoved && IndexToTag(iID) == CHEATER_TAG)
		RemoveCheaterRecord(uAccountID, bSave);
}
void CPlayerlistUtils::RemoveTag(int iIndex, int iID, bool bSave, const char* sName, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags)
{
	RemoveTag(GetAccountID(iIndex), iID, bSave, sName, mPlayerTags);
}
void CPlayerlistUtils::RemoveTag(uint32_t uAccountID, int iID, bool bSave, const char* sName)
{
	RemoveTag(uAccountID, iID, bSave, sName, m_mPlayerTags);
}
void CPlayerlistUtils::RemoveTag(int iIndex, int iID, bool bSave, const char* sName)
{
	RemoveTag(iIndex, iID, bSave, sName, m_mPlayerTags);
}

bool CPlayerlistUtils::HasTags(uint32_t uAccountID, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags)
{
	if (!uAccountID)
		return false;

	auto it = mPlayerTags.find(uAccountID);
	return it != mPlayerTags.end() && !it->second.empty();
}
bool CPlayerlistUtils::HasTags(int iIndex, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags)
{
	return HasTags(GetAccountID(iIndex), mPlayerTags);
}
bool CPlayerlistUtils::HasTags(uint32_t uAccountID)
{
	return HasTags(uAccountID, m_mPlayerTags);
}

bool CPlayerlistUtils::HasTags(int iIndex)
{
	if (const uint32_t uAccountID = GetAccountID(iIndex))
		return HasTags(uAccountID);
	return false;
}

bool CPlayerlistUtils::HasTag(uint32_t uAccountID, int iID, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags)
{
	if (!uAccountID)
		return false;

	auto it = mPlayerTags.find(uAccountID);
	if (it == mPlayerTags.end())
		return false;

	auto tag_it = std::ranges::find_if(it->second, [iID](const auto& _iID) { return iID == _iID; });
	return tag_it != it->second.end();
}
bool CPlayerlistUtils::HasTag(int iIndex, int iID, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags)
{
	return HasTag(GetAccountID(iIndex), iID, mPlayerTags);
}
bool CPlayerlistUtils::HasTag(uint32_t uAccountID, int iID)
{
	return HasTag(uAccountID, iID, m_mPlayerTags);
}
bool CPlayerlistUtils::HasTag(int iIndex, int iID)
{
	return HasTag(iIndex, iID, m_mPlayerTags);
}



int CPlayerlistUtils::GetPriority(uint32_t uAccountID, bool bCache)
{
	if (bCache)
		return H::Entities.GetPriority(uAccountID);

	const int iDefault = m_vTags[TagToIndex(DEFAULT_TAG)].m_iPriority;
	if (!uAccountID)
		return iDefault;

	if (HasTag(uAccountID, TagToIndex(IGNORED_TAG)))
		return m_vTags[TagToIndex(IGNORED_TAG)].m_iPriority;

	std::vector<int> vPriorities;
	if (auto it = m_mPlayerTags.find(uAccountID); it != m_mPlayerTags.end())
	{
		for (auto& iID : it->second)
		{
			auto pTag = GetTag(iID);
			if (pTag && !pTag->m_bLabel)
				vPriorities.push_back(pTag->m_iPriority);
		}
	}
	if (H::Entities.IsFriend(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(FRIEND_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iPriority);
	}
	if (H::Entities.InParty(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(PARTY_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iPriority);
	}
	if (H::Entities.IsF2P(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(F2P_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iPriority);
	}
	if (vPriorities.empty())
		return iDefault;

	std::sort(vPriorities.begin(), vPriorities.end(), std::greater<int>());
	return vPriorities.front();
}
int CPlayerlistUtils::GetPriority(int iIndex, bool bCache)
{
	if (bCache)
		return H::Entities.GetPriority(iIndex);

	return GetPriority(GetAccountID(iIndex), false);
}

int CPlayerlistUtils::GetFollowPriority(uint32_t uAccountID, bool bCache)
{
	if (bCache)
		return H::Entities.GetPriority(uAccountID, PriorityTypeEnum::Follow);

	const int iDefault = m_vTags[TagToIndex(DEFAULT_TAG)].m_iFollowPriority;
	if (!uAccountID)
		return iDefault;

	std::vector<int> vPriorities;
	if (auto it = m_mPlayerTags.find(uAccountID); it != m_mPlayerTags.end())
	{
		for (auto& iID : it->second)
		{
			auto pTag = GetTag(iID);
			if (pTag && !pTag->m_bLabel)
			{
				if (pTag->m_iFollowPriority < 0)
					return -1;

				vPriorities.push_back(pTag->m_iFollowPriority);
			}
		}
	}
	if (H::Entities.IsFriend(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(FRIEND_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iFollowPriority);
	}
	if (H::Entities.InParty(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(PARTY_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iFollowPriority);
	}
	if (H::Entities.IsF2P(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(F2P_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iFollowPriority);
	}
	if (vPriorities.empty())
		return iDefault;

	std::sort(vPriorities.begin(), vPriorities.end(), std::greater<int>());
	return vPriorities.front();
}

int CPlayerlistUtils::GetFollowPriority(int iIndex, bool bCache)
{
	if (bCache)
		return H::Entities.GetPriority(iIndex, PriorityTypeEnum::Follow);

	return GetFollowPriority(GetAccountID(iIndex), false);
}

int CPlayerlistUtils::GetVotePriority(uint32_t uAccountID, bool bCache)
{
	if (bCache)
		return H::Entities.GetPriority(uAccountID, PriorityTypeEnum::Vote);

	const int iDefault = m_vTags[TagToIndex(DEFAULT_TAG)].m_iVotePriority;
	if (!uAccountID)
		return iDefault;

	std::vector<int> vPriorities;
	if (auto it = m_mPlayerTags.find(uAccountID); it != m_mPlayerTags.end())
	{
		for (auto& iID : it->second)
		{
			auto pTag = GetTag(iID);
			if (pTag && !pTag->m_bLabel)
			{
				if (pTag->m_iVotePriority < 0)
					return -1;

				vPriorities.push_back(pTag->m_iVotePriority);
			}
		}
	}
	if (H::Entities.IsFriend(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(FRIEND_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iVotePriority);
	}
	if (H::Entities.InParty(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(PARTY_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iVotePriority);
	}
	if (H::Entities.IsF2P(uAccountID))
	{
		auto& tTag = m_vTags[TagToIndex(F2P_TAG)];
		if (!tTag.m_bLabel)
			vPriorities.push_back(tTag.m_iVotePriority);
	}
	if (vPriorities.empty())
		return iDefault;

	std::sort(vPriorities.begin(), vPriorities.end(), std::greater<int>());
	return vPriorities.front();
}

int CPlayerlistUtils::GetVotePriority(int iIndex, bool bCache)
{
	if (bCache)
		return H::Entities.GetPriority(iIndex, PriorityTypeEnum::Vote);

	return GetVotePriority(GetAccountID(iIndex), false);
}

PriorityLabel_t* CPlayerlistUtils::GetSignificantTag(uint32_t uAccountID, int iMode)
{
	if (!uAccountID)
		return nullptr;

	std::vector<PriorityLabel_t*> vTags;
	if (!iMode || iMode == 1)
	{
		if (HasTag(uAccountID, TagToIndex(IGNORED_TAG)))
			return &m_vTags[TagToIndex(IGNORED_TAG)];

		if (auto it = m_mPlayerTags.find(uAccountID); it != m_mPlayerTags.end())
		{
			for (auto& iID : it->second)
			{
				PriorityLabel_t* _pTag = GetTag(iID);
				if (_pTag && !_pTag->m_bLabel)
					vTags.push_back(_pTag);
			}
		}
		if (H::Entities.IsFriend(uAccountID))
		{
			auto _pTag = &m_vTags[TagToIndex(FRIEND_TAG)];
			if (!_pTag->m_bLabel)
				vTags.push_back(_pTag);
		}
		if (H::Entities.InParty(uAccountID))
		{
			auto _pTag = &m_vTags[TagToIndex(PARTY_TAG)];
			if (!_pTag->m_bLabel)
				vTags.push_back(_pTag);
		}
		if (H::Entities.IsF2P(uAccountID))
		{
			auto _pTag = &m_vTags[TagToIndex(F2P_TAG)];
			if (!_pTag->m_bLabel)
				vTags.push_back(_pTag);
		}
	}
	if ((!iMode || iMode == 2) && !vTags.size())
	{
		if (auto it = m_mPlayerTags.find(uAccountID); it != m_mPlayerTags.end())
		{
			for (auto& iID : it->second)
			{
				PriorityLabel_t* _pTag = GetTag(iID);
				if (_pTag && _pTag->m_bLabel)
					vTags.push_back(_pTag);
			}
		}
		if (H::Entities.IsFriend(uAccountID))
		{
			auto _pTag = &m_vTags[TagToIndex(FRIEND_TAG)];
			if (_pTag->m_bLabel)
				vTags.push_back(_pTag);
		}
		if (H::Entities.InParty(uAccountID))
		{
			auto _pTag = &m_vTags[TagToIndex(PARTY_TAG)];
			if (_pTag->m_bLabel)
				vTags.push_back(_pTag);
		}
		if (H::Entities.IsF2P(uAccountID))
		{
			auto _pTag = &m_vTags[TagToIndex(F2P_TAG)];
			if (_pTag->m_bLabel)
				vTags.push_back(_pTag);
		}
	}
	if (vTags.empty())
		return nullptr;

	std::sort(vTags.begin(), vTags.end(), [&](const PriorityLabel_t* a, const PriorityLabel_t* b) -> bool
	{
		// sort by priority if unequal
		if (a->m_iPriority != b->m_iPriority)
			return a->m_iPriority > b->m_iPriority;

		return a->m_sName < b->m_sName;
	});
	return vTags.front();
}
PriorityLabel_t* CPlayerlistUtils::GetSignificantTag(int iIndex, int iMode)
{
	return GetSignificantTag(GetAccountID(iIndex), iMode);
}

bool CPlayerlistUtils::IsIgnored(uint32_t uAccountID)
{
	if (!uAccountID)
		return false;

	const int iIgnoredTag = TagToIndex(IGNORED_TAG);
	if (HasTag(uAccountID, iIgnoredTag))
		return true;

	const int iPriority = GetPriority(uAccountID, false);
	const int iIgnored = m_vTags[iIgnoredTag].m_iPriority;
	return iPriority <= iIgnored;
}
bool CPlayerlistUtils::IsIgnored(int iIndex)
{
	return IsIgnored(GetAccountID(iIndex));
}

bool CPlayerlistUtils::IsPrioritized(uint32_t uAccountID)
{
	if (!uAccountID)
		return false;

	const int iPriority = GetPriority(uAccountID);
	const int iDefault = m_vTags[TagToIndex(DEFAULT_TAG)].m_iPriority;
	return iPriority > iDefault;
}
bool CPlayerlistUtils::IsPrioritized(int iIndex)
{
	return IsPrioritized(GetAccountID(iIndex));
}



int CPlayerlistUtils::GetNameType(int iIndex)
{
	if (Vars::Visuals::UI::StreamerMode.Value)
	{
		if (iIndex == I::EngineClient->GetLocalPlayer())
		{
			if (Vars::Visuals::UI::StreamerMode.Value >= Vars::Visuals::UI::StreamerModeEnum::Local)
				return NameTypeEnum::Local;
		}
		else if (H::Entities.IsFriend(iIndex))
		{
			if (Vars::Visuals::UI::StreamerMode.Value >= Vars::Visuals::UI::StreamerModeEnum::Friends)
				return NameTypeEnum::Friend;
		}
		else if (H::Entities.InParty(iIndex))
		{
			if (Vars::Visuals::UI::StreamerMode.Value >= Vars::Visuals::UI::StreamerModeEnum::Party)
				return NameTypeEnum::Party;
		}
		else if (Vars::Visuals::UI::StreamerMode.Value >= Vars::Visuals::UI::StreamerModeEnum::All)
			return NameTypeEnum::Player;
	}
	if (const uint32_t uAccountID = GetAccountID(iIndex); uAccountID && GetPlayerAlias(uAccountID))
		return NameTypeEnum::Custom;
	return NameTypeEnum::None;
}

int CPlayerlistUtils::GetNameType(uint32_t uAccountID)
{
	if (Vars::Visuals::UI::StreamerMode.Value)
	{
		if (uAccountID == I::SteamUser->GetSteamID().GetAccountID())
		{
			if (Vars::Visuals::UI::StreamerMode.Value >= Vars::Visuals::UI::StreamerModeEnum::Local)
				return NameTypeEnum::Local;
		}
		else if (H::Entities.IsFriend(uAccountID))
		{
			if (Vars::Visuals::UI::StreamerMode.Value >= Vars::Visuals::UI::StreamerModeEnum::Friends)
				return NameTypeEnum::Friend;
		}
		else if (H::Entities.InParty(uAccountID))
		{
			if (Vars::Visuals::UI::StreamerMode.Value >= Vars::Visuals::UI::StreamerModeEnum::Party)
				return NameTypeEnum::Party;
		}
		else if (Vars::Visuals::UI::StreamerMode.Value >= Vars::Visuals::UI::StreamerModeEnum::All)
			return NameTypeEnum::Player;
	}
	if (GetPlayerAlias(uAccountID))
		return NameTypeEnum::Custom;
	return NameTypeEnum::None;
}

const char* CPlayerlistUtils::GetPlayerName(int iIndex, const char* sDefault, int* pType)
{
	int iType = GetNameType(iIndex);
	if (pType) *pType = iType;

	switch (iType)
	{
	case NameTypeEnum::Local:
		return LOCAL;
	case NameTypeEnum::Friend:
		return FRIEND;
	case NameTypeEnum::Party:
		return PARTY;
	case NameTypeEnum::Player:
		if (auto pTag = GetSignificantTag(iIndex, 0))
			return pTag->m_sName.c_str();
		else if (auto pResource = H::Entities.GetResource(); pResource && pResource->m_bValid(iIndex))
			return pResource->m_iTeam(I::EngineClient->GetLocalPlayer()) != pResource->m_iTeam(iIndex) ? ENEMY : TEAMMATE;
		return PLAYER;
	case NameTypeEnum::Custom:
		if (auto sAlias = GetPlayerAlias(GetAccountID(iIndex)))
			return sAlias->c_str();
	}
	return sDefault;
}

const char* CPlayerlistUtils::GetPlayerName(uint32_t uAccountID, const char* sDefault, int* pType)
{
	int iType = GetNameType(uAccountID);
	if (pType) *pType = iType;

	switch (iType)
	{
	case NameTypeEnum::Local:
		return LOCAL;
	case NameTypeEnum::Friend:
		return FRIEND;
	case NameTypeEnum::Party:
		return PARTY;
	case NameTypeEnum::Player:
		if (auto pTag = GetSignificantTag(uAccountID, 0))
			return pTag->m_sName.c_str();
		else if (auto pResource = H::Entities.GetResource(); (iType = GetIndex(uAccountID)) && pResource && pResource->m_bValid(iType))
			return pResource->m_iTeam(I::EngineClient->GetLocalPlayer()) != pResource->m_iTeam(iType) ? ENEMY : TEAMMATE;
		return PLAYER;
	case NameTypeEnum::Custom:
		if (auto sAlias = GetPlayerAlias(uAccountID))
			return sAlias->c_str();
	}
	return sDefault;
}

const char* CPlayerlistUtils::GetPlayerName(int iIndex)
{
	auto pResource = H::Entities.GetResource();
	return pResource && pResource->IsValid(iIndex) ? pResource->GetName(iIndex) : PLAYER_ERROR_NAME;
}

const char* CPlayerlistUtils::GetPlayerName(uint32_t uAccountID)
{
	auto pResource = H::Entities.GetResource();
	int iIndex = GetIndex(uAccountID);
	return pResource && pResource->IsValid(iIndex) ? pResource->GetName(iIndex) : PLAYER_ERROR_NAME;
}



void CPlayerlistUtils::Store()
{
	static Timer tTimer = {};
	if (!tTimer.Run(1.f))
		return;

	std::lock_guard tLock(m_tMutex);
	m_vPlayerCache.clear();

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		if (!pResource->m_bValid(n) || !pResource->m_bConnected(n))
			continue;

		uint32_t uAccountID = pResource->m_iAccountID(n);
		const char* sName = pResource->GetName(n);
		
		// (dont) Process special characters in player names
		//if (sName && uAccountID)
		//	ProcessSpecialCharsInName(uAccountID, sName);

		m_vPlayerCache.emplace_back(
			sName,
			uAccountID,
			pResource->m_iUserID(n),
			pResource->m_iTeam(n),
			pResource->m_bAlive(n),
			n == I::EngineClient->GetLocalPlayer(),
			pResource->IsFakePlayer(n),
			H::Entities.IsFriend(uAccountID),
			H::Entities.InParty(uAccountID),
			H::Entities.IsF2P(uAccountID),
			H::Entities.GetLevel(uAccountID),
			H::Entities.GetParty(uAccountID)
		);

		if (uAccountID)
			F::SteamProfileCache.Touch(uAccountID);
	}
}

std::string CPlayerlistUtils::ResolveAccountName(uint32_t uAccountID, const std::string& sAlias) const
{
	if (!uAccountID)
		return "";

	if (!sAlias.empty())
		return sAlias;

	if (auto pResource = H::Entities.GetResource())
	{
		for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
		{
			if (pResource->m_bValid(n) && !pResource->IsFakePlayer(n) && pResource->m_iAccountID(n) == uAccountID)
			{
				if (const char* sName = pResource->GetName(n); sName && *sName)
					return sName;
			}
		}
	}

	if (I::SteamFriends)
	{
		const CSteamID steamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual);
		if (const char* persona = I::SteamFriends->GetFriendPersonaName(steamID); persona && *persona && std::strcmp(persona, "Unknown") != 0)
			return persona;
	}

	if (auto sRemote = F::SteamProfileCache.GetPersonaName(uAccountID); !sRemote.empty())
		return sRemote;

	F::SteamProfileCache.Touch(uAccountID);

	return std::format("{}", CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64());
}

std::string CPlayerlistUtils::ResolveAccountName(uint32_t uAccountID) const
{
	std::string sAlias;
	{
		std::shared_lock tLock(m_tMutex);
		if (auto it = m_mPlayerAliases.find(uAccountID); it != m_mPlayerAliases.end())
			sAlias = it->second;
	}
	return ResolveAccountName(uAccountID, sAlias);
}

std::vector<MarkedPlayer_t> CPlayerlistUtils::GetMarkedPlayers()
{
	std::vector<std::pair<uint32_t, std::vector<int>>> vTaggedAccounts = {};
	std::unordered_map<uint32_t, std::string> mAliases = {};
	{
		std::shared_lock tLock(m_tMutex);
		vTaggedAccounts.reserve(m_mPlayerTags.size());
		for (const auto& [uAccountID, vTags] : m_mPlayerTags)
		{
			if (!vTags.empty())
				vTaggedAccounts.emplace_back(uAccountID, vTags);
		}
		mAliases = m_mPlayerAliases;
	}

	std::vector<MarkedPlayer_t> vMarkedPlayers = {};
	vMarkedPlayers.reserve(vTaggedAccounts.size());

	auto getRoleTag = [&](const std::vector<int>& vTags, uint32_t uAccountID) -> PriorityLabel_t
	{
		std::vector<PriorityLabel_t*> vRoles = {};
		std::vector<PriorityLabel_t*> vLabels = {};
		if (std::ranges::find(vTags, TagToIndex(IGNORED_TAG)) != vTags.end())
			return m_vTags[TagToIndex(IGNORED_TAG)];

		for (int iID : vTags)
		{
			if (auto pTag = GetTag(iID))
			{
				if (pTag->m_bLabel)
					vLabels.push_back(pTag);
				else
					vRoles.push_back(pTag);
			}
		}

		if (H::Entities.IsFriend(uAccountID))
		{
			auto pTag = &m_vTags[TagToIndex(FRIEND_TAG)];
			if (pTag->m_bLabel)
				vLabels.push_back(pTag);
			else
				vRoles.push_back(pTag);
		}
		if (H::Entities.InParty(uAccountID))
		{
			auto pTag = &m_vTags[TagToIndex(PARTY_TAG)];
			if (pTag->m_bLabel)
				vLabels.push_back(pTag);
			else
				vRoles.push_back(pTag);
		}
		if (H::Entities.IsF2P(uAccountID))
		{
			auto pTag = &m_vTags[TagToIndex(F2P_TAG)];
			if (pTag->m_bLabel)
				vLabels.push_back(pTag);
			else
				vRoles.push_back(pTag);
		}

		auto compare_tag = [](const PriorityLabel_t* a, const PriorityLabel_t* b) -> bool
		{
			if (a->m_iPriority != b->m_iPriority)
				return a->m_iPriority > b->m_iPriority;
			return a->m_sName < b->m_sName;
		};

		if (!vRoles.empty())
		{
			std::sort(vRoles.begin(), vRoles.end(), compare_tag);
			return *vRoles.front();
		}
		if (!vLabels.empty())
		{
			std::sort(vLabels.begin(), vLabels.end(), compare_tag);
			return *vLabels.front();
		}
		return {};
	};

	for (const auto& [uAccountID, vTags] : vTaggedAccounts)
	{
		MarkedPlayer_t tPlayer = {};
		tPlayer.m_uAccountID = uAccountID;
		if (auto it = mAliases.find(uAccountID); it != mAliases.end())
			tPlayer.m_sAlias = it->second;
		tPlayer.m_sDisplayName = ResolveAccountName(uAccountID, tPlayer.m_sAlias);

		tPlayer.m_vRoleTags.reserve(vTags.size());
		tPlayer.m_vLabelTags.reserve(vTags.size());
		for (int iID : vTags)
		{
			if (auto pTag = GetTag(iID))
			{
				if (pTag->m_bLabel)
					tPlayer.m_vLabelTags.push_back(iID);
				else
					tPlayer.m_vRoleTags.push_back(iID);
			}
		}

		tPlayer.m_tRole = getRoleTag(vTags, uAccountID);
		tPlayer.m_sRoleName = tPlayer.m_tRole.m_sName;
		vMarkedPlayers.emplace_back(std::move(tPlayer));
	}

	std::sort(vMarkedPlayers.begin(), vMarkedPlayers.end(), [](const MarkedPlayer_t& a, const MarkedPlayer_t& b) -> bool
	{
		if (a.m_tRole.m_iPriority != b.m_tRole.m_iPriority)
			return a.m_tRole.m_iPriority > b.m_tRole.m_iPriority;
		if (a.m_tRole.m_sName != b.m_tRole.m_sName)
			return a.m_tRole.m_sName < b.m_tRole.m_sName;
		if (a.m_sDisplayName != b.m_sDisplayName)
			return a.m_sDisplayName < b.m_sDisplayName;
		return a.m_uAccountID < b.m_uAccountID;
	});

	return vMarkedPlayers;
}

bool CPlayerlistUtils::SetPlayerRole(uint32_t uAccountID, int iID, bool bSave, const char* sName)
{
	if (!uAccountID)
		return false;

	std::vector<int> vRoleTags = {};
	{
		auto it = m_mPlayerTags.find(uAccountID);
		if (it != m_mPlayerTags.end())
		{
			for (int iTag : it->second)
			{
				if (auto pTag = GetTag(iTag); pTag && !pTag->m_bLabel)
					vRoleTags.push_back(iTag);
			}
		}
	}

	bool bChanged = false;
	for (int iTag : vRoleTags)
	{
		RemoveTag(uAccountID, iTag, bSave, sName);
		bChanged = true;
	}

	if (iID >= 0)
	{
		auto pTag = GetTag(iID);
		if (pTag && pTag->m_bAssignable && !pTag->m_bLabel)
		{
			if (!HasTag(uAccountID, iID))
			{
				AddTag(uAccountID, iID, bSave, sName);
				bChanged = true;
			}
		}
	}

	return bChanged;
}

void CPlayerlistUtils::UpdateCheaterRecord(uint32_t uAccountID, const char* sName, const char* sReason, int iDetections, bool bAuto)
{
	if (!uAccountID)
		return;

	F::SteamProfileCache.TouchAvatar(uAccountID);

	auto& tRecord = m_mCheaterRecords[uAccountID];
	tRecord.m_uAccountID = uAccountID;
	if (sName && *sName && !IsPlaceholderName(sName))
		tRecord.m_sName = sName;
	else if (tRecord.m_sName.empty() || tRecord.m_sName == "Unknown")
		tRecord.m_sName = ResolveAccountName(uAccountID);

	if (sReason && *sReason)
		tRecord.m_sReason = sReason;
	else if (tRecord.m_sReason.empty())
		tRecord.m_sReason = "tagged by the player";

	if (iDetections > 0)
		tRecord.m_iDetections = std::max(tRecord.m_iDetections, iDetections);
	else if (bAuto && tRecord.m_iDetections <= 0)
		tRecord.m_iDetections = 1;

	tRecord.m_bAuto = bAuto;
	tRecord.m_iTimestamp = I::GlobalVars ? I::GlobalVars->tickcount : int(std::time(nullptr));

	m_bCheaterSave = true;
}

void CPlayerlistUtils::RemoveCheaterRecord(uint32_t uAccountID, bool bMarkSave)
{
	if (!uAccountID || !m_mCheaterRecords.contains(uAccountID))
		return;

	m_mCheaterRecords.erase(uAccountID);
	if (bMarkSave)
		m_bCheaterSave = true;
}

std::vector<std::pair<uint32_t, CheaterRecord_t>> CPlayerlistUtils::GetCheaterVector()
{
	std::shared_lock tLock(m_tMutex);
	std::vector<std::pair<uint32_t, CheaterRecord_t>> vCheaters;
	vCheaters.reserve(m_mCheaterRecords.size());
	for (auto& [uAccountID, tRecord] : m_mCheaterRecords)
	{
		auto tCopy = tRecord;
		if (tCopy.m_sName.empty() || tCopy.m_sName == "Unknown" || IsPlaceholderName(tCopy.m_sName))
			tCopy.m_sName = ResolveAccountName(uAccountID);
		vCheaters.emplace_back(uAccountID, tCopy);
	}
	return vCheaters;
}

bool CPlayerlistUtils::ImportCheatersFromJson(const std::string& sJson, bool bMarkDirty)
{
	try
	{
		boost::property_tree::ptree tRead;
		std::stringstream ssStream;
		ssStream << sJson;
		read_json(ssStream, tRead);

		std::unordered_map<uint32_t, CheaterRecord_t> mTemp;
		if (auto tCheaters = tRead.get_child_optional("Cheaters"))
		{
			for (auto& [sAccount, tChild] : *tCheaters)
			{
				uint32_t uAccountID = std::stoul(sAccount);
				CheaterRecord_t tRecord;
				tRecord.m_uAccountID = uAccountID;
				tRecord.m_sName = tChild.get<std::string>("Name", "Unknown");
				tRecord.m_sReason = tChild.get<std::string>("Reason", "tagged by the player");
				tRecord.m_iDetections = tChild.get<int>("Detections", 0);
				tRecord.m_bAuto = tChild.get<bool>("Auto", false);
				tRecord.m_iTimestamp = tChild.get<int>("Timestamp", 0);
				mTemp[uAccountID] = tRecord;
			}
		}

		{
			std::lock_guard tLock(m_tMutex);
			m_mCheaterRecords = std::move(mTemp);
			for (const auto& [uAccountID, _] : m_mCheaterRecords)
			{
				if (uAccountID)
					F::SteamProfileCache.TouchAvatar(uAccountID);
			}
		}

		m_bCheaterSave = bMarkDirty;
		return true;
	}
	catch (...)
	{
		return false;
	}
}

std::string CPlayerlistUtils::ExportCheatersToJson() const
{
	boost::property_tree::ptree tWrite;
	boost::property_tree::ptree tCheaters;
	{
		std::shared_lock tLock(m_tMutex);
		for (auto& [uAccountID, tRecord] : m_mCheaterRecords)
		{
			boost::property_tree::ptree tEntry;
			tEntry.put("Name", tRecord.m_sName.empty() ? "Unknown" : tRecord.m_sName);
			tEntry.put("Reason", tRecord.m_sReason.empty() ? "tagged by the player" : tRecord.m_sReason);
			tEntry.put("Detections", tRecord.m_iDetections);
			tEntry.put("Auto", tRecord.m_bAuto);
			tEntry.put("Timestamp", tRecord.m_iTimestamp);
			tCheaters.put_child(std::to_string(uAccountID), tEntry);
		}
		tLock.unlock();
	}
	tWrite.put_child("Cheaters", tCheaters);

	std::ostringstream oss;
	write_json(oss, tWrite);
	return oss.str();
}
