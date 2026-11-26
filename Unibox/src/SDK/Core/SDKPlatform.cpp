#include "../SDK.h"

#include "../../Features/ImGui/Notifications/Notifications.h"
#include "../../Features/ImGui/Menu/Menu.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>

static BOOL CALLBACK TeamFortressWindow(HWND hWindow, LPARAM lParam)
{
	DWORD iProcess1 = GetCurrentProcessId();
	DWORD iProcess2; GetWindowThreadProcessId(hWindow, &iProcess2);
	if (iProcess1 != iProcess2)
		return TRUE;

	char sWindowTitle[64];
	GetWindowText(hWindow, sWindowTitle, sizeof(sWindowTitle));
	switch (FNV1A::Hash32(sWindowTitle))
	{
	case FNV1A::Hash32Const("Team Fortress 2 - Direct3D 9 - 64 Bit"):
	case FNV1A::Hash32Const("Team Fortress 2 - Vulkan - 64 Bit"):
		break;
	default:
		return TRUE;
	}

	*reinterpret_cast<HWND*>(lParam) = hWindow;
	return FALSE;
}

void SDK::Output(const char* sFunction, const char* sLog, Color_t tColor,
	int iTo, const char* sIcon, int iMessageBox,
	const char* sLeft, const char* sRight)
{
	if (sLog)
	{
		if (iTo & OUTPUT_CONSOLE)
		{
			I::CVar->ConsoleColorPrintf(tColor, "%s%s%s ", sLeft, sFunction, sRight);
			I::CVar->ConsoleColorPrintf({}, "%s\n", sLog);
		}
		if (iTo & OUTPUT_DEBUG)
			OutputDebugString(std::format("{}{}{} {}\n", sLeft, sFunction, sRight, sLog).c_str());
		if (iTo & OUTPUT_TOAST)
			F::Notifications.Add(sLog, sIcon, tColor);
		if (iTo & OUTPUT_MENU)
			F::Menu.AddOutput(std::format("{}{}{}", sLeft, sFunction, sRight).c_str(), sLog, tColor);
		if (iTo & OUTPUT_CHAT)
			I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("{}{}{}{}\x1 {}", tColor.ToHex(), sLeft, sFunction, sRight, sLog).c_str());
		if (iTo & OUTPUT_PARTY)
			I::TFPartyClient->SendPartyChat(sLog);
		if (iMessageBox != -1)
			MessageBox(nullptr, sLog, sFunction, iMessageBox);
	}
	else
	{
		if (iTo & OUTPUT_CONSOLE)
			I::CVar->ConsoleColorPrintf(tColor, "%s\n", sFunction);
		if (iTo & OUTPUT_DEBUG)
			OutputDebugString(std::format("{}\n", sFunction).c_str());
		if (iTo & OUTPUT_TOAST)
			F::Notifications.Add(sFunction, sIcon, tColor);
		if (iTo & OUTPUT_MENU)
			F::Menu.AddOutput("", sFunction, tColor);
		if (iTo & OUTPUT_CHAT)
			I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("{}{}\x1", tColor.ToHex(), sFunction).c_str());
		if (iTo & OUTPUT_PARTY)
			I::TFPartyClient->SendPartyChat(sFunction);
		if (iMessageBox != -1)
			MessageBox(nullptr, "", sFunction, iMessageBox);
	}
}

void SDK::SetClipboard(const std::string& sString)
{
	if (OpenClipboard(nullptr))
	{
		EmptyClipboard();

		if (HGLOBAL hMemory = GlobalAlloc(GMEM_DDESHARE, sString.length() + 1))
		{
			if (void* pData = GlobalLock(hMemory))
			{
				memset(pData, 0, sString.length() + 1);
				memcpy(pData, sString.c_str(), sString.length());
				GlobalUnlock(hMemory);
				SetClipboardData(CF_TEXT, hMemory);
			}
		}

		CloseClipboard();
	}
}

std::string SDK::GetClipboard()
{
	std::string sString = "";
	if (OpenClipboard(nullptr))
	{
		if (void* pData = GetClipboardData(CF_TEXT))
			sString = (char*)(pData);

		CloseClipboard();
	}
	return sString;
}

std::string SDK::GetDate()
{
	time_t tTime = time(nullptr);
	tm timeinfo; localtime_s(&timeinfo, &tTime);
	char buffer[16]; strftime(buffer, sizeof(buffer), "%b %e %Y", &timeinfo);
	return buffer;
}

std::string SDK::GetTime()
{
	time_t tTime = time(nullptr);
	tm timeinfo; localtime_s(&timeinfo, &tTime);
	char buffer[16]; strftime(buffer, sizeof(buffer), "%T", &timeinfo);
	return buffer;
}

std::wstring SDK::ConvertUtf8ToWide(const std::string& sSource)
{
	if (sSource.empty())
		return L"";

	int iSize = MultiByteToWideChar(CP_UTF8, 0, sSource.data(), -1, nullptr, 0);
	if (iSize <= 0)
		return L"";

	std::wstring sResult(iSize, 0);
	MultiByteToWideChar(CP_UTF8, 0, sSource.data(), -1, sResult.data(), iSize);
	sResult.pop_back(); return sResult;
}

std::string SDK::ConvertWideToUTF8(const std::wstring& sSource)
{
	if (sSource.empty())
		return "";

	int iSize = WideCharToMultiByte(CP_UTF8, 0, sSource.data(), -1, nullptr, 0, nullptr, nullptr);
	if (iSize <= 0)
		return "";

	std::string sResult(iSize, 0);
	WideCharToMultiByte(CP_UTF8, 0, sSource.data(), -1, sResult.data(), iSize, nullptr, nullptr);
	sResult.pop_back(); return sResult;
}

HWND SDK::GetTeamFortressWindow()
{
	static HWND hWindow = nullptr;
	if (!hWindow)
		EnumWindows(TeamFortressWindow, reinterpret_cast<LPARAM>(&hWindow));
	return hWindow;
}

bool SDK::IsGameWindowInFocus()
{
	HWND hWindow = GetTeamFortressWindow();
	return hWindow == GetForegroundWindow() || !hWindow;
}

bool SDK::StdRandomBool()
{
	return StdRandomInt(0, 1);
}
int SDK::StdRandomInt(int iMin, int iMax)
{
	std::uniform_int_distribution<int> iDistribution(iMin, iMax);
	return iDistribution(Random);
}
float SDK::StdRandomFloat(float flMin, float flMax)
{
	std::uniform_real_distribution<float> flDistribution(flMin, flMax);
	return flDistribution(Random);
}

int SDK::SeedFileLineHash(int iSeed, const char* sName, int iAdditionalSeed)
{
	CRC32_t iReturn;
	CRC32_Init(&iReturn);
	CRC32_ProcessBuffer(&iReturn, &iSeed, sizeof(int));
	CRC32_ProcessBuffer(&iReturn, &iAdditionalSeed, sizeof(int));
	CRC32_ProcessBuffer(&iReturn, sName, int(strlen(sName)));
	CRC32_Final(&iReturn);
	return static_cast<int>(iReturn);
}

void SDK::RandomSeed(int iSeed)
{
	static auto RandomSeed = U::Memory.GetModuleExport<void(*)(uint32_t)>("vstdlib.dll", "RandomSeed");
	RandomSeed(iSeed);
}
int SDK::RandomInt(int iMinVal, int iMaxVal)
{
	static auto RandomInt = U::Memory.GetModuleExport<int(*)(int, int)>("vstdlib.dll", "RandomInt");
	return RandomInt(iMinVal, iMaxVal);
}
float SDK::RandomFloat(float flMinVal, float flMaxVal)
{
	static auto RandomFloat = U::Memory.GetModuleExport<float(*)(float, float)>("vstdlib.dll", "RandomFloat");
	return RandomFloat(flMinVal, flMaxVal);
}

double SDK::PlatFloatTime()
{
	static auto Plat_FloatTime = U::Memory.GetModuleExport<double(*)()>("tier0.dll", "Plat_FloatTime");
	return Plat_FloatTime();
}

double SDK::InitNowMs()
{
	static const double flFreq = []()
	{
		LARGE_INTEGER tFreq = {};
		QueryPerformanceFrequency(&tFreq);
		return tFreq.QuadPart ? double(tFreq.QuadPart) : 1.0;
	}();

	LARGE_INTEGER tNow = {};
	QueryPerformanceCounter(&tNow);
	return 1000.0 * double(tNow.QuadPart) / flFreq;
}

void SDK::LogInitTiming(const char* sStage, double flMs)
{
	const std::string sMessage = std::format("{}: {:.2f} ms", sStage ? sStage : "?", flMs);
	if (I::CVar)
		Output("init", sMessage.c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	else
		OutputDebugStringA(std::format("[init] {}\n", sMessage).c_str());
}

int SDK::GetActiveDXLevel()
{
	if (auto pDxLevel = H::ConVars.FindVar("mat_dxlevel"))
		return pDxLevel->GetInt();
	if (I::EngineClient)
		return I::EngineClient->GetDXSupportLevel();
	return 90;
}

bool SDK::SupportsDX9Shaders()
{
	return GetActiveDXLevel() >= 90;
}

SDK::CInitTimingScope::CInitTimingScope(const char* szName)
	: m_szName(szName), m_flStart(InitNowMs())
{
}

SDK::CInitTimingScope::~CInitTimingScope()
{
	LogInitTiming(m_szName, InitNowMs() - m_flStart);
}

static int s_nCleanScreenshotFrames = 0;
static bool s_bPendingSteamScreenshot = false;
static bool s_bSteamScreenshotsHooked = false;
static bool s_bSteamScreenshotCallbackRegistered = false;

class CSteamScreenshotRequestedCallback final : public CCallbackBase
{
public:
	void Run(void*) override
	{
		TRACK_HOOK;

		SDK::NotifyCleanScreenshot();
		s_bPendingSteamScreenshot = true;
	}
	void Run(void* pParam, bool, SteamAPICall_t) override { Run(pParam); }
	int GetCallbackSizeBytes() override { return sizeof(ScreenshotRequested_t); }
};

static CSteamScreenshotRequestedCallback s_tSteamScreenshotCallback;

static void* GetSteamApiExport(const char* sName)
{
	if (auto pExport = U::Memory.GetModuleExport<void*>("steam_api64.dll", sName))
		return pExport;
	return U::Memory.GetModuleExport<void*>("steam_api.dll", sName);
}

static void RegisterSteamScreenshotCallback()
{
	if (s_bSteamScreenshotCallbackRegistered)
		return;

	using RegisterFn = void(__cdecl*)(CCallbackBase*, int);
	auto RegisterCallback = reinterpret_cast<RegisterFn>(GetSteamApiExport("SteamAPI_RegisterCallback"));
	if (!RegisterCallback)
		return;

	RegisterCallback(&s_tSteamScreenshotCallback, ScreenshotRequested_t::k_iCallback);
	s_bSteamScreenshotCallbackRegistered = true;
}

static void UnregisterSteamScreenshotCallback()
{
	if (!s_bSteamScreenshotCallbackRegistered)
		return;

	using UnregisterFn = void(__cdecl*)(CCallbackBase*);
	auto UnregisterCallback = reinterpret_cast<UnregisterFn>(GetSteamApiExport("SteamAPI_UnregisterCallback"));
	if (UnregisterCallback)
		UnregisterCallback(&s_tSteamScreenshotCallback);
	s_bSteamScreenshotCallbackRegistered = false;
}

static void SubmitCleanSteamScreenshot()
{
	if (!I::SteamScreenshots || !I::MaterialSystem)
		return;

	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext)
		return;

	int nWidth = 0, nHeight = 0;
	pRenderContext->GetRenderTargetDimensions(nWidth, nHeight);
	if (nWidth <= 0 || nHeight <= 0)
		I::MaterialSystem->GetBackBufferDimensions(nWidth, nHeight);
	if (nWidth <= 0 || nHeight <= 0 || nWidth > 7680 || nHeight > 4320)
	{
		pRenderContext->Release();
		return;
	}

	const auto nBytes = static_cast<uint32>(nWidth) * nHeight * 3;
	std::vector<unsigned char> vRgb(nBytes);
	pRenderContext->ReadPixels(0, 0, nWidth, nHeight, vRgb.data(), IMAGE_FORMAT_RGB888);
	pRenderContext->Release();
	I::SteamScreenshots->WriteScreenshot(vRgb.data(), nBytes, nWidth, nHeight);
}

void SDK::NotifyCleanScreenshot()
{
	if (s_nCleanScreenshotFrames < 3)
		s_nCleanScreenshotFrames = 3;
}

void SDK::TickCleanScreenshot()
{
	if (s_bPendingSteamScreenshot && s_nCleanScreenshotFrames > 0)
	{
		SubmitCleanSteamScreenshot();
		s_bPendingSteamScreenshot = false;
	}

	if (s_nCleanScreenshotFrames > 0)
		s_nCleanScreenshotFrames--;
}

void SDK::UpdateSteamScreenshotHook()
{
	if (!Vars::Visuals::UI::CleanScreenshots.Value)
	{
		if (s_bSteamScreenshotsHooked && I::SteamScreenshots)
		{
			I::SteamScreenshots->HookScreenshots(false);
			s_bSteamScreenshotsHooked = false;
		}
		UnregisterSteamScreenshotCallback();
		s_bPendingSteamScreenshot = false;
		return;
	}

	if (!I::SteamScreenshots)
		return;

	RegisterSteamScreenshotCallback();

	static bool s_bF12Down = false;
	const bool bF12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
	if (bF12 && !s_bF12Down)
	{
		NotifyCleanScreenshot();
		s_bPendingSteamScreenshot = true;
	}
	s_bF12Down = bF12;

	if (!s_bSteamScreenshotsHooked)
	{
		I::SteamScreenshots->HookScreenshots(true);
		s_bSteamScreenshotsHooked = true;
	}
}

void SDK::ShutdownSteamScreenshotHook()
{
	s_bPendingSteamScreenshot = false;
	s_nCleanScreenshotFrames = 0;
	if (s_bSteamScreenshotsHooked && I::SteamScreenshots)
	{
		I::SteamScreenshots->HookScreenshots(false);
		s_bSteamScreenshotsHooked = false;
	}
	UnregisterSteamScreenshotCallback();
}

bool SDK::TakingScreenshot()
{
	if (!Vars::Visuals::UI::CleanScreenshots.Value)
		return false;
	if (s_nCleanScreenshotFrames > 0)
		return true;
	return I::EngineClient && I::EngineClient->IsTakingScreenshot();
}

bool SDK::AntiObs()
{
	return Vars::Visuals::AntiObs::Enabled.Value;
}

bool SDK::CleanScreenshot()
{
	return AntiObs() || TakingScreenshot();
}
