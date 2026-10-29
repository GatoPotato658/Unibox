#pragma once
#ifndef TEXTMODE
#include "../../../SDK/SDK.h"
#include <ImGui/imgui.h>
#include <d3d9.h>
#include <atomic>

class CCaptureOverlay
{
public:
	bool Prepare(IDirect3DDevice9* pDevice);
	void Present(IDirect3DDevice9* pDevice, ImDrawData* pDrawData);
	void Hide();
	void ReleaseDeviceObjects();
	void Shutdown();
	bool Unsupported() const { return m_bUnsupported; }

private:
	enum class Mode_t
	{
		None,
		Alpha,
		Opaque
	};

	bool StartWindowThread();
	bool SelectMode(IDirect3DDevice9* pDevice);
	bool CreateSwapChain(IDirect3DDevice9* pDevice);
	bool TrackGameWindow();
	void ApplyWindowStyle();
	void ClearGameSurface();
	void ReleaseSwapChain();
	void SetVisible(bool bVisible);
	void NotifyUnavailable(const char* sReason);

	static DWORD WINAPI WindowThread(LPVOID pParam);

	HANDLE m_hThread = nullptr;
	HANDLE m_hReady = nullptr;
	std::atomic<HWND> m_hWindow = nullptr;
	HWND m_hGameWindow = nullptr;
	HWND m_hStyledWindow = nullptr;
	HWND m_hClearedWindow = nullptr;
	IDirect3DSwapChain9* m_pSwapChain = nullptr;
	IDirect3DDevice9* m_pDevice = nullptr;

	Mode_t m_eMode = Mode_t::None;
	Mode_t m_eStyledMode = Mode_t::None;
	D3DFORMAT m_Format = D3DFMT_UNKNOWN;
	int m_nWidth = 0, m_nHeight = 0;
	RECT m_tLastRect = {};
	bool m_bVisible = false;
	bool m_bThreadFailed = false;
	bool m_bSwapChainFailed = false;
	bool m_bUnsupported = false;
	bool m_bAvailable = false;
	bool m_bNotified = false;
};

ADD_FEATURE(CCaptureOverlay, CaptureOverlay);
#endif
