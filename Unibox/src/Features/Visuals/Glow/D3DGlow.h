#pragma once

#if defined(_WIN32) && !defined(TEXTMODE)
#include <d3d9.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include "GlowShaders.h"

class CD3DGlow
{
	template <class T>
	static void Release(T*& pObject)
	{
		if (pObject)
			pObject->Release();
		pObject = nullptr;
	}

	struct Target_t
	{
		IDirect3DTexture9* m_pTexture = nullptr;
		IDirect3DSurface9* m_pSurface = nullptr;

		void Reset()
		{
			CD3DGlow::Release(m_pSurface);
			CD3DGlow::Release(m_pTexture);
		}
	};

	struct State_t
	{
		IDirect3DDevice9* m_pDevice;
		IDirect3DStateBlock9* m_pBlock;
		IDirect3DSurface9* m_pTargets[4] = {};
		IDirect3DSurface9* m_pDepth = nullptr;
		D3DVIEWPORT9 m_Viewport = {};
		UINT m_nTargets;
		bool m_bCaptured = false;

		State_t(IDirect3DDevice9* pDevice, IDirect3DStateBlock9* pBlock, UINT nTargets)
			: m_pDevice(pDevice), m_pBlock(pBlock), m_nTargets(nTargets)
		{
			if (FAILED(m_pBlock->Capture()) || FAILED(m_pDevice->GetViewport(&m_Viewport)))
				return;
			for (UINT i = 0; i < m_nTargets; ++i)
				m_pDevice->GetRenderTarget(i, &m_pTargets[i]);
			m_pDevice->GetDepthStencilSurface(&m_pDepth);
			m_bCaptured = m_pTargets[0] != nullptr;
		}

		~State_t()
		{
			if (m_bCaptured)
			{
				m_pDevice->SetDepthStencilSurface(nullptr);
				for (UINT i = 1; i < m_nTargets; ++i)
					m_pDevice->SetRenderTarget(i, nullptr);
				m_pDevice->SetRenderTarget(0, m_pTargets[0]);
				for (UINT i = 1; i < m_nTargets; ++i)
					m_pDevice->SetRenderTarget(i, m_pTargets[i]);
				m_pDevice->SetDepthStencilSurface(m_pDepth);
				m_pBlock->Apply();
				m_pDevice->SetViewport(&m_Viewport);
			}
			for (auto& pTarget : m_pTargets)
				CD3DGlow::Release(pTarget);
			CD3DGlow::Release(m_pDepth);
		}
	};

	struct Vertex_t
	{
		float x, y, z, rhw, u, v;
	};

	IDirect3DDevice9* m_pDevice = nullptr;
	IDirect3DPixelShader9* m_pDecode = nullptr;
	IDirect3DPixelShader9* m_pDilate = nullptr;
	IDirect3DPixelShader9* m_pBlur = nullptr;
	IDirect3DPixelShader9* m_pCombine = nullptr;
	IDirect3DStateBlock9* m_pState = nullptr;
	IDirect3DSurface9* m_pDestination = nullptr;
	IDirect3DSurface9* m_pDestinationDepth = nullptr;
	D3DVIEWPORT9 m_DestinationViewport = {};
	Target_t m_Targets[7];
	UINT m_nRenderTargets = 1;
	int m_nWidth = 0;
	int m_nHeight = 0;
	bool m_bAttempted = false;
	std::atomic<bool> m_bReady = false;

	void EndCapture()
	{
		Release(m_pDestinationDepth);
		Release(m_pDestination);
	}

	bool EnsureTargets(int w, int h)
	{
		if (w == m_nWidth && h == m_nHeight && m_Targets[0].m_pTexture)
			return true;
		for (auto& tTarget : m_Targets)
			tTarget.Reset();
		m_nWidth = m_nHeight = 0;
		for (auto& tTarget : m_Targets)
		{
			if (FAILED(m_pDevice->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
				D3DPOOL_DEFAULT, &tTarget.m_pTexture, nullptr))
				|| FAILED(tTarget.m_pTexture->GetSurfaceLevel(0, &tTarget.m_pSurface)))
			{
				for (auto& tCleanup : m_Targets)
					tCleanup.Reset();
				m_bReady = false;
				return false;
			}
		}
		m_nWidth = w;
		m_nHeight = h;
		return true;
	}

	bool Draw(IDirect3DPixelShader9* pShader, int w, int h, float x = 0.f, float y = 0.f)
	{
		const Vertex_t aVertices[] =
		{
			{ x - 0.5f, y - 0.5f, 0.f, 1.f, 0.f, 0.f },
			{ x + w - 0.5f, y - 0.5f, 0.f, 1.f, 1.f, 0.f },
			{ x - 0.5f, y + h - 0.5f, 0.f, 1.f, 0.f, 1.f },
			{ x + w - 0.5f, y + h - 0.5f, 0.f, 1.f, 1.f, 1.f }
		};
		const bool bSuccess = SUCCEEDED(m_pDevice->SetPixelShader(pShader))
			&& SUCCEEDED(m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, aVertices, sizeof(Vertex_t)));
		if (!bSuccess)
			m_bReady = false;
		return bSuccess;
	}

	bool Filter(int iSource, int iTarget, IDirect3DPixelShader9* pShader, float x, float y, float radius)
	{
		m_pDevice->SetTexture(0, nullptr);
		m_pDevice->SetTexture(1, nullptr);
		if (FAILED(m_pDevice->SetRenderTarget(0, m_Targets[iTarget].m_pSurface)))
		{
			m_bReady = false;
			return false;
		}
		const D3DVIEWPORT9 tViewport = { 0, 0, UINT(m_nWidth), UINT(m_nHeight), 0.f, 1.f };
		m_pDevice->SetViewport(&tViewport);
		m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		m_pDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		m_pDevice->SetTexture(0, m_Targets[iSource].m_pTexture);
		const float aDirection[] = { x, y, radius, 0.f };
		m_pDevice->SetPixelShaderConstantF(0, aDirection, 1);
		return Draw(pShader, m_nWidth, m_nHeight);
	}

	bool Combine(int iSource, int iPrevious, const Glow_t& tGlow, float weight, float tint, bool bRing)
	{
		m_pDevice->SetTexture(0, nullptr);
		m_pDevice->SetTexture(1, nullptr);
		m_pDevice->SetDepthStencilSurface(nullptr);
		if (FAILED(m_pDevice->SetRenderTarget(0, m_pDestination))
			|| FAILED(m_pDevice->SetDepthStencilSurface(m_pDestinationDepth)))
		{
			m_bReady = false;
			return false;
		}
		m_pDevice->SetViewport(&m_DestinationViewport);
		m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		m_pDevice->SetRenderState(D3DRS_SRCBLEND, tGlow.Blend == 2 ? D3DBLEND_INVDESTCOLOR : D3DBLEND_ONE);
		m_pDevice->SetRenderState(D3DRS_DESTBLEND, tGlow.Blend == 1 ? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE);
		m_pDevice->SetRenderState(D3DRS_STENCILENABLE, m_pDestinationDepth != nullptr);
		m_pDevice->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
		m_pDevice->SetRenderState(D3DRS_STENCILREF, 0);
		m_pDevice->SetRenderState(D3DRS_STENCILMASK, 0x80);
		m_pDevice->SetRenderState(D3DRS_STENCILWRITEMASK, 0);
		m_pDevice->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
		m_pDevice->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
		m_pDevice->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
		m_pDevice->SetTexture(0, m_Targets[iSource].m_pTexture);
		m_pDevice->SetTexture(1, m_Targets[iPrevious].m_pTexture);
		const float aSettings[] = { tGlow.Intensity, tGlow.Opacity, weight, 0.f };
		const float aTint[] = { tGlow.OuterColor.r / 255.f, tGlow.OuterColor.g / 255.f,
			tGlow.OuterColor.b / 255.f, tint };
		const float aComposite[] = { bRing ? 1.f : 0.f,
			1.f + (tGlow.OuterColor.a / 255.f - 1.f) * tint, 0.f, 0.f };
		m_pDevice->SetPixelShaderConstantF(1, aSettings, 1);
		m_pDevice->SetPixelShaderConstantF(2, aTint, 1);
		m_pDevice->SetPixelShaderConstantF(3, aComposite, 1);
		return Draw(m_pCombine, m_nWidth, m_nHeight, float(m_DestinationViewport.X), float(m_DestinationViewport.Y));
	}

public:
	bool IsReady() const
	{
		return m_bReady.load();
	}

	void Initialize(IDirect3DDevice9* pDevice)
	{
		if (m_bAttempted || !pDevice)
			return;
		m_bAttempted = true;
		m_pDevice = pDevice;
		D3DCAPS9 tCaps = {};
		if (FAILED(pDevice->GetDeviceCaps(&tCaps)) || tCaps.PixelShaderVersion < D3DPS_VERSION(3, 0))
			return;
		m_nRenderTargets = (std::min)(tCaps.NumSimultaneousRTs, DWORD(4));
		if (FAILED(pDevice->CreatePixelShader(reinterpret_cast<const DWORD*>(g_GlowDecode), &m_pDecode))
			|| FAILED(pDevice->CreatePixelShader(reinterpret_cast<const DWORD*>(g_GlowDilate), &m_pDilate))
			|| FAILED(pDevice->CreatePixelShader(reinterpret_cast<const DWORD*>(g_GlowBlur), &m_pBlur))
			|| FAILED(pDevice->CreatePixelShader(reinterpret_cast<const DWORD*>(g_GlowCombine), &m_pCombine))
			|| FAILED(pDevice->CreateStateBlock(D3DSBT_ALL, &m_pState)))
		{
			Release(m_pDecode);
			Release(m_pDilate);
			Release(m_pBlur);
			Release(m_pCombine);
			Release(m_pState);
			return;
		}
		m_bReady = true;
	}

	void Begin(int w, int h)
	{
		EndCapture();
		if (!IsReady() || !EnsureTargets(w, h))
			return;
		if (FAILED(m_pDevice->GetRenderTarget(0, &m_pDestination))
			|| FAILED(m_pDevice->GetViewport(&m_DestinationViewport)))
		{
			EndCapture();
			m_bReady = false;
			return;
		}
		m_pDevice->GetDepthStencilSurface(&m_pDestinationDepth);
	}

	void Render(const Glow_t& tGlow)
	{
		if (!IsReady() || !m_pDestination)
		{
			EndCapture();
			return;
		}
		{
			State_t tState(m_pDevice, m_pState, m_nRenderTargets);
			if (tState.m_bCaptured)
			{
				const RECT tSourceRect = { 0, 0, m_nWidth, m_nHeight };
				if (SUCCEEDED(m_pDevice->StretchRect(tState.m_pTargets[0], &tSourceRect,
					m_Targets[6].m_pSurface, nullptr, D3DTEXF_NONE)))
				{
					DWORD nBlending = FALSE, nSeparateAlpha = FALSE, nAlphaSource = D3DBLEND_ONE;
					m_pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &nBlending);
					m_pDevice->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &nSeparateAlpha);
					m_pDevice->GetRenderState(nSeparateAlpha ? D3DRS_SRCBLENDALPHA : D3DRS_SRCBLEND, &nAlphaSource);
					const float flAlphaPower = nBlending && nAlphaSource == D3DBLEND_SRCALPHA ? 0.5f : 1.f;
					m_pDevice->SetDepthStencilSurface(nullptr);
					for (UINT i = 1; i < m_nRenderTargets; ++i)
						m_pDevice->SetRenderTarget(i, nullptr);
					m_pDevice->SetVertexShader(nullptr);
					m_pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
					m_pDevice->SetStreamSourceFreq(0, 1);
					m_pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
					m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
					m_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
					m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
					m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
					m_pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
					m_pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
					m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
					m_pDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
					m_pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
					m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED
						| D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
					m_pDevice->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
					m_pDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
					m_pDevice->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
					for (DWORD i = 0; i < 2; ++i)
					{
						m_pDevice->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
						m_pDevice->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
						m_pDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
						m_pDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
						m_pDevice->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
						m_pDevice->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE);
					}
					m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
					if (Filter(6, 0, m_pDecode, 0.f, 0.f, flAlphaPower))
						RenderLayers(tGlow);
				}
				else
					m_bReady = false;
			}
		}
		EndCapture();
	}

	void RenderLayers(const Glow_t& tGlow)
	{
		m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
		const int nRadius = std::clamp(tGlow.Stencil, 0, 16);
		const int nLayers = (std::min)(std::clamp(tGlow.Layers, 1, 8), (std::max)(nRadius, 1));
		int iPrevious = 0;
		for (int i = 0; i < nLayers && nRadius; ++i)
		{
			const int radius = (nRadius * (i + 1) + nLayers - 1) / nLayers;
			const int iCurrent = 2 + (i & 1);
			m_pDevice->SetDepthStencilSurface(nullptr);
			m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
			if (!Filter(0, 1, m_pDilate, 1.f / m_nWidth, 0.f, float(radius))
				|| !Filter(1, iCurrent, m_pDilate, 0.f, 1.f / m_nHeight, float(radius)))
				return;
			const float flPosition = nLayers > 1 ? float(i) / (nLayers - 1) : 1.f;
			const float flWeight = std::pow(1.f - float(i) / nLayers, tGlow.Falloff);
			m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0x7);
			if (!Combine(iCurrent, iPrevious, tGlow, flWeight, tGlow.CustomOuter ? flPosition : 0.f, true))
				return;
			iPrevious = iCurrent;
		}
		if (tGlow.Blur > 0.f && tGlow.Bloom > 0.f)
		{
			const float flRadius = std::clamp(tGlow.Blur, 0.f, 16.f) / 3.2307692308f;
			m_pDevice->SetDepthStencilSurface(nullptr);
			m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
			if (!Filter(iPrevious, 4, m_pBlur, flRadius / m_nWidth, 0.f, 0.f)
				|| !Filter(4, 5, m_pBlur, 0.f, flRadius / m_nHeight, 0.f))
				return;
			m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0x7);
			Combine(5, 0, tGlow, tGlow.Bloom, tGlow.CustomOuter ? 1.f : 0.f, true);
		}
	}

	void OnDeviceLost()
	{
		m_bReady = false;
		EndCapture();
		for (auto& tTarget : m_Targets)
			tTarget.Reset();
		Release(m_pState);
		Release(m_pDecode);
		Release(m_pDilate);
		Release(m_pBlur);
		Release(m_pCombine);
		m_nWidth = m_nHeight = 0;
		m_pDevice = nullptr;
		m_bAttempted = false;
	}
};
#endif
