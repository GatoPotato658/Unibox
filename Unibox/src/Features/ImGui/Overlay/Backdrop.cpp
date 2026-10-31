#ifndef TEXTMODE
#include "Backdrop.h"

#include <d3dcompiler.h>
#include <algorithm>

static constexpr const char s_sShader[] = R"(
float4 g_vScreen : register(c0);

struct VS_OUT
{
	float4 m_vPos : POSITION;
	float2 m_vClient : TEXCOORD0;
};

VS_OUT VS(float3 vPos : POSITION)
{
	VS_OUT tOut;
	tOut.m_vPos = float4((vPos.x - 0.5) * g_vScreen.x * 2.0 - 1.0, 1.0 - (vPos.y - 0.5) * g_vScreen.y * 2.0, 0.0, 1.0);
	tOut.m_vClient = vPos.xy;
	return tOut;
}

sampler2D g_sHeavy : register(s0);
sampler2D g_sSoft : register(s1);
float4 g_vRect : register(c0);
float4 g_vShape : register(c1);
float4 g_vTint : register(c2);
float4 g_vSample : register(c3);
float4 g_vLight : register(c4);
float4 g_vGrade : register(c5);
float4 g_vLighting : register(c6);

float RoundRect(float2 p)
{
	float2 q = abs(p) - g_vRect.zw + g_vShape.x;
	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - g_vShape.x;
}

float3 Backdrop(float2 vPixel, float flFrost)
{
	float4 vUV = float4(vPixel * g_vSample.xy, 0.0, 0.0);
	return lerp(tex2Dlod(g_sSoft, vUV).rgb, tex2Dlod(g_sHeavy, vUV).rgb, flFrost);
}

float4 PS(float2 vClient : TEXCOORD0) : COLOR
{
	float2 p = vClient - g_vRect.xy;
	float d = RoundRect(p);
	float flShape = saturate(0.5 - d / max(fwidth(d), 0.001));
	float flShadow = saturate(1.0 - RoundRect(p - float2(0.0, g_vGrade.y)) / g_vLight.w);
	flShadow *= flShadow * g_vGrade.x;
	clip(flShape + flShadow - 0.002);

	float2 e = float2(1.0, 0.0);
	float2 n = float2(RoundRect(p + e.xy) - RoundRect(p - e.xy), RoundRect(p + e.yx) - RoundRect(p - e.yx));
	n /= max(length(n), 0.0001);

	float flInner = max(-d, 0.0);
	float flEdge = saturate(1.0 - flInner / g_vShape.w);
	flEdge *= flEdge;
	float2 vSample = vClient + n * flEdge * g_vShape.z;
	float2 vChroma = n * flEdge * g_vSample.z;
	float flFrost = g_vGrade.w * (1.0 - flEdge);
	float3 vBack = float3(Backdrop(vSample + vChroma, flFrost).r, Backdrop(vSample, flFrost).g, Backdrop(vSample - vChroma, flFrost).b);
	vBack = lerp(dot(vBack, float3(0.2126, 0.7152, 0.0722)).xxx, vBack, g_vGrade.z);
	float3 vColor = lerp(vBack, g_vTint.rgb, g_vTint.a * (1.0 - flEdge * g_vSample.w));

	float flBevel = saturate(1.0 - flInner / g_vLight.z);
	flBevel *= flBevel;
	float3 N = normalize(float3(n * flBevel * 2.6, 1.0));
	float flFresnel = pow(saturate(1.0 - N.z), 2.5);
	float flFacing = dot(n, g_vLight.xy);
	float flFront = pow(saturate(flFacing), 1.2), flBack = pow(saturate(-flFacing), 1.2);
	float3 H = normalize(normalize(float3(g_vLight.xy * 0.9, 0.8)) + float3(0.0, 0.0, 1.0));
	vColor += (flFresnel * (0.16 + 0.42 * (flFront + flBack * 0.85)) + pow(saturate(dot(N, H)), 40.0) * 0.44 * flBevel) * g_vLighting.x;
	vColor -= flFresnel * (1.0 - flFront) * (1.0 - flBack) * 0.054 * g_vLighting.x;
	vColor = saturate(vColor);

	float flAlpha = flShape * g_vShape.y;
	float flOut = flAlpha + flShadow * g_vShape.y * (1.0 - flAlpha);
	return float4(vColor * flAlpha / max(flOut, 0.0001), flOut);
}
)";

bool CBackdrop::Pane(ImDrawList* pDrawList, const ImVec2& vMin, const ImVec2& vMax, float flRounding, const ImVec4& tTint, float flAlpha, float flClarity, float flShadow, float flLight)
{
	m_bRequested = true;
	if (!m_bReady || !m_pVertexShader || !m_pPixelShader)
		return false;

	GlassPane_t tPane = { vMin, vMax, flRounding, flAlpha, flClarity, H::Draw.Scale(), flShadow, flLight, tTint };
	pDrawList->AddCallback(Callback, &tPane, sizeof(tPane));
	return true;
}

GlassPane_t* CBackdrop::GetPane(ImDrawList* pDrawList, const ImDrawCmd& tCmd)
{
	if (tCmd.UserCallback != Callback || tCmd.UserCallbackDataOffset < 0)
		return nullptr;
	return reinterpret_cast<GlassPane_t*>(pDrawList->_CallbacksDataBuf.Data + tCmd.UserCallbackDataOffset);
}

void CBackdrop::Callback(const ImDrawList* pDrawList, const ImDrawCmd* pCmd)
{
	F::Backdrop.Draw(*reinterpret_cast<const GlassPane_t*>(pCmd->UserCallbackData));
}

bool CBackdrop::CreateShaders(IDirect3DDevice9* pDevice)
{
	HMODULE hCompiler = LoadLibraryW(L"d3dcompiler_47.dll");
	if (!hCompiler)
		return false;

	bool bSuccess = false;
	if (auto fCompile = reinterpret_cast<pD3DCompile>(GetProcAddress(hCompiler, "D3DCompile")))
	{
		ID3DBlob* pVertex = nullptr;
		ID3DBlob* pPixel = nullptr;
		bSuccess = SUCCEEDED(fCompile(s_sShader, sizeof(s_sShader) - 1, nullptr, nullptr, nullptr, "VS", "vs_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pVertex, nullptr))
			&& SUCCEEDED(fCompile(s_sShader, sizeof(s_sShader) - 1, nullptr, nullptr, nullptr, "PS", "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pPixel, nullptr))
			&& SUCCEEDED(pDevice->CreateVertexShader(static_cast<const DWORD*>(pVertex->GetBufferPointer()), &m_pVertexShader))
			&& SUCCEEDED(pDevice->CreatePixelShader(static_cast<const DWORD*>(pPixel->GetBufferPointer()), &m_pPixelShader));
		if (pVertex)
			pVertex->Release();
		if (pPixel)
			pPixel->Release();
	}
	FreeLibrary(hCompiler);
	return bSuccess;
}

bool CBackdrop::Create(IDirect3DDevice9* pDevice, const D3DSURFACE_DESC& tDesc)
{
	m_uWidth = tDesc.Width, m_uHeight = tDesc.Height;
	m_eFormat = tDesc.Format, m_eMultiSample = tDesc.MultiSampleType;

	// game alpha holds depth, sampling it would make the glass see-through
	const D3DFORMAT eFormat = tDesc.Format == D3DFMT_A8R8G8B8 ? D3DFMT_X8R8G8B8 : tDesc.Format;
	if (tDesc.MultiSampleType != D3DMULTISAMPLE_NONE
		&& FAILED(pDevice->CreateTexture(tDesc.Width, tDesc.Height, 1, D3DUSAGE_RENDERTARGET, tDesc.Format, D3DPOOL_DEFAULT, &m_pResolve, nullptr)))
		return false;

	UINT uWidth = tDesc.Width, uHeight = tDesc.Height;
	for (auto& pLevel : m_aLevels)
	{
		uWidth = std::max(uWidth / 2, 1u), uHeight = std::max(uHeight / 2, 1u);
		if (FAILED(pDevice->CreateTexture(uWidth, uHeight, 1, D3DUSAGE_RENDERTARGET, eFormat, D3DPOOL_DEFAULT, &pLevel, nullptr)))
			return false;
	}
	return true;
}

void CBackdrop::Capture(IDirect3DDevice9* pDevice)
{
	const bool bRequested = m_bRequested;
	m_bRequested = false;
	if (!bRequested || !pDevice)
		return;

	if (pDevice != m_pDevice)
	{
		ReleaseDeviceObjects();
		m_pDevice = pDevice;
	}
	if (m_bFailed)
		return;

	if (!m_pPixelShader && !m_bShaderFailed && !CreateShaders(pDevice))
	{
		m_bShaderFailed = true;
		ReleaseDeviceObjects();
		return;
	}
	if (m_bShaderFailed)
		return;

	IDirect3DSurface9* pBackBuffer = nullptr;
	if (FAILED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) || !pBackBuffer)
	{
		m_bReady = false;
		return;
	}

	D3DSURFACE_DESC tDesc = {};
	pBackBuffer->GetDesc(&tDesc);
	if (!m_aLevels[0] || tDesc.Width != m_uWidth || tDesc.Height != m_uHeight || tDesc.Format != m_eFormat || tDesc.MultiSampleType != m_eMultiSample)
	{
		ReleaseTextures();
		if (!Create(pDevice, tDesc))
		{
			ReleaseTextures();
			m_bFailed = true;
			pBackBuffer->Release();
			return;
		}
	}

	m_bReady = Downsample(pDevice, pBackBuffer);
	m_iPanes = 0;
	pBackBuffer->Release();
}

bool CBackdrop::Downsample(IDirect3DDevice9* pDevice, IDirect3DSurface9* pFrom)
{
	IDirect3DSurface9* pResolve = nullptr;
	IDirect3DSurface9* aSurfaces[s_iLevels] = {};
	bool bSuccess = !m_pResolve
		|| SUCCEEDED(m_pResolve->GetSurfaceLevel(0, &pResolve)) && SUCCEEDED(pDevice->StretchRect(pFrom, nullptr, pResolve, nullptr, D3DTEXF_NONE));
	IDirect3DSurface9* pSource = pResolve ? pResolve : pFrom;
	for (int i = 0; i < s_iLevels && bSuccess; i++)
	{
		bSuccess = SUCCEEDED(m_aLevels[i]->GetSurfaceLevel(0, &aSurfaces[i])) && SUCCEEDED(pDevice->StretchRect(pSource, nullptr, aSurfaces[i], nullptr, D3DTEXF_LINEAR));
		pSource = aSurfaces[i];
	}
	for (int i = s_iLevels - 2; i >= s_iHeavy && bSuccess; i--)
		bSuccess = SUCCEEDED(pDevice->StretchRect(aSurfaces[i + 1], nullptr, aSurfaces[i], nullptr, D3DTEXF_LINEAR));

	for (auto pSurface : aSurfaces)
	{
		if (pSurface)
			pSurface->Release();
	}
	if (pResolve)
		pResolve->Release();
	return bSuccess;
}

void CBackdrop::Refresh(IDirect3DDevice9* pDevice)
{
	IDirect3DSurface9* pTarget = nullptr;
	IDirect3DSurface9* pBackBuffer = nullptr;
	if (SUCCEEDED(pDevice->GetRenderTarget(0, &pTarget)) && SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) && pTarget == pBackBuffer)
		m_bReady = Downsample(pDevice, pTarget);
	if (pBackBuffer)
		pBackBuffer->Release();
	if (pTarget)
		pTarget->Release();
}

void CBackdrop::Draw(const GlassPane_t& tPane)
{
	IDirect3DDevice9* pDevice = m_pDevice;
	if (!m_bReady || !pDevice || !m_pVertexShader || !m_pPixelShader || tPane.m_flAlpha <= 0.f)
		return;

	const float flHalfX = (tPane.m_vMax.x - tPane.m_vMin.x) / 2, flHalfY = (tPane.m_vMax.y - tPane.m_vMin.y) / 2;
	if (flHalfX < 1.f || flHalfY < 1.f)
		return;

	// later panes sit on top of earlier ui, so they have to see it
	if (m_iPanes++ && tPane.m_tTint.w < 1.f)
	{
		Refresh(pDevice);
		if (!m_bReady)
			return;
	}

	const float flScale = tPane.m_flScale, flMinHalf = std::min(flHalfX, flHalfY);
	const float flLens = std::clamp(44.f * flScale / flMinHalf, 0.7f, 1.f);
	const float flShadow = (3.f + 6.f * tPane.m_flShadow) * flScale, flShadowY = (1.f + 2.f * tPane.m_flShadow) * flScale, flPad = flShadow + flShadowY + 2.f;

	D3DVIEWPORT9 tViewport = {};
	pDevice->GetViewport(&tViewport);
	const float aVertexConstants[4] = { 1.f / tViewport.Width, 1.f / tViewport.Height, 0.f, 0.f };
	const float aPixelConstants[7][4] = {
		{ tPane.m_vMin.x + flHalfX, tPane.m_vMin.y + flHalfY, flHalfX, flHalfY },
		{ std::min(tPane.m_flRounding, flMinHalf), tPane.m_flAlpha, 14.f * flScale * flLens * tPane.m_flLight, std::min(18.f * flScale, flMinHalf * 0.8f) },
		{ tPane.m_tTint.x, tPane.m_tTint.y, tPane.m_tTint.z, tPane.m_tTint.w },
		{ 1.f / m_uWidth, 1.f / m_uHeight, 1.5f * flScale * flLens * tPane.m_flLight, tPane.m_flClarity },
		{ -0.33f, -0.944f, std::min(8.f * flScale, flMinHalf * 0.6f), flShadow },
		{ tPane.m_flShadow, flShadowY, 1.25f, 0.85f },
		{ tPane.m_flLight, 0.f, 0.f, 0.f }
	};
	const float aVertices[4][3] = {
		{ tPane.m_vMin.x - flPad, tPane.m_vMin.y - flPad, 0.f },
		{ tPane.m_vMax.x + flPad, tPane.m_vMin.y - flPad, 0.f },
		{ tPane.m_vMin.x - flPad, tPane.m_vMax.y + flPad, 0.f },
		{ tPane.m_vMax.x + flPad, tPane.m_vMax.y + flPad, 0.f }
	};

	IDirect3DVertexBuffer9* pStream = nullptr;
	UINT uOffset = 0, uStride = 0;
	IDirect3DIndexBuffer9* pIndices = nullptr;
	DWORD uFVF = 0;
	RECT tScissor = {};
	pDevice->GetStreamSource(0, &pStream, &uOffset, &uStride);
	pDevice->GetIndices(&pIndices);
	pDevice->GetFVF(&uFVF);
	pDevice->GetScissorRect(&tScissor);

	const RECT tFull = { 0, 0, LONG(tViewport.Width), LONG(tViewport.Height) };
	pDevice->SetScissorRect(&tFull);
	pDevice->SetVertexShader(m_pVertexShader);
	pDevice->SetPixelShader(m_pPixelShader);
	pDevice->SetVertexShaderConstantF(0, aVertexConstants, 1);
	pDevice->SetPixelShaderConstantF(0, &aPixelConstants[0][0], 7);
	pDevice->SetTexture(0, m_aLevels[s_iHeavy]);
	pDevice->SetTexture(1, m_aLevels[s_iSoft]);
	for (DWORD i = 0; i < 2; i++)
	{
		pDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		pDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		pDevice->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		pDevice->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		pDevice->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}
	pDevice->SetFVF(D3DFVF_XYZ);
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, aVertices, sizeof(aVertices[0]));

	pDevice->SetTexture(1, nullptr);
	pDevice->SetVertexShader(nullptr);
	pDevice->SetPixelShader(nullptr);
	pDevice->SetFVF(uFVF);
	pDevice->SetStreamSource(0, pStream, uOffset, uStride);
	pDevice->SetIndices(pIndices);
	pDevice->SetScissorRect(&tScissor);
	if (pStream)
		pStream->Release();
	if (pIndices)
		pIndices->Release();
}

void CBackdrop::ReleaseTextures()
{
	if (m_pResolve)
	{
		m_pResolve->Release();
		m_pResolve = nullptr;
	}
	for (auto& pLevel : m_aLevels)
	{
		if (pLevel)
		{
			pLevel->Release();
			pLevel = nullptr;
		}
	}
	m_uWidth = m_uHeight = 0;
	m_eFormat = D3DFMT_UNKNOWN;
	m_eMultiSample = D3DMULTISAMPLE_NONE;
	m_bReady = m_bFailed = false;
}

void CBackdrop::ReleaseDeviceObjects()
{
	ReleaseTextures();
	if (m_pVertexShader)
	{
		m_pVertexShader->Release();
		m_pVertexShader = nullptr;
	}
	if (m_pPixelShader)
	{
		m_pPixelShader->Release();
		m_pPixelShader = nullptr;
	}
}
#endif
