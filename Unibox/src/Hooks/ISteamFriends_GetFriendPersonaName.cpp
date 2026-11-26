#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Players/PlayerUtils.h"


MAKE_HOOK(ISteamFriends_GetFriendPersonaName, U::Memory.GetVirtual(I::SteamFriends, 7), const char*,
	void* rcx, CSteamID steamIDFriend)
{
	DEBUG_RETURN(ISteamFriends_GetFriendPersonaName, rcx, steamIDFriend);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::GetPlayerNameForSteamID_GetFriendPersonaName_Call();

	if (dwRetAddr == dwDesired && Vars::Visuals::UI::StreamerMode.Value)
	{
		switch (F::PlayerUtils.GetNameType(steamIDFriend.GetAccountID()))
		{
		case NameTypeEnum::Local: return LOCAL;
		case NameTypeEnum::Friend: return FRIEND;
		case NameTypeEnum::Party: return PARTY;
		}
	}

	return CALL_ORIGINAL(rcx, steamIDFriend);
}
#endif