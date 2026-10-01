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
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&OverlayWndProc), &hModule);
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
		WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
		s_szClassName, L"", WS_POPUP,
		0, 0, 1, 1,
		nullptr, nullptr, hInstance, nullptr);

	const MARGINS tMargins{ -1, -1, -1, -1 };
	if (!hWindow
		|| !SetLayeredWindowAttributes(hWindow, 0, 255, LWA_ALPHA)
		|| FAILED(DwmExtendFrameIntoClientArea(hWindow, &tMargins))
		|| !SetWindowDisplayAffinity(hWindow, WDA_EXCLUDEFROMCAPTURE))
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
	if (m_bThreadFailed || m_hThread)
		return false;

	m_hReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (m_hReady)
		m_hThread = CreateThread(nullptr, 0, WindowThread, this, 0, nullptr);
	if (!m_hThread)
	{
		m_bThreadFailed = true;
		return false;
	}

	WaitForSingleObject(m_hReady, 2000);
	return m_hWindow != nullptr;
}

bool CCaptureOverlay::CheckDevice(IDirect3DDevice9* pDevice)
{
	if (m_bDeviceChecked)
		return m_bDeviceSupported;
	m_bDeviceChecked = true;
	m_bDeviceSupported = false;

	if (GetModuleHandleA("shaderapivk.dll"))
		return false;

	HMODULE hD3D9 = nullptr;
	wchar_t sModule[MAX_PATH]{}, sSystem[MAX_PATH]{};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(*reinterpret_cast<void**>(pDevice)), &hD3D9)
		|| !GetModuleFileNameW(hD3D9, sModule, MAX_PATH)
		|| !GetSystemDirectoryW(sSystem, MAX_PATH)
		|| _wcsnicmp(sModule, sSystem, wcslen(sSystem)) != 0)
		return false;

	IDirect3DSwapChain9* pMain = nullptr;
	D3DPRESENT_PARAMETERS tParams{};
	const bool bQueried = SUCCEEDED(pDevice->GetSwapChain(0, &pMain)) && pMain && SUCCEEDED(pMain->GetPresentParameters(&tParams));
	if (pMain)
		pMain->Release();
	return m_bDeviceSupported = bQueried && tParams.Windowed;
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
		if (m_pSwapChain)
		{
			m_pSwapChain->Release();
			m_pSwapChain = nullptr;
		}
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

	D3DPRESENT_PARAMETERS tParams{};
	tParams.Windowed = TRUE;
	tParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
	tParams.BackBufferFormat = D3DFMT_A8R8G8B8;
	tParams.BackBufferWidth = m_nWidth;
	tParams.BackBufferHeight = m_nHeight;
	tParams.BackBufferCount = 1;
	tParams.hDeviceWindow = m_hWindow;
	tParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	tParams.EnableAutoDepthStencil = FALSE;

	if (FAILED(pDevice->CreateAdditionalSwapChain(&tParams, &m_pSwapChain)) || !m_pSwapChain)
	{
		m_pSwapChain = nullptr;
		return false;
	}

	return true;
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

	if (!StartWindowThread() || !CheckDevice(pDevice))
	{
		SetVisible(false);
		return false;
	}

	if (!TrackGameWindow() || !CreateSwapChain(pDevice))
	{
		SetVisible(false);
		return false;
	}

	return m_bAvailable = true;
}

void CCaptureOverlay::Present(IDirect3DDevice9* pDevice, ImDrawData* pDrawData)
{
	if (!m_bAvailable || !m_pSwapChain || !pDevice || pDevice != m_pDevice)
		return;

	IDirect3DSurface9* pBackBuffer = nullptr;
	if (FAILED(m_pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) || !pBackBuffer)
		return;

	IDirect3DSurface9* pOldTarget = nullptr;
	IDirect3DSurface9* pOldDepth = nullptr;
	D3DVIEWPORT9 tOldViewport{};
	DWORD dwColorWrite = 0, dwScissor = 0;
	pDevice->GetRenderTarget(0, &pOldTarget);
	pDevice->GetDepthStencilSurface(&pOldDepth);
	pDevice->GetViewport(&tOldViewport);
	pDevice->GetRenderState(D3DRS_COLORWRITEENABLE, &dwColorWrite);
	pDevice->GetRenderState(D3DRS_SCISSORTESTENABLE, &dwScissor);

	pDevice->SetRenderTarget(0, pBackBuffer);
	pDevice->SetDepthStencilSurface(nullptr);
	pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
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
		m_pSwapChain->Release();
		m_pSwapChain = nullptr;
		SetVisible(false);
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
	if (m_pSwapChain)
	{
		m_pSwapChain->Release();
		m_pSwapChain = nullptr;
	}
	m_pDevice = nullptr;
	m_bDeviceChecked = false;
	m_bDeviceSupported = false;
	m_bAvailable = false;
	SetVisible(false);
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
	m_nWidth = m_nHeight = 0;
	m_tLastRect = {};
	m_bVisible = false;
	m_bThreadFailed = false;
}
#endif
