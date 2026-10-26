#pragma once
#include "../../SDK/SDK.h"
#include "Theme.h"
#include <ImGui/imgui_impl_dx9.h>
#include <ImGui/imgui.h>

class CRender : public CMenuTheme
{
public:
	void Render(IDirect3DDevice9* pDevice);
	void Initialize(IDirect3DDevice9* pDevice);
	void Reload();

	void LoadColors();
	void LoadFonts();
	void LoadStyle();

	int Cursor = 2;
	bool m_bLoaded = false;

	IDirect3DDevice9* GetDevice() const { return m_pDevice; }

private:
	IDirect3DDevice9* m_pDevice = nullptr;
};

ADD_FEATURE(CRender, Render);
