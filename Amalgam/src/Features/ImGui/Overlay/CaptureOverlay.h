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

private:
	bool StartWindowThread();
	bool CheckDevice(IDirect3DDevice9* pDevice);
	bool CreateSwapChain(IDirect3DDevice9* pDevice);
	bool TrackGameWindow();
	void SetVisible(bool bVisible);

	static DWORD WINAPI WindowThread(LPVOID pParam);

	HANDLE m_hThread = nullptr;
	HANDLE m_hReady = nullptr;
	std::atomic<HWND> m_hWindow = nullptr;
	HWND m_hGameWindow = nullptr;
	IDirect3DSwapChain9* m_pSwapChain = nullptr;
	IDirect3DDevice9* m_pDevice = nullptr;

	int m_nWidth = 0, m_nHeight = 0;
	RECT m_tLastRect = {};
	bool m_bVisible = false;
	bool m_bThreadFailed = false;
	bool m_bDeviceChecked = false;
	bool m_bDeviceSupported = false;
	bool m_bAvailable = false;
};

ADD_FEATURE(CCaptureOverlay, CaptureOverlay);
#endif
