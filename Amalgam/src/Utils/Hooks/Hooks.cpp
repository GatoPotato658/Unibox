#include "Hooks.h"

#include "../../Core/Core.h"
#include "../../Hooks/Direct3DDevice9.h"
#include <ranges>

CHook::CHook(const std::string& sName, void* pInitFunc)
{
	m_pInitFunc = pInitFunc;
	U::Hooks.m_mHooks[sName] = this;
}

bool CHooks::Initialize()
{
	m_bFailed = false;
	m_bInitialized = false;
	if (MH_Initialize() != MH_OK)
	{
		U::Core.AppendFailText("MinHook failed to initialize!");
		return false;
	}

#ifndef TEXTMODE
	WndProc::Initialize();
	if (!WndProc::hwWindow || !WndProc::Original)
		m_bFailed = true;
#endif

	for (auto& pHook : m_mHooks | std::views::values)
	{
		const bool bOK = reinterpret_cast<bool(__cdecl*)()>(pHook->m_pInitFunc)();
		m_bFailed = m_bFailed || !bOK;
	}

	if (!m_bFailed && MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
		m_bFailed = true;

	if (m_bFailed)
	{
		MH_DisableHook(MH_ALL_HOOKS);
#ifndef TEXTMODE
		if (WndProc::hwWindow && WndProc::Original && GetWindowLongPtr(WndProc::hwWindow, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(WndProc::Func))
			WndProc::Unload();
#endif
		Sleep(250);
		MH_Uninitialize();
		U::Core.AppendFailText("Hook initialization failed!");
		return false;
	}

	m_bInitialized = true;
	return true;
}

bool CHooks::Unload()
{
	if (!m_bInitialized)
		return true;

	m_bFailed = MH_DisableHook(MH_ALL_HOOKS) != MH_OK;
#ifndef TEXTMODE
	if (WndProc::hwWindow && WndProc::Original && GetWindowLongPtr(WndProc::hwWindow, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(WndProc::Func))
	{
		const auto hWindow = WndProc::hwWindow;
		WndProc::Unload();
		m_bFailed = m_bFailed || GetWindowLongPtr(hWindow, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(WndProc::Func);
	}
#endif
	Sleep(250);
	m_bFailed = MH_Uninitialize() != MH_OK || m_bFailed;
	m_bInitialized = false;
	if (m_bFailed)
		U::Core.AppendFailText("MinHook failed to unload all hooks!");
	return !m_bFailed;
}
