#include "Interfaces.h"

#include "../../Core/Core.h"

#define Validate(x) if (!x) { U::Core.AppendFailText("CNullInterfaces::Initialize() failed to initialize "#x); m_bFailed = true; }
#define ValidateNonLethal(x) if (!x) { const char* sMessage = "CNullInterfaces::Initialize() failed to initialize "#x; MessageBox(nullptr, sMessage, "Warning", MB_OK | MB_ICONERROR); U::Core.AppendFailText(sMessage); }

#ifdef TEXTMODE
#define ValidateSteam(x) if (!x) { U::Core.AppendFailText("CNullInterfaces::Initialize() failed to initialize "#x" in Textmode"); }
#else
#define ValidateSteam(x) Validate(x)
#endif


bool CNullInterfaces::Initialize()
{
	I::TFPartyClient = S::Get_TFPartyClient.Call<CTFPartyClient*>();
	Validate(I::TFPartyClient);

	I::KeyValuesSystem = U::Memory.GetModuleExport<IKeyValuesSystem*(*)()>("vstdlib.dll", "KeyValuesSystem")();
	Validate(I::KeyValuesSystem);

	if (!I::SteamClient)
		return !m_bFailed;

	using GetPipeFn = HSteamPipe(__cdecl*)();
	using GetUserFn = HSteamUser(__cdecl*)();
	auto GetPipe = U::Memory.GetModuleExport<GetPipeFn>("steam_api64.dll", "SteamAPI_GetHSteamPipe");
	if (!GetPipe)
		GetPipe = U::Memory.GetModuleExport<GetPipeFn>("steam_api.dll", "SteamAPI_GetHSteamPipe");
	auto GetUser = U::Memory.GetModuleExport<GetUserFn>("steam_api64.dll", "SteamAPI_GetHSteamUser");
	if (!GetUser)
		GetUser = U::Memory.GetModuleExport<GetUserFn>("steam_api.dll", "SteamAPI_GetHSteamUser");

	HSteamPipe hPipe = GetPipe ? GetPipe() : 0;
	HSteamUser hUser = GetUser ? GetUser() : 0;
	if (!hPipe || !hUser)
	{
		hPipe = I::SteamClient->CreateSteamPipe();
		ValidateSteam(hPipe);
		hUser = I::SteamClient->ConnectToGlobalUser(hPipe);
		ValidateSteam(hUser);
	}

	I::SteamFriends = I::SteamClient->GetISteamFriends(hUser, hPipe, STEAMFRIENDS_INTERFACE_VERSION);
	ValidateSteam(I::SteamFriends);

	I::SteamUtils = I::SteamClient->GetISteamUtils(hPipe, STEAMUTILS_INTERFACE_VERSION);
	ValidateSteam(I::SteamUtils);

	I::SteamApps = I::SteamClient->GetISteamApps(hUser, hPipe, STEAMAPPS_INTERFACE_VERSION);
	ValidateNonLethal(I::SteamApps);

	I::SteamUserStats = I::SteamClient->GetISteamUserStats(hUser, hPipe, STEAMUSERSTATS_INTERFACE_VERSION);
	ValidateSteam(I::SteamUserStats);

	I::SteamUser = I::SteamClient->GetISteamUser(hUser, hPipe, STEAMUSER_INTERFACE_VERSION);
	ValidateSteam(I::SteamUser);

	I::SteamScreenshots = I::SteamClient->GetISteamScreenshots(hUser, hPipe, STEAMSCREENSHOTS_INTERFACE_VERSION);
	ValidateNonLethal(I::SteamScreenshots);

	S::Get_SteamNetworkingUtils.Call<ISteamNetworkingUtils*>(&I::SteamNetworkingUtils);
	ValidateSteam(I::SteamNetworkingUtils);

	I::SteamMatchmakingServers = I::SteamClient->GetISteamMatchmakingServers(hUser, hPipe, STEAMMATCHMAKINGSERVERS_INTERFACE_VERSION);
	ValidateSteam(I::SteamMatchmakingServers);

	return !m_bFailed;
}