#ifndef TEXTMODE
#include "CaptureOverlay.h"

#include <ImGui/imgui_impl_dx9.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

static constexpr const wchar_t* s_szClassName = L"GdiPlusHostWindow";

static LRESULT WINAPI OverlayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static HMODULE GetOwnModule()
{
	HMODULE hModule = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&OverlayWndProc), &hModule) || !hModule)
		hModule = GetModuleHandleW(nullptr);
	return hModule;
}

DWORD WINAPI CCaptureOverlay::WindowThread(LPVOID pParam)
{
	auto pThis = static_cast<CCaptureOverlay*>(pParam);
	const HINSTANCE hInstance = GetOwnModule();

	WNDCLASSEXW tClass{};
	tClass.cbSize = sizeof(tClass);
	tClass.lpfnWndProc = OverlayWndProc;
	tClass.hInstance = hInstance;
	tClass.lpszClassName = s_szClassName;
	RegisterClassExW(&tClass);

	HWND hWindow = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
		s_szClassName, L"", WS_POPUP,
		0, 0, 1, 1,
		nullptr, nullptr, hInstance, nullptr);

	DWORD dwAffinity = 0;
	if (!hWindow
		|| !SetWindowDisplayAffinity(hWindow, WDA_EXCLUDEFROMCAPTURE)
		|| !GetWindowDisplayAffinity(hWindow, &dwAffinity)
		|| dwAffinity != WDA_EXCLUDEFROMCAPTURE)
	{
		if (hWindow)
			DestroyWindow(hWindow);
		UnregisterClassW(s_szClassName, hInstance);
		pThis->m_bThreadFailed = true;
		SetEvent(pThis->m_hReady);
		return 0;
	}

	pThis->m_hWindow = hWindow;
	SetEvent(pThis->m_hReady);

	MSG tMsg;
	while (GetMessageW(&tMsg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&tMsg);
		DispatchMessageW(&tMsg);
	}

	pThis->m_hWindow = nullptr;
	UnregisterClassW(s_szClassName, hInstance);
	return 0;
}

bool CCaptureOverlay::StartWindowThread()
{
	if (m_hWindow)
		return true;
	if (m_bThreadFailed)
		return false;

	if (!m_hThread)
	{
		m_hReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (m_hReady)
			m_hThread = CreateThread(nullptr, 0, WindowThread, this, 0, nullptr);
		if (!m_hThread)
		{
			m_bThreadFailed = true;
			return false;
		}
		WaitForSingleObject(m_hReady, 2000);
	}

	return m_hWindow != nullptr;
}

void CCaptureOverlay::ApplyWindowStyle()
{
	const HWND hWindow = m_hWindow;
	if (!hWindow || (m_hStyledWindow == hWindow && m_eStyledMode == m_eMode))
		return;
	m_hStyledWindow = hWindow;
	m_eStyledMode = m_eMode;

	const MARGINS tMargins = m_eMode == Mode_t::Alpha ? MARGINS{ -1, -1, -1, -1 } : MARGINS{};
	DwmExtendFrameIntoClientArea(hWindow, &tMargins);
}

void CCaptureOverlay::ClearGameSurface()
{
	if (!m_pDevice || m_hClearedWindow == m_hWindow)
		return;
	m_hClearedWindow = m_hWindow;

	m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
}

bool CCaptureOverlay::SelectMode(IDirect3DDevice9* pDevice)
{
	IDirect3DSwapChain9* pMain = nullptr;
	D3DPRESENT_PARAMETERS tParams{};
	const bool bQueried = SUCCEEDED(pDevice->GetSwapChain(0, &pMain)) && pMain
		&& SUCCEEDED(pMain->GetPresentParameters(&tParams));
	if (pMain)
		pMain->Release();
	if (!bQueried)
	{
		m_bUnsupported = true;
		NotifyUnavailable("the game swapchain could not be queried");
		return false;
	}

	if (!tParams.Windowed)
	{
		m_bUnsupported = true;
		NotifyUnavailable("the game is running in exclusive fullscreen");
		return false;
	}

	m_Format = tParams.BackBufferFormat;
	IDirect3DSurface9* pBackBuffer = nullptr;
	if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) && pBackBuffer)
	{
		D3DSURFACE_DESC tDesc{};
		if (SUCCEEDED(pBackBuffer->GetDesc(&tDesc)) && tDesc.Format != D3DFMT_UNKNOWN)
			m_Format = tDesc.Format;
		pBackBuffer->Release();
	}
	if (m_Format == D3DFMT_UNKNOWN)
		m_Format = D3DFMT_X8R8G8B8;

	HMODULE hDevice = nullptr;
	wchar_t sDevice[MAX_PATH]{}, sSystem[MAX_PATH]{};
	const bool bNative = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>((*reinterpret_cast<void***>(pDevice))[17]), &hDevice) && hDevice
		&& GetModuleFileNameW(hDevice, sDevice, MAX_PATH)
		&& GetSystemDirectoryW(sSystem, MAX_PATH)
		&& _wcsnicmp(sDevice, sSystem, wcslen(sSystem)) == 0;

	if (!bNative)
	{
		m_bUnsupported = true;
		NotifyUnavailable("the Vulkan backend (DXVK) does not support overlays outside the game swapchain");
		return false;
	}

	IDirect3DDevice9Ex* pEx = nullptr;
	const bool bEx = SUCCEEDED(pDevice->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&pEx))) && pEx;
	if (pEx)
		pEx->Release();

	m_eMode = bEx ? Mode_t::Alpha : Mode_t::Opaque;
	return true;
}

void CCaptureOverlay::NotifyUnavailable(const char* sReason)
{
	if (m_bNotified)
		return;
	m_bNotified = true;
	const std::string sMessage = std::format("Anti-OBS off: {}. Windowed mode is required for capture exclusion.", sReason);
	SDK::Output("Anti-OBS", sMessage.c_str(), { 255, 190, 120, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_CHAT);
}

void CCaptureOverlay::SetVisible(bool bVisible)
{
	const HWND hWindow = m_hWindow;
	if (!hWindow || bVisible == m_bVisible)
		return;
	m_bVisible = bVisible;

	if (bVisible)
		SetWindowPos(hWindow, HWND_TOPMOST, m_tLastRect.left, m_tLastRect.top, m_nWidth, m_nHeight,
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS | SWP_SHOWWINDOW);
	else
		ShowWindowAsync(hWindow, SW_HIDE);
}

void CCaptureOverlay::ReleaseSwapChain()
{
	if (m_pSwapChain)
	{
		m_pSwapChain->Release();
		m_pSwapChain = nullptr;
	}
	m_bAvailable = false;
	SetVisible(false);
}

bool CCaptureOverlay::TrackGameWindow()
{
	if (!m_hGameWindow || !IsWindow(m_hGameWindow))
		m_hGameWindow = SDK::GetTeamFortressWindow();
	if (!m_hGameWindow || IsIconic(m_hGameWindow) || GetForegroundWindow() != m_hGameWindow)
		return false;

	RECT tClient{};
	POINT tTopLeft{};
	if (!GetClientRect(m_hGameWindow, &tClient) || !ClientToScreen(m_hGameWindow, &tTopLeft))
		return false;

	const int nWidth = tClient.right - tClient.left;
	const int nHeight = tClient.bottom - tClient.top;
	if (nWidth <= 0 || nHeight <= 0)
		return false;

	if (nWidth != m_nWidth || nHeight != m_nHeight)
	{
		ReleaseSwapChain();
		m_bSwapChainFailed = false;
		m_nWidth = nWidth;
		m_nHeight = nHeight;
	}

	const RECT tRect{ tTopLeft.x, tTopLeft.y, tTopLeft.x + nWidth, tTopLeft.y + nHeight };
	if (memcmp(&tRect, &m_tLastRect, sizeof(RECT)) != 0)
	{
		m_tLastRect = tRect;
		if (const HWND hWindow = m_hWindow)
			SetWindowPos(hWindow, HWND_TOPMOST, tRect.left, tRect.top, nWidth, nHeight,
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS | (m_bVisible ? SWP_SHOWWINDOW : 0));
	}

	return true;
}

bool CCaptureOverlay::CreateSwapChain(IDirect3DDevice9* pDevice)
{
	if (m_pSwapChain)
		return true;
	if (m_bSwapChainFailed)
		return false;
	if (!m_nWidth || !m_nHeight)
		return false;

	D3DPRESENT_PARAMETERS tParams{};
	tParams.Windowed = TRUE;
	tParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
	tParams.BackBufferFormat = m_eMode == Mode_t::Alpha ? D3DFMT_A8R8G8B8 : m_Format;
	tParams.BackBufferWidth = m_nWidth;
	tParams.BackBufferHeight = m_nHeight;
	tParams.BackBufferCount = 1;
	tParams.hDeviceWindow = m_hWindow;
	tParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	tParams.EnableAutoDepthStencil = FALSE;

	if (SUCCEEDED(pDevice->CreateAdditionalSwapChain(&tParams, &m_pSwapChain)) && m_pSwapChain)
		return true;
	m_pSwapChain = nullptr;

	m_bSwapChainFailed = true;
	m_bUnsupported = true;
	NotifyUnavailable("an overlay swapchain could not be created for this device");
	return false;
}

bool CCaptureOverlay::Prepare(IDirect3DDevice9* pDevice)
{
	m_bAvailable = false;
	if (!pDevice)
		return false;

	if (pDevice != m_pDevice)
	{
		ReleaseDeviceObjects();
		m_pDevice = pDevice;
	}
	if (m_bSwapChainFailed)
		return false;

	if (m_eMode == Mode_t::None && !SelectMode(pDevice))
	{
		SetVisible(false);
		return false;
	}

	if (!StartWindowThread())
	{
		m_bUnsupported = true;
		NotifyUnavailable("the overlay window is not capturable on this system");
		SetVisible(false);
		return false;
	}

	if (!TrackGameWindow() || !CreateSwapChain(pDevice))
	{
		SetVisible(false);
		return false;
	}

	ClearGameSurface();
	return m_bAvailable = true;
}

void CCaptureOverlay::Present(IDirect3DDevice9* pDevice, ImDrawData* pDrawData)
{
	if (!m_bAvailable || !m_pSwapChain || !pDevice || pDevice != m_pDevice)
		return;

	ApplyWindowStyle();

	IDirect3DSurface9* pBackBuffer = nullptr;
	if (FAILED(m_pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) || !pBackBuffer)
	{
		ReleaseSwapChain();
		return;
	}

	IDirect3DSurface9* pOldTarget = nullptr;
	IDirect3DSurface9* pOldDepth = nullptr;
	D3DVIEWPORT9 tOldViewport{};
	DWORD dwColorWrite = 0, dwScissor = 0;
	pDevice->GetRenderTarget(0, &pOldTarget);
	pDevice->GetDepthStencilSurface(&pOldDepth);
	pDevice->GetViewport(&tOldViewport);
	pDevice->GetRenderState(D3DRS_COLORWRITEENABLE, &dwColorWrite);
	pDevice->GetRenderState(D3DRS_SCISSORTESTENABLE, &dwScissor);

	ClearGameSurface();

	if (m_eMode == Mode_t::Opaque)
	{
		IDirect3DSurface9* pSource = pOldTarget;
		D3DSURFACE_DESC tSource{}, tTarget{};
		if (pSource && SUCCEEDED(pSource->GetDesc(&tSource)) && SUCCEEDED(pBackBuffer->GetDesc(&tTarget))
			&& tSource.Format == tTarget.Format)
		{
			const RECT tDestination{ 0, 0, static_cast<LONG>(tTarget.Width), static_cast<LONG>(tTarget.Height) };
			pDevice->SetRenderTarget(0, nullptr);
			pDevice->StretchRect(pSource, nullptr, pBackBuffer, &tDestination, D3DTEXF_NONE);
		}
	}

	ClearGameSurface();
	pDevice->SetRenderTarget(0, pBackBuffer);
	pDevice->SetDepthStencilSurface(nullptr);
	pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	if (m_eMode == Mode_t::Alpha)
		pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
	if (pDrawData)
		ImGui_ImplDX9_RenderDrawData(pDrawData);

	pDevice->SetRenderTarget(0, pOldTarget);
	pDevice->SetDepthStencilSurface(pOldDepth);
	pDevice->SetViewport(&tOldViewport);
	pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, dwColorWrite);
	pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, dwScissor);

	if (pOldTarget)
		pOldTarget->Release();
	if (pOldDepth)
		pOldDepth->Release();
	pBackBuffer->Release();

	if (FAILED(m_pSwapChain->Present(nullptr, nullptr, nullptr, nullptr, 0)))
	{
		ReleaseSwapChain();
		return;
	}

	SetVisible(true);
}

void CCaptureOverlay::Hide()
{
	SetVisible(false);
	m_bAvailable = false;
}

void CCaptureOverlay::ReleaseDeviceObjects()
{
	ReleaseSwapChain();
	m_pDevice = nullptr;
	m_eMode = Mode_t::None;
	m_Format = D3DFMT_UNKNOWN;
	m_hStyledWindow = nullptr;
	m_hClearedWindow = nullptr;
	m_eStyledMode = Mode_t::None;
	m_bSwapChainFailed = false;
	m_bUnsupported = false;
}

void CCaptureOverlay::Shutdown()
{
	ReleaseDeviceObjects();

	if (const HWND hWindow = m_hWindow)
		PostMessageW(hWindow, WM_CLOSE, 0, 0);
	if (m_hThread)
	{
		if (WaitForSingleObject(m_hThread, 2000) == WAIT_TIMEOUT)
			PostThreadMessageW(GetThreadId(m_hThread), WM_QUIT, 0, 0), WaitForSingleObject(m_hThread, 1000);
		CloseHandle(m_hThread);
		m_hThread = nullptr;
	}
	if (m_hReady)
	{
		CloseHandle(m_hReady);
		m_hReady = nullptr;
	}

	m_hWindow = nullptr;
	m_hGameWindow = nullptr;
	m_hStyledWindow = nullptr;
	m_hClearedWindow = nullptr;
	m_eStyledMode = Mode_t::None;
	m_nWidth = m_nHeight = 0;
	m_tLastRect = {};
	m_bVisible = false;
	m_bThreadFailed = false;
	m_bSwapChainFailed = false;
	m_bUnsupported = false;
	m_bNotified = false;
}
#endif
