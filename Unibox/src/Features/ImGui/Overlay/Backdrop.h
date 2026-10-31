#pragma once
#ifndef TEXTMODE
#include "../../../SDK/SDK.h"
#include <ImGui/imgui.h>
#include <d3d9.h>

struct GlassPane_t
{
	ImVec2 m_vMin;
	ImVec2 m_vMax;
	float m_flRounding;
	float m_flAlpha;
	float m_flClarity;
	float m_flScale;
	float m_flShadow;
	float m_flLight;
	ImVec4 m_tTint;
};

class CBackdrop
{
public:
	bool Pane(ImDrawList* pDrawList, const ImVec2& vMin, const ImVec2& vMax, float flRounding, const ImVec4& tTint, float flAlpha, float flClarity, float flShadow, float flLight);
	static GlassPane_t* GetPane(ImDrawList* pDrawList, const ImDrawCmd& tCmd);
	void Capture(IDirect3DDevice9* pDevice);
	void ReleaseTextures();
	void ReleaseDeviceObjects();

private:
	bool Create(IDirect3DDevice9* pDevice, const D3DSURFACE_DESC& tDesc);
	bool CreateShaders(IDirect3DDevice9* pDevice);
	bool Downsample(IDirect3DDevice9* pDevice, IDirect3DSurface9* pFrom);
	void Refresh(IDirect3DDevice9* pDevice);
	void Draw(const GlassPane_t& tPane);
	static void Callback(const ImDrawList* pDrawList, const ImDrawCmd* pCmd);

	static constexpr int s_iLevels = 5;
	static constexpr int s_iSoft = 1;
	static constexpr int s_iHeavy = 2;

	IDirect3DDevice9* m_pDevice = nullptr;
	IDirect3DTexture9* m_pResolve = nullptr;
	IDirect3DTexture9* m_aLevels[s_iLevels] = {};
	IDirect3DVertexShader9* m_pVertexShader = nullptr;
	IDirect3DPixelShader9* m_pPixelShader = nullptr;
	UINT m_uWidth = 0, m_uHeight = 0;
	int m_iPanes = 0;
	D3DFORMAT m_eFormat = D3DFMT_UNKNOWN;
	D3DMULTISAMPLE_TYPE m_eMultiSample = D3DMULTISAMPLE_NONE;
	bool m_bRequested = false;
	bool m_bReady = false;
	bool m_bFailed = false;
	bool m_bShaderFailed = false;
};

ADD_FEATURE(CBackdrop, Backdrop);
#endif
