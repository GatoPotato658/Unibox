#pragma once
#include "../../SDK/SDK.h"
#include "Theme.h"
#include <ImGui/imgui_impl_dx9.h>
#include <ImGui/imgui.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <wrl/client.h>

class CRender : public CMenuTheme
{
public:
	enum class StartupStage { Config, Features, MenuAssets };

	void Render(IDirect3DDevice9* pDevice);
	void Unload();
	void Reload();
	void SetStartupStage(StartupStage eStage) { m_eStartupStage.store(eStage); }
	bool IsConfigStage() const { return m_eStartupStage.load() == StartupStage::Config; }
	void InvalidateDeviceObjects(bool bDeviceChanged = false);
	void OnDeviceLost();
	void OnDeviceReset();

	void LoadColors();
	void LoadFonts();
	void LoadStyle();

	int Cursor = 2;
	bool m_bLoaded = false;

	IDirect3DDevice9* GetDevice() const { return m_pDevice; }

private:
	void Initialize(IDirect3DDevice9* pDevice);
	void DrawStartup(const char* sStatus);
	void DrawContent(bool bDrawVisuals);

	std::mutex m_tFrameMutex;
	IDirect3DDevice9* m_pDevice = nullptr;
	std::atomic<StartupStage> m_eStartupStage = StartupStage::Config;
	std::chrono::steady_clock::time_point m_tCatalogueWait = {};
	static constexpr int BLUR_LEVELS = 4;
	Microsoft::WRL::ComPtr<IDirect3DTexture9> m_aStartupBlur[BLUR_LEVELS];
	Microsoft::WRL::ComPtr<IDirect3DSurface9> m_pStartupResolve;
	D3DSURFACE_DESC m_tStartupSource = {};
	bool m_bStartupFrame = false;
	bool m_bStartupComplete = false;
};

ADD_FEATURE(CRender, Render);
