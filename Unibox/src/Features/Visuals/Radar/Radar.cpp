#include "Radar.h"

#include "../Groups/Groups.h"
#include "../ESP/ESP.h"
#include "../../ImGui/IndicatorPanel.h"
#include "../../ImGui/Menu/Menu.h"

bool CRadar::GetDrawPosition(CBaseEntity* pCenter, CBaseEntity* pEntity, float& x, float& y, float& z)
{
	const float flRange = Vars::Radar::Range.Value;
	const float flYaw = -Math::Deg2Rad(I::EngineClient->GetViewAngles().y);
	const float flSin = sinf(flYaw), flCos = cosf(flYaw);

	Vec3 vDelta = pCenter->GetAbsOrigin() - pEntity->GetAbsOrigin();
	Vec2 vPos = { vDelta.x * flSin + vDelta.y * flCos, vDelta.x * flCos - vDelta.y * flSin };

	switch (Vars::Radar::Style.Value)
	{
	case Vars::Radar::StyleEnum::Circle:
	{
		const float flDist = vDelta.Length2D();
		if (flDist > flRange)
		{
			if (!Vars::Radar::DrawOutOfRange.Value)
				return false;

			vPos *= flRange / flDist;
		}
		break;
	}
	case Vars::Radar::StyleEnum::Rectangle:
		if (fabs(vPos.x) > flRange || fabs(vPos.y) > flRange)
		{
			if (!Vars::Radar::DrawOutOfRange.Value)
				return false;

			Vec2 a = { -flRange / vPos.x, -flRange / vPos.y };
			Vec2 b = { flRange / vPos.x, flRange / vPos.y };
			Vec2 c = { std::min(a.x, b.x), std::min(a.y, b.y) };
			vPos *= fabsf(std::max(c.x, c.y));
		}
	}

	auto& tWindowBox = Vars::Radar::Window.Value;
	x = tWindowBox.x + vPos.x / flRange * tWindowBox.w / 2;
	y = tWindowBox.y + vPos.y / flRange * tWindowBox.w / 2 + tWindowBox.h / 2.f;
	z = vDelta.z;

	return true;
}

void CRadar::DrawBackground(ImDrawList* pDrawList)
{
	auto& tWindowBox = Vars::Radar::Window.Value;
	Color_t& tThemeBack = Vars::Menu::Theme::Background.Value;
	Color_t& tThemeAccent = Vars::Menu::Theme::Accent.Value;
	Color_t tColorBackground = { tThemeBack.r, tThemeBack.g, tThemeBack.b, byte(Vars::Radar::BackgroundAlpha.Value) };
	Color_t tColorAccent = { tThemeAccent.r, tThemeAccent.g, tThemeAccent.b, byte(Vars::Radar::LineAlpha.Value) };

	const ImVec2 vMin = { float(tWindowBox.x - tWindowBox.w / 2), float(tWindowBox.y) };
	const ImVec2 vMax = { float(tWindowBox.x + tWindowBox.w / 2), float(tWindowBox.y + tWindowBox.h) };
	const ImVec2 vCenter = { float(tWindowBox.x), float(tWindowBox.y + tWindowBox.h / 2.f) };

	switch (Vars::Radar::Style.Value)
	{
	case Vars::Radar::StyleEnum::Circle:
		pDrawList->AddCircleFilled(vCenter, tWindowBox.w / 2.f, ColorToU32(tColorBackground));
		pDrawList->AddCircle(vCenter, tWindowBox.w / 2.f, ColorToU32(tColorAccent));
		break;
	case Vars::Radar::StyleEnum::Rectangle:
		pDrawList->AddRectFilled(vMin, vMax, ColorToU32(tColorBackground), H::Draw.Scale(3));
		pDrawList->AddRect(vMin, vMax, ColorToU32(tColorAccent), H::Draw.Scale(3));
	}

	pDrawList->AddLine({ vMin.x, vCenter.y }, { vMax.x - 1, vCenter.y }, ColorToU32(tColorAccent));
	pDrawList->AddLine(vCenter, { vCenter.x, vMax.y - 1 }, ColorToU32(tColorAccent));
}

void CRadar::DrawPoints(ImDrawList* pDrawList, CBaseEntity* pCenter)
{
	const float flSize = float(Vars::Radar::Size.Value);
	auto& tWindowBox = Vars::Radar::Window.Value;

	pDrawList->PushClipRect({ float(tWindowBox.x - tWindowBox.w / 2), float(tWindowBox.y) }, { float(tWindowBox.x + tWindowBox.w / 2), float(tWindowBox.y + tWindowBox.h) }, true);

	for (auto& [pEntity, pGroup] : F::Groups.GetGroup(false))
	{
		if (!pGroup->m_bRadar)
			continue;

		float x, y, z;
		if (!GetDrawPosition(pCenter, pEntity, x, y, z))
			continue;

		const Color_t tColor = F::Groups.GetColor(pEntity, pGroup);
		const ImU32 uColor = ColorToU32(tColor);

		bool bDrew = false;
		if (pEntity->IsPlayer())
		{
			pDrawList->AddCircleFilled({ x, y }, flSize / 2, uColor);
#ifndef TEXTMODE
			if (IDirect3DTexture9* pTexture = GetIconTexture(GetClassIconTexture(pEntity->As<CTFPlayer>()->m_iClass())))
				pDrawList->AddImage(reinterpret_cast<ImTextureID>(pTexture), { x - flSize / 2, y - flSize / 2 }, { x + flSize / 2, y + flSize / 2 }, { 0, 0 }, { 1, 1 }, ColorToU32({ 255, 255, 255, 255 }));
#endif
			bDrew = true;
		}
		else if (pEntity->IsBuilding())
		{
			pDrawList->AddRectFilled({ x - flSize / 2, y - flSize / 2 }, { x + flSize / 2, y + flSize / 2 }, uColor);
			bDrew = true;
		}
		if (!bDrew)
			pDrawList->AddCircleFilled({ x, y }, flSize / 3, uColor);

		int iHealth = 0, iMaxHealth = 0;
		if (pEntity->IsPlayer())
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			iHealth = pPlayer->m_iHealth(), iMaxHealth = pPlayer->GetMaxHealth();
		}
		else if (pEntity->IsBuilding())
		{
			auto pBuilding = pEntity->As<CBaseObject>();
			iHealth = pBuilding->m_iHealth(), iMaxHealth = pBuilding->m_iMaxHealth();
		}

		if (Vars::Radar::HealthBar.Value && iMaxHealth > 0)
		{
			const float flRatio = std::clamp(float(iHealth) / iMaxHealth, 0.f, 1.f);
			const Color_t tHealth = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorGood.Value, flRatio);
			const float flBar = std::max(roundf(flSize / 8), 2.f);
			pDrawList->AddRectFilled({ x - flSize / 2 - flBar, y - flSize / 2 }, { x - flSize / 2, y + flSize / 2 }, ColorToU32({ 0, 0, 0, 255 }));
			pDrawList->AddRectFilled({ x - flSize / 2 - flBar, y + flSize / 2 - flSize * flRatio }, { x - flSize / 2, y + flSize / 2 }, ColorToU32(tHealth));
		}

		if (Vars::Radar::HeightIndicator.Value && std::abs(z) > 80.f)
		{
			const float flHalf = flSize / 2;
			const float flOffset = std::max(flSize / 4, 2.f) * (z < 0 ? -1 : 1);
			const float flY = y + (flHalf + 2) * (z < 0 ? -1 : 1);
			pDrawList->AddTriangleFilled({ x - flHalf, flY }, { x, flY + flOffset }, { x + flHalf, flY }, uColor);
		}
	}

	pDrawList->PopClipRect();
}

void CRadar::Draw(ImDrawList* pDrawList, CTFPlayer* pLocal)
{
	if (!pDrawList || !pLocal || !Vars::Radar::Enabled.Value
		|| I::MatSystemSurface->IsCursorVisible() && !I::EngineClient->IsPlayingDemo() && !F::Menu.m_bIsOpen)
		return;

	CBaseEntity* pCenter = pLocal;
	switch (pLocal->m_iObserverMode())
	{
	case OBS_MODE_FIRSTPERSON:
	case OBS_MODE_THIRDPERSON:
		if (auto pTarget = pLocal->m_hObserverTarget().Get())
			pCenter = pTarget;
	}

	DrawBackground(pDrawList);
	DrawPoints(pDrawList, pCenter);
}
