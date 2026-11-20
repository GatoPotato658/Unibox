#include "Radar.h"

#include "../Groups/Groups.h"
#include "../ESP/ESP.h"
#include "../../ImGui/IndicatorPanel.h"
#include "../../ImGui/Menu/Menu.h"

static inline Color_t ScaleAlpha(Color_t tColor, float flAlpha)
{
	tColor.a = byte(std::clamp(float(tColor.a) * flAlpha, 0.f, 255.f));
	return tColor;
}

static void DrawHealthBar(ImDrawList* pDrawList, float x, float y, float flSize, int iHealth, int iMaxHealth, float flAlpha)
{
	if (iMaxHealth <= 0)
		return;

	const float flHealth = float(iHealth);
	const float flMaxHealth = float(iMaxHealth);
	const float flRatio = std::clamp(flHealth / flMaxHealth, 0.f, 1.f);
	const float flOverheal = flHealth > flMaxHealth
		? std::clamp((flHealth - flMaxHealth) / std::max(floorf(flMaxHealth / 10.f) * 5.f, 1.f), 0.f, 1.f)
		: 0.f;

	const float flBarW = std::max(roundf(flSize / 8.f), 2.f);
	const float flBarX1 = x - flSize * 0.5f - flBarW - 2.f;
	const float flBarX2 = flBarX1 + flBarW;
	const float flBarY1 = y - flSize * 0.5f;
	const float flBarY2 = y + flSize * 0.5f;

	const Color_t tHealth = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorGood.Value, flRatio, LerpEnum::HSV);
	pDrawList->AddRectFilled({ flBarX1 - 1.f, flBarY1 - 1.f }, { flBarX2 + 1.f, flBarY2 + 1.f }, ColorToU32(ScaleAlpha({ 0, 0, 0, 220 }, flAlpha)));
	pDrawList->AddRectFilled({ flBarX1, flBarY2 - flSize * flRatio }, { flBarX2, flBarY2 }, ColorToU32(ScaleAlpha(tHealth, flAlpha)));

	if (flOverheal > 0.f)
		pDrawList->AddRectFilled({ flBarX1, flBarY2 - flSize * flOverheal }, { flBarX2, flBarY2 }, ColorToU32(ScaleAlpha(Vars::Colors::IndicatorMisc.Value, flAlpha)));
}

static void DrawHeightTriangle(ImDrawList* pDrawList, float x, float y, float flSize, float flDeltaZ, Color_t tColor)
{
	const float flHalf = std::max(flSize * 0.35f, 3.f);
	const float flHeight = std::max(flSize * 0.25f, 3.f);
	const bool bAbove = flDeltaZ > 0.f;
	const float flDir = bAbove ? -1.f : 1.f;
	const float flBaseY = y + (flSize * 0.5f + 2.f) * flDir;
	const float flTipY = flBaseY + flHeight * flDir;

	const ImVec2 v1 = { x - flHalf, flBaseY };
	const ImVec2 v2 = { x + flHalf, flBaseY };
	const ImVec2 v3 = { x, flTipY };

	const ImU32 uOutline = ColorToU32({ 0, 0, 0, byte(std::min<int>(tColor.a, 220)) });
	pDrawList->AddTriangleFilled(v1, v2, v3, ColorToU32(tColor));
	pDrawList->AddTriangle(v1, v2, v3, uOutline, 1.f);
}

static void DrawPlayerDirection(ImDrawList* pDrawList, float x, float y, float flRadius, float flRelativeYawRad, Color_t tColor, float flAlpha)
{
	const float flCos = cosf(flRelativeYawRad);
	const float flSin = sinf(flRelativeYawRad);
	const float flTipDist = flRadius + std::max(flRadius * 0.55f, 4.f);
	const float flBaseDist = flRadius - 0.5f;
	const float flHalfSpread = 0.45f;

	const ImVec2 vTip = { x + flSin * flTipDist, y - flCos * flTipDist };
	const ImVec2 vLeft = { x + sinf(flRelativeYawRad - flHalfSpread) * flBaseDist, y - cosf(flRelativeYawRad - flHalfSpread) * flBaseDist };
	const ImVec2 vRight = { x + sinf(flRelativeYawRad + flHalfSpread) * flBaseDist, y - cosf(flRelativeYawRad + flHalfSpread) * flBaseDist };

	pDrawList->AddTriangleFilled(vLeft, vRight, vTip, ColorToU32(ScaleAlpha(tColor, flAlpha)));
	pDrawList->AddTriangle(vLeft, vRight, vTip, ColorToU32(ScaleAlpha({ 0, 0, 0, 220 }, flAlpha)), 1.f);
}

void CRadar::Store(CTFPlayer* pLocal)
{
	if (!pLocal || !Vars::Radar::Enabled.Value)
	{
		m_tCache.Set({});
		return;
	}

	CBaseEntity* pCenter = pLocal;
	switch (pLocal->m_iObserverMode())
	{
	case OBS_MODE_FIRSTPERSON:
	case OBS_MODE_THIRDPERSON:
		if (auto pTarget = pLocal->m_hObserverTarget().Get())
			pCenter = pTarget;
		break;
	}

	RadarCache_t tCache = {};
	tCache.m_bValid = true;
	tCache.m_vCenterOrigin = pCenter->GetAbsOrigin();

	const int iFOV = pLocal->m_iFOV();
	tCache.m_flLocalFOV = std::clamp(
		iFOV > 0 ? float(iFOV) : (Vars::Visuals::UI::FieldOfView.Value > 0.f ? Vars::Visuals::UI::FieldOfView.Value : 90.f),
		20.f, 160.f
	);

	for (auto& [pEntity, pGroup] : F::Groups.GetGroup(false))
	{
		if (!pGroup->m_bRadar || pEntity == pCenter || pEntity == pLocal)
			continue;

		RadarEntity_t tEntity = {};
		tEntity.m_vOrigin = pEntity->GetAbsOrigin();
		tEntity.m_tColor = F::Groups.GetColor(pEntity, pGroup);
		tEntity.m_bDormant = pEntity->IsDormant();

		if (pEntity->IsPlayer())
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			tEntity.m_eType = ERadarEntityType::Player;
			tEntity.m_flYaw = pPlayer->GetEyeAngles().y;
			tEntity.m_iClass = pPlayer->m_iClass();
			tEntity.m_iHealth = pPlayer->m_iHealth();
			tEntity.m_iMaxHealth = pPlayer->GetMaxHealth();
			tEntity.m_bCloaked = pPlayer->IsInvisible();
		}
		else if (pEntity->IsBuilding())
		{
			auto pBuilding = pEntity->As<CBaseObject>();
			tEntity.m_eType = ERadarEntityType::Building;
			tEntity.m_iHealth = pBuilding->m_iHealth();
			tEntity.m_iMaxHealth = pBuilding->m_iMaxHealth();
			switch (pEntity->GetClassID())
			{
			case ETFClassID::CObjectSentrygun: tEntity.m_iClass = 0; break;
			case ETFClassID::CObjectDispenser: tEntity.m_iClass = 1; break;
			case ETFClassID::CObjectTeleporter: tEntity.m_iClass = 2; break;
			default: tEntity.m_iClass = -1; break;
			}
		}
		else if (pEntity->IsProjectile())
		{
			tEntity.m_eType = ERadarEntityType::Projectile;
		}
		else if (pEntity->GetClassID() == ETFClassID::CCaptureFlag)
		{
			tEntity.m_eType = ERadarEntityType::Objective;
		}
		else if (pEntity->IsNPC())
		{
			tEntity.m_eType = ERadarEntityType::NPC;
		}
		else if (pEntity->GetClassID() == ETFClassID::CBaseAnimating)
		{
			auto uHash = H::Entities.GetModel(pEntity->entindex());
			if (H::Entities.IsHealth(uHash))
				tEntity.m_eType = ERadarEntityType::Health;
			else if (H::Entities.IsAmmo(uHash))
				tEntity.m_eType = ERadarEntityType::Ammo;
			else
				tEntity.m_eType = ERadarEntityType::Other;
		}
		else
		{
			tEntity.m_eType = ERadarEntityType::Other;
		}

		tCache.m_vEntities.push_back(tEntity);
	}

	m_tCache.Set(std::move(tCache));
}

bool CRadar::GetDrawPosition(const Vec3& vCenterOrigin, const Vec3& vEntityOrigin, float flViewYaw, float flPadding, float& x, float& y, float& z)
{
	const float flRange = std::max(float(Vars::Radar::Range.Value), 1.f);
	const float flYawRad = Math::Deg2Rad(flViewYaw);
	const float flCos = cosf(flYawRad);
	const float flSin = sinf(flYawRad);

	const Vec3 vDelta = vEntityOrigin - vCenterOrigin;
	const float flForward = vDelta.x * flCos + vDelta.y * flSin;
	const float flRight = vDelta.x * flSin - vDelta.y * flCos;
	Vec2 vPos = { flRight, -flForward };

	auto& tWindowBox = Vars::Radar::Window.Value;
	const float flHalfW = tWindowBox.w * 0.5f;
	const float flHalfH = tWindowBox.h * 0.5f;
	const ImVec2 vCenter = { float(tWindowBox.x), float(tWindowBox.y) + flHalfH };

	switch (Vars::Radar::Style.Value)
	{
	case Vars::Radar::StyleEnum::Circle:
	{
		const float flDist = vPos.Length();
		if (flDist > flRange)
		{
			if (!Vars::Radar::DrawOutOfRange.Value)
				return false;
			vPos *= (flRange / flDist);
		}
		const float flDrawRadius = std::max(std::min(flHalfW, flHalfH) - flPadding, 1.f);
		x = vCenter.x + (vPos.x / flRange) * flDrawRadius;
		y = vCenter.y + (vPos.y / flRange) * flDrawRadius;
		break;
	}
	case Vars::Radar::StyleEnum::Rectangle:
	{
		const float flMaxAxis = std::max(fabsf(vPos.x), fabsf(vPos.y));
		if (flMaxAxis > flRange)
		{
			if (!Vars::Radar::DrawOutOfRange.Value)
				return false;
			vPos *= (flRange / flMaxAxis);
		}
		const float flDrawHalfW = std::max(flHalfW - flPadding, 1.f);
		const float flDrawHalfH = std::max(flHalfH - flPadding, 1.f);
		x = vCenter.x + (vPos.x / flRange) * flDrawHalfW;
		y = vCenter.y + (vPos.y / flRange) * flDrawHalfH;
		break;
	}
	}

	z = vDelta.z;
	return true;
}

void CRadar::DrawBackground(ImDrawList* pDrawList, float flLocalFOV)
{
	auto& tWindowBox = Vars::Radar::Window.Value;
	const Color_t& tThemeBack = Vars::Menu::Theme::Background.Value;
	const Color_t& tThemeAccent = Vars::Menu::Theme::Accent.Value;
	const Color_t tColorBackground = { tThemeBack.r, tThemeBack.g, tThemeBack.b, byte(Vars::Radar::BackgroundAlpha.Value) };
	const Color_t tColorAccent = { tThemeAccent.r, tThemeAccent.g, tThemeAccent.b, byte(Vars::Radar::LineAlpha.Value) };
	const Color_t tColorGrid = ScaleAlpha(tColorAccent, 0.35f);
	const Color_t tColorCone = ScaleAlpha(tColorAccent, 0.14f);

	const float flHalfW = tWindowBox.w * 0.5f;
	const float flHalfH = tWindowBox.h * 0.5f;
	const ImVec2 vMin = { float(tWindowBox.x) - flHalfW, float(tWindowBox.y) };
	const ImVec2 vMax = { float(tWindowBox.x) + flHalfW, float(tWindowBox.y) + float(tWindowBox.h) };
	const ImVec2 vCenter = { float(tWindowBox.x), float(tWindowBox.y) + flHalfH };
	const float flRadius = std::min(flHalfW, flHalfH);
	const float flRounding = H::Draw.Scale(3);

	switch (Vars::Radar::Style.Value)
	{
	case Vars::Radar::StyleEnum::Circle:
		pDrawList->AddCircleFilled(vCenter, flRadius, ColorToU32(tColorBackground), 64);
		pDrawList->AddCircle(vCenter, flRadius * 0.5f, ColorToU32(tColorGrid), 48, 1.f);
		break;
	case Vars::Radar::StyleEnum::Rectangle:
		pDrawList->AddRectFilled(vMin, vMax, ColorToU32(tColorBackground), flRounding);
		break;
	}

	if (Vars::Radar::Style.Value == Vars::Radar::StyleEnum::Circle)
	{
		pDrawList->AddLine({ vCenter.x - flRadius, vCenter.y }, { vCenter.x + flRadius, vCenter.y }, ColorToU32(tColorGrid));
		pDrawList->AddLine({ vCenter.x, vCenter.y - flRadius }, { vCenter.x, vCenter.y + flRadius }, ColorToU32(tColorGrid));
	}
	else
	{
		pDrawList->AddLine({ vMin.x, vCenter.y }, { vMax.x, vCenter.y }, ColorToU32(tColorGrid));
		pDrawList->AddLine({ vCenter.x, vMin.y }, { vCenter.x, vMax.y }, ColorToU32(tColorGrid));
	}

	if (Vars::Radar::ViewCone.Value && Vars::Radar::LineAlpha.Value > 0)
	{
		const float flHalfFovRad = Math::Deg2Rad(flLocalFOV * 0.5f);
		const float flConeLen = Vars::Radar::Style.Value == Vars::Radar::StyleEnum::Circle
			? flRadius
			: std::min(flHalfW / std::max(fabsf(sinf(flHalfFovRad)), 0.001f), flHalfH / std::max(fabsf(cosf(flHalfFovRad)), 0.001f));

		const ImVec2 vLeft = { vCenter.x - sinf(flHalfFovRad) * flConeLen, vCenter.y - cosf(flHalfFovRad) * flConeLen };
		const ImVec2 vRight = { vCenter.x + sinf(flHalfFovRad) * flConeLen, vCenter.y - cosf(flHalfFovRad) * flConeLen };

		pDrawList->PushClipRect(vMin, vMax, true);
		pDrawList->AddTriangleFilled(vCenter, vLeft, vRight, ColorToU32(tColorCone));
		pDrawList->AddLine(vCenter, vLeft, ColorToU32(ScaleAlpha(tColorAccent, 0.55f)));
		pDrawList->AddLine(vCenter, vRight, ColorToU32(ScaleAlpha(tColorAccent, 0.55f)));
		pDrawList->PopClipRect();
	}

	switch (Vars::Radar::Style.Value)
	{
	case Vars::Radar::StyleEnum::Circle:
		pDrawList->AddCircle(vCenter, flRadius, ColorToU32(tColorAccent), 64, 1.5f);
		break;
	case Vars::Radar::StyleEnum::Rectangle:
		pDrawList->AddRect(vMin, vMax, ColorToU32(tColorAccent), flRounding, 0, 1.5f);
		break;
	}

	const float flCenterSize = std::max(H::Draw.Scale(3.5f), 3.f);
	const ImVec2 vLocal1 = { vCenter.x, vCenter.y - flCenterSize * 1.2f };
	const ImVec2 vLocal2 = { vCenter.x + flCenterSize, vCenter.y + flCenterSize * 0.8f };
	const ImVec2 vLocal3 = { vCenter.x - flCenterSize, vCenter.y + flCenterSize * 0.8f };
	pDrawList->AddTriangleFilled(vLocal1, vLocal2, vLocal3, ColorToU32(Vars::Menu::Theme::Active.Value));
	pDrawList->AddTriangle(vLocal1, vLocal2, vLocal3, ColorToU32({ 0, 0, 0, 220 }), 1.f);
}

void CRadar::DrawPoints(ImDrawList* pDrawList, const RadarCache_t& tCache, float flViewYaw)
{
	const float flSize = float(Vars::Radar::Size.Value);
	const float flRadius = flSize * 0.5f;
	const float flPadding = flRadius + 2.f;
	auto& tWindowBox = Vars::Radar::Window.Value;

	const ImVec2 vMin = { float(tWindowBox.x) - tWindowBox.w * 0.5f, float(tWindowBox.y) };
	const ImVec2 vMax = { float(tWindowBox.x) + tWindowBox.w * 0.5f, float(tWindowBox.y) + float(tWindowBox.h) };
	pDrawList->PushClipRect(vMin, vMax, true);

	for (const auto& tEntity : tCache.m_vEntities)
	{
		float x, y, z;
		if (!GetDrawPosition(tCache.m_vCenterOrigin, tEntity.m_vOrigin, flViewYaw, flPadding, x, y, z))
			continue;

		const float flAlpha = tEntity.m_bDormant ? 0.55f : (tEntity.m_bCloaked ? 0.65f : 1.f);
		const Color_t tColor = ScaleAlpha(tEntity.m_tColor, flAlpha);
		const ImU32 uColor = ColorToU32(tColor);
		const ImU32 uOutline = ColorToU32(ScaleAlpha({ 0, 0, 0, 220 }, flAlpha));

		switch (tEntity.m_eType)
		{
		case ERadarEntityType::Player:
		{
			if (Vars::Radar::PlayerDirection.Value)
			{
				const float flRelYawRad = Math::Deg2Rad(flViewYaw - tEntity.m_flYaw);
				DrawPlayerDirection(pDrawList, x, y, flRadius, flRelYawRad, tEntity.m_tColor, flAlpha);
			}

			pDrawList->AddCircleFilled({ x, y }, flRadius, uColor, 24);
			pDrawList->AddCircle({ x, y }, flRadius, uOutline, 24, 1.f);
#ifndef TEXTMODE
			if (IDirect3DTexture9* pTexture = GetIconTexture(GetClassIconTexture(tEntity.m_iClass)))
			{
				const float flIconPad = std::max(flSize * 0.1f, 1.f);
				const ImU32 uIconTint = ColorToU32(ScaleAlpha({ 255, 255, 255, 255 }, flAlpha));
				pDrawList->AddImage(reinterpret_cast<ImTextureID>(pTexture),
					{ x - flRadius + flIconPad, y - flRadius + flIconPad },
					{ x + flRadius - flIconPad, y + flRadius - flIconPad },
					{ 0, 0 }, { 1, 1 }, uIconTint);
			}
#endif
			if (Vars::Radar::HealthBar.Value && tEntity.m_iMaxHealth > 0)
				DrawHealthBar(pDrawList, x, y, flSize, tEntity.m_iHealth, tEntity.m_iMaxHealth, flAlpha);
			if (Vars::Radar::HeightIndicator.Value && std::abs(z) > 80.f)
				DrawHeightTriangle(pDrawList, x, y, flSize, z, tColor);
			break;
		}
		case ERadarEntityType::Building:
		{
			const float flHalf = std::max(flRadius * 0.85f, 3.5f);
			pDrawList->AddRectFilled({ x - flHalf, y - flHalf }, { x + flHalf, y + flHalf }, uColor, 2.f);
			pDrawList->AddRect({ x - flHalf, y - flHalf }, { x + flHalf, y + flHalf }, uOutline, 2.f, 0, 1.f);

			const char* sTag = tEntity.m_iClass == 0 ? "S" : (tEntity.m_iClass == 1 ? "D" : (tEntity.m_iClass == 2 ? "T" : "B"));
			const ImVec2 vTextSize = ImGui::CalcTextSize(sTag);
			const ImVec2 vTextPos = { x - vTextSize.x * 0.5f, y - vTextSize.y * 0.5f };
			pDrawList->AddText({ vTextPos.x + 1.f, vTextPos.y + 1.f }, uOutline, sTag);
			pDrawList->AddText(vTextPos, ColorToU32(ScaleAlpha({ 255, 255, 255, 255 }, flAlpha)), sTag);

			if (Vars::Radar::HealthBar.Value && tEntity.m_iMaxHealth > 0)
				DrawHealthBar(pDrawList, x, y, flHalf * 2.f, tEntity.m_iHealth, tEntity.m_iMaxHealth, flAlpha);
			if (Vars::Radar::HeightIndicator.Value && std::abs(z) > 80.f)
				DrawHeightTriangle(pDrawList, x, y, flHalf * 2.f, z, tColor);
			break;
		}
		case ERadarEntityType::Health:
		{
			const float flArm = std::max(flRadius * 0.55f, 3.f);
			const float flThick = std::max(flRadius * 0.25f, 1.5f);
			pDrawList->AddRectFilled({ x - flThick - 1.f, y - flArm - 1.f }, { x + flThick + 1.f, y + flArm + 1.f }, uOutline);
			pDrawList->AddRectFilled({ x - flArm - 1.f, y - flThick - 1.f }, { x + flArm + 1.f, y + flThick + 1.f }, uOutline);
			pDrawList->AddRectFilled({ x - flThick, y - flArm }, { x + flThick, y + flArm }, uColor);
			pDrawList->AddRectFilled({ x - flArm, y - flThick }, { x + flArm, y + flThick }, uColor);
			break;
		}
		case ERadarEntityType::Ammo:
		{
			const float flBox = std::max(flRadius * 0.45f, 2.5f);
			pDrawList->AddRectFilled({ x - flBox, y - flBox }, { x + flBox, y + flBox }, uColor);
			pDrawList->AddRect({ x - flBox, y - flBox }, { x + flBox, y + flBox }, uOutline, 0.f, 0, 1.f);
			break;
		}
		case ERadarEntityType::Objective:
		{
			const float flDia = std::max(flRadius * 0.7f, 4.f);
			const ImVec2 aPoints[4] = { { x, y - flDia }, { x + flDia, y }, { x, y + flDia }, { x - flDia, y } };
			pDrawList->AddConvexPolyFilled(aPoints, 4, uColor);
			pDrawList->AddPolyline(aPoints, 4, uOutline, ImDrawFlags_Closed, 1.2f);
			break;
		}
		case ERadarEntityType::Projectile:
		{
			const float flDia = std::max(flRadius * 0.4f, 2.f);
			const ImVec2 aPoints[4] = { { x, y - flDia }, { x + flDia, y }, { x, y + flDia }, { x - flDia, y } };
			pDrawList->AddConvexPolyFilled(aPoints, 4, uColor);
			pDrawList->AddPolyline(aPoints, 4, uOutline, ImDrawFlags_Closed, 1.f);
			break;
		}
		default:
		{
			const float flDot = std::max(flRadius * 0.45f, 2.5f);
			pDrawList->AddCircleFilled({ x, y }, flDot, uColor, 16);
			pDrawList->AddCircle({ x, y }, flDot, uOutline, 16, 1.f);
			break;
		}
		}
	}

	pDrawList->PopClipRect();
}

void CRadar::Draw(ImDrawList* pDrawList, CTFPlayer* pLocal)
{
	if (!pDrawList || !Vars::Radar::Enabled.Value
		|| I::MatSystemSurface->IsCursorVisible() && !I::EngineClient->IsPlayingDemo() && !F::Menu.m_bIsOpen)
		return;

	const auto pCache = m_tCache.Get();
	if (!pCache || !pCache->m_bValid)
		return;

	const float flViewYaw = I::EngineClient->GetViewAngles().y;
	DrawBackground(pDrawList, pCache->m_flLocalFOV);
	DrawPoints(pDrawList, *pCache, flViewYaw);
}
