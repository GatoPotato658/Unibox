#include "Draw.h"

#include "../../SDK.h"
#include "../../Definitions/Interfaces.h"
#include "../../../Utils/Math/Math.h"
#include "../../../Utils/Timer/Timer.h"
#include "../../../Features/ImGui/Render.h"

MAKE_SIGNATURE(CHudBaseDeathNotice_GetIcon, "client.dll", "40 53 48 81 EC ? ? ? ? 48 8B DA", 0x0);
MAKE_SIGNATURE(RenderLine, "engine.dll", "48 89 5C 24 ? 48 89 74 24 ? 44 89 44 24", 0x0);
MAKE_SIGNATURE(RenderBox, "engine.dll", "48 83 EC ? 8B 84 24 ? ? ? ? 4D 8B D8", 0x0);
MAKE_SIGNATURE(RenderWireframeBox, "engine.dll", "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 49 8B F9", 0x0);
MAKE_SIGNATURE(RenderWireframeSweptBox, "engine.dll", "48 8B C4 48 89 58 ? 48 89 50 ? 48 89 48 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70", 0x0);
MAKE_SIGNATURE(RenderTriangle, "engine.dll", "48 83 EC ? 41 8B C1 44 88 4C 24", 0x0);
MAKE_SIGNATURE(RenderSphere, "engine.dll", "48 8B C4 44 89 48 ? F3 0F 11 48", 0x0);

void CDraw::Start(bool bBadFontCheck)
{
	I::MatSystemSurface->StartDrawing();
	I::MatSystemSurface->DisableClipping(true);

	if (bBadFontCheck)
	{
		static Timer tTimer = {};
		if (tTimer.Run(1.f))
		{
			if (!GetTextSize("", H::Fonts.GetFont(FONT_ESP)).y)
				H::Fonts.Reload();
		}
	}
}

void CDraw::End()
{
	I::MatSystemSurface->FinishDrawing();
}

void CDraw::UpdateScreenSize()
{
	I::MatSystemSurface->GetScreenSize(m_nScreenW, m_nScreenH);
}

void CDraw::UpdateW2SMatrix()
{
	CViewSetup tViewSetup;
	if (I::Client->GetPlayerView(tViewSetup))
	{
		static VMatrix mWorldToView, mViewToProjection, mWorldToPixels;
		I::RenderView->GetMatricesForView(tViewSetup, &mWorldToView, &mViewToProjection, &m_mWorldToProjection, &mWorldToPixels);
	}
}

void CDraw::UpdateKeyStrings()
{
	if (!F::Render.m_bLoaded)
		return;

	static HKL pLastLayout = nullptr;
	if (HKL pCurrLayout = GetKeyboardLayout(0); pCurrLayout != pLastLayout)
	{
		pLastLayout = pCurrLayout;
		for (short iKey = 1; iKey < 255; iKey++)
			U::KeyHandler.String(iKey, true);
	}
}

Vec2 CDraw::GetTextSize(const char* text, const Font_t& tFont)
{
	int w = 0, h = 0;
	I::MatSystemSurface->GetTextSize(tFont.m_dwFont, SDK::ConvertUtf8ToWide(text).c_str(), w, h);
	return { float(w), float(h) };
}

Vec2 CDraw::GetTextSize(const wchar_t* text, const Font_t& tFont)
{
	int w = 0, h = 0;
	I::MatSystemSurface->GetTextSize(tFont.m_dwFont, text, w, h);
	return { float(w), float(h) };
}

static wchar_t s_wstr[1024] = { '\0' };

static void AlignText(EAlign eAlign, const Vec2& vSize, int& x, int& y)
{
	switch (eAlign)
	{
	case ALIGN_TOPLEFT: break;
	case ALIGN_TOP: x -= vSize.x / 2; break;
	case ALIGN_TOPRIGHT: x -= vSize.x; break;
	case ALIGN_LEFT: y -= vSize.y / 2; break;
	case ALIGN_CENTER: x -= vSize.x / 2; y -= vSize.y / 2; break;
	case ALIGN_RIGHT: x -= vSize.x; y -= vSize.y / 2; break;
	case ALIGN_BOTTOMLEFT: y -= vSize.y; break;
	case ALIGN_BOTTOM: x -= vSize.x / 2; y -= vSize.y; break;
	case ALIGN_BOTTOMRIGHT: x -= vSize.x; y -= vSize.y; break;
	}
}

void CDraw::String(const Font_t& tFont, int x, int y, Color_t tColor, EAlign eAlign, const char* str)
{
	wsprintfW(s_wstr, L"%hs", str);
	String(tFont, x, y, tColor, eAlign, s_wstr);
}
void CDraw::String(const Font_t& tFont, int x, int y, Color_t tColor, EAlign eAlign, const wchar_t* wstr)
{
	const auto dwFont = tFont.m_dwFont;

	Vec2 vSize = GetTextSize(wstr, tFont);
	AlignText(eAlign, vSize, x, y);

	I::MatSystemSurface->DrawSetTextPos(x, y);
	I::MatSystemSurface->DrawSetTextFont(dwFont);
	I::MatSystemSurface->DrawSetTextColor(tColor);
	I::MatSystemSurface->DrawPrintText(wstr, int(wcslen(wstr)));
}
void CDraw::StringOutlined(const Font_t& tFont, int x, int y, Color_t tColor, Color_t tColorOut, EAlign eAlign, const char* str)
{
	wsprintfW(s_wstr, L"%hs", str);
	StringOutlined(tFont, x, y, tColor, tColorOut, eAlign, s_wstr);
}
void CDraw::StringOutlined(const Font_t& tFont, int x, int y, Color_t tColor, Color_t tColorOut, EAlign eAlign, const wchar_t* wstr)
{
	if (Vars::Menu::CheapText.Value)
		return String(tFont, x, y, tColor, eAlign, wstr);

	const auto dwFont = tFont.m_dwFont;

	Vec2 vSize = GetTextSize(wstr, tFont);
	AlignText(eAlign, vSize, x, y);
	
	tColorOut.a *= Math::RemapVal(tColorOut.Brightness(), 0, 255, 0.5f, 0.1f);
	if (tColorOut.a)
	{
		std::vector<std::pair<int, int>> vOutline = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 } };

		for (auto& [x2, y2] : vOutline)
		{
			I::MatSystemSurface->DrawSetTextPos(x + x2, y + y2);
			I::MatSystemSurface->DrawSetTextFont(dwFont);
			I::MatSystemSurface->DrawSetTextColor(tColorOut);
			I::MatSystemSurface->DrawPrintText(wstr, int(wcslen(wstr)));
		}
	}

	I::MatSystemSurface->DrawSetTextPos(x, y);
	I::MatSystemSurface->DrawSetTextFont(dwFont);
	I::MatSystemSurface->DrawSetTextColor(tColor);
	I::MatSystemSurface->DrawPrintText(wstr, int(wcslen(wstr)));
}

void CDraw::StringWithBackground(const Font_t& tFont, int x, int y, Color_t tColor, Color_t tColorBg, EAlign eAlign, const char* str)
{
	wsprintfW(s_wstr, L"%hs", str);
	StringWithBackground(tFont, x, y, tColor, tColorBg, eAlign, s_wstr);
}

void CDraw::StringWithBackground(const Font_t& tFont, int x, int y, Color_t tColor, Color_t tColorBg, EAlign eAlign, const wchar_t* wstr)
{
	const auto dwFont = tFont.m_dwFont;

	int w = 0, h = 0; I::MatSystemSurface->GetTextSize(dwFont, wstr, w, h);
	
	int padding = Scale(2, Scale_Round);
	
	int textX = x, textY = y;
	Vec2 vSize = { float(w), float(h) };
	AlignText(eAlign, vSize, textX, textY);
	
	if (tColorBg.a > 0)
	{
		FillRect(textX - padding, textY - padding, w + padding * 2, h + padding * 2, tColorBg);
	}
	
	I::MatSystemSurface->DrawSetTextPos(textX, textY);
	I::MatSystemSurface->DrawSetTextFont(dwFont);
	I::MatSystemSurface->DrawSetTextColor(tColor);
	I::MatSystemSurface->DrawPrintText(wstr, int(wcslen(wstr)));
}

void CDraw::Line(int x1, int y1, int x2, int y2, Color_t tColor)
{
	I::MatSystemSurface->DrawSetColor(tColor);
	I::MatSystemSurface->DrawLine(x1, y1, x2, y2);
}

void CDraw::FillPolygon(std::vector<Vertex_t> vVertices, Color_t tColor)
{
	static int iId = 0;
	if (!I::MatSystemSurface->IsTextureIDValid(iId))
		iId = I::MatSystemSurface->CreateNewTextureID();

	I::MatSystemSurface->DrawSetColor(tColor);
	I::MatSystemSurface->DrawSetTexture(iId);
	I::MatSystemSurface->DrawTexturedPolygon(int(vVertices.size()), vVertices.data());
}
void CDraw::LinePolygon(std::vector<Vertex_t> vVertices, Color_t tColor)
{
	static int iId = 0;
	if (!I::MatSystemSurface->IsTextureIDValid(iId))
		iId = I::MatSystemSurface->CreateNewTextureID();

	I::MatSystemSurface->DrawSetColor(tColor);
	I::MatSystemSurface->DrawSetTexture(iId);
	I::MatSystemSurface->DrawTexturedPolyLine(vVertices.data(), int(vVertices.size()));
}

void CDraw::FillRect(int x, int y, int w, int h, Color_t tColor)
{
	I::MatSystemSurface->DrawSetColor(tColor);
	I::MatSystemSurface->DrawFilledRect(x, y, x + w, y + h);
}
void CDraw::LineRect(int x, int y, int w, int h, Color_t tColor)
{
	I::MatSystemSurface->DrawSetColor(tColor);
	I::MatSystemSurface->DrawOutlinedRect(x, y, x + w, y + h);
}
CHudTexture* CDraw::GetIcon(const char* szIcon, int eIconFormat)
{
	return S::CHudBaseDeathNotice_GetIcon.Call<CHudTexture*>(nullptr, szIcon, eIconFormat);
}
int CDraw::CreateTextureFromArray(const unsigned char* rgba, int w, int h)
{
	const int nTextureIdOut = I::MatSystemSurface->CreateNewTextureID(true);
	I::MatSystemSurface->DrawSetTextureRGBAEx(nTextureIdOut, rgba, w, h, IMAGE_FORMAT_RGBA8888);
	return nTextureIdOut;
}
// Every engine mesh primitive queues functors into materialsystem's fixed 16MB per-frame call queue pool and
// vertex data into a fixed 4.3MB pool; CMatQueuedMesh::Draw dereferences the allocation unchecked, so overflowing either one crashes
constexpr int MAX_RENDER_DRAWS = 4096;
constexpr int MAX_RENDER_VERTICES = 32768;
constexpr int MAX_RENDER_PRIMITIVE_VERTICES = 8192;
constexpr int MAX_RENDER_PRIMITIVE_INDICES = 16384;

bool CDraw::HasRenderBudget()
{
	const int iFrame = I::GlobalVars ? I::GlobalVars->framecount : 0;
	if (iFrame != m_iRenderFrame)
	{
		m_iRenderFrame = iFrame;
		m_iRenderDraws = 0;
		m_iRenderVertices = 0;
	}

	return m_iRenderDraws < MAX_RENDER_DRAWS && m_iRenderVertices < MAX_RENDER_VERTICES;
}

bool CDraw::ConsumeRenderBudget(int iVertices)
{
	if (!HasRenderBudget() || iVertices > MAX_RENDER_VERTICES - m_iRenderVertices)
		return false;

	m_iRenderDraws++;
	m_iRenderVertices += iVertices;
	return true;
}

void CDraw::RenderLine(const Vec3& vStart, const Vec3& vEnd, Color_t tColor, bool bZBuffer)
{
	if (!tColor.a || !ConsumeRenderBudget(2))
		return;

	S::RenderLine.Call<void>(std::ref(vStart), std::ref(vEnd), tColor, bZBuffer);
}

void CDraw::RenderPath(const std::vector<Vec3>& vPath, Color_t tColor, bool bZBuffer, int iStyle, float flTime, int iSeparatorSpacing, float flSeparatorLength)
{
	if (!tColor.a || iStyle == Vars::Visuals::Path::StyleEnum::Off)
		return;

	bool bTimed = flTime < 0.f;
	for (size_t i = 1; i < vPath.size(); i++)
	{
		if (!HasRenderBudget())
			return;
		if (bTimed && vPath.size() - i > -flTime)
			continue;

		switch (iStyle)
		{
		case Vars::Visuals::Path::StyleEnum::Line:
		{
			RenderLine(vPath[i - 1], vPath[i], tColor, bZBuffer);
			break;
		}
		case Vars::Visuals::Path::StyleEnum::Separators:
		{
			RenderLine(vPath[i - 1], vPath[i], tColor, bZBuffer);
			if (!(i % iSeparatorSpacing))
			{
				const Vec3& vStart = vPath[i - 1];
				const Vec3& vEnd = vPath[i];

				Vec3 vDir = (vEnd - vStart).Normalized2D();
				vDir = Math::RotatePoint(vDir * flSeparatorLength, {}, { 0, 90, 0 });
				RenderLine(vEnd, vEnd + vDir, tColor, bZBuffer);
			}
			break;
		}
		case Vars::Visuals::Path::StyleEnum::Spaced:
		{
			if (!(i % 2))
				RenderLine(vPath[i - 1], vPath[i], tColor, bZBuffer);
			break;
		}
		case Vars::Visuals::Path::StyleEnum::Arrows:
		{
			if (!(i % 3))
			{
				const Vec3& vStart = vPath[i - 1];
				const Vec3& vEnd = vPath[i];

				if (!(vStart - vEnd).IsZero())
				{
					Vec3 vAngles = Math::VectorAngles(vEnd - vStart);
					Vec3 vForward, vRight, vUp; Math::AngleVectors(vAngles, &vForward, &vRight, &vUp);
					RenderLine(vEnd, vEnd - vForward * 5 + vRight * 5, tColor, bZBuffer);
					RenderLine(vEnd, vEnd - vForward * 5 - vRight * 5, tColor, bZBuffer);
				}
			}
			break;
		}
		case Vars::Visuals::Path::StyleEnum::Boxes:
		{
			RenderLine(vPath[i - 1], vPath[i], tColor, bZBuffer);
			if (!(i % iSeparatorSpacing))
				RenderWireframeBox(vPath[i], { -1, -1, -1 }, { 1, 1, 1 }, {}, tColor, bZBuffer);
			break;
		}
		}
	}
}

void CDraw::RenderBox(const Vec3& vOrigin, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vAngles, Color_t tColor, bool bZBuffer, bool bInsideOut)
{
	if (!tColor.a || !ConsumeRenderBudget(24))
		return;

	S::RenderBox.Call<void>(std::ref(vOrigin), std::ref(vAngles), std::ref(vMins), std::ref(vMaxs), tColor, bZBuffer, bInsideOut);
}

void CDraw::RenderWireframeBox(const Vec3& vOrigin, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vAngles, Color_t tColor, bool bZBuffer)
{
	if (!tColor.a || !ConsumeRenderBudget(24))
		return;

	S::RenderWireframeBox.Call<void>(std::ref(vOrigin), std::ref(vAngles), std::ref(vMins), std::ref(vMaxs), tColor, bZBuffer);
}

void CDraw::RenderWireframeSweptBox(const Vector& vStart, const Vector& vEnd, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vAngles, Color_t tColor, bool bZBuffer)
{
	if (!tColor.a || !ConsumeRenderBudget(56))
		return;

	S::RenderWireframeSweptBox.Call<void>(std::ref(vStart), std::ref(vEnd), std::ref(vAngles), std::ref(vMins), std::ref(vMaxs), tColor, bZBuffer);
}

void CDraw::RenderTriangle(const Vector& vPoint1, const Vector& vPoint2, const Vector& vPoint3, Color_t tColor, bool bZBuffer)
{
	if (!tColor.a || !ConsumeRenderBudget(3))
		return;

	S::RenderTriangle.Call<void>(std::ref(vPoint1), std::ref(vPoint2), std::ref(vPoint3), tColor, bZBuffer);
}

void CDraw::RenderSphere(const Vector& vCenter, float flRadius, int nTheta, int nPhi, Color_t tColor, IMaterial* pMaterial)
{
	if (!tColor.a || !pMaterial || nTheta < 1 || nPhi < 2)
		return;

	const long long llVertices = 2ll * nTheta * (nPhi - 1);
	const long long llIndices = (nTheta + 1ll) * (2ll * nPhi - 2ll);
	if (llVertices > MAX_RENDER_PRIMITIVE_VERTICES || llIndices > MAX_RENDER_PRIMITIVE_INDICES || !ConsumeRenderBudget(int(llVertices)))
		return;

	S::RenderSphere.Call<void>(std::ref(vCenter), flRadius, nTheta, nPhi, tColor, pMaterial);
}

void CDraw::RenderSphere(const Vector& vCenter, float flRadius, int nTheta, int nPhi, Color_t tColor, bool bZBuffer)
{
	if (!tColor.a)
		return;

	auto pMaterial = *reinterpret_cast<IMaterial**>(U::Memory.RelToAbs(bZBuffer ? S::VertexColor() : S::VertexColorIgnoreZ()));
	if (!pMaterial)
		return;

	RenderSphere(vCenter, flRadius, nTheta, nPhi, tColor, pMaterial);
}

void CDraw::RenderWireframeSphere(const Vector& vCenter, float flRadius, int nTheta, int nPhi, Color_t tColor, bool bZBuffer)
{
	if (!tColor.a)
		return;

	auto pMaterial = *reinterpret_cast<IMaterial**>(U::Memory.RelToAbs(bZBuffer ? S::Wireframe() : S::WireframeIgnoreZ()));
	if (!pMaterial)
		return;

	RenderSphere(vCenter, flRadius, nTheta, nPhi, tColor, pMaterial);
}
