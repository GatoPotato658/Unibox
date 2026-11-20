#include "Radar.h"

#ifndef TEXTMODE

#include "../../Binds/Binds.h"
#include "../ESP/ESP.h"
#include "../../ImGui/Menu/BindWidgets.h"
#include "../../NavBot/Map.h"

#include <filesystem>

static void DrawArrow(ImDrawList* pDrawList, const ImVec2& vPos, float flYaw, float flSize, ImU32 uFill)
{
	const ImVec2 vForward = { sinf(flYaw), -cosf(flYaw) };
	const ImVec2 vRight = { -vForward.y, vForward.x };
	const ImVec2 aPoints[] = {
		vPos + vForward * flSize,
		vPos - vForward * flSize * 0.35f + vRight * flSize * 0.55f,
		vPos - vForward * flSize * 0.15f,
		vPos - vForward * flSize * 0.35f - vRight * flSize * 0.55f
	};
	pDrawList->AddConvexPolyFilled(aPoints, 4, uFill);
	pDrawList->AddPolyline(aPoints, 4, IM_COL32(14, 15, 17, 230), ImDrawFlags_Closed, 1.2f);
}

void CRadar::LoadFloor()
{
	const char* sLevel = I::EngineClient->GetLevelName();
	if (!sLevel || m_sLevel == sLevel)
		return;

	m_sLevel = sLevel;
	m_pFloor.reset();
	const char* sGameDir = I::EngineClient->GetGameDirectory();
	if (m_sLevel.empty() || !sGameDir || !*sGameDir)
		return;

	auto tName = std::filesystem::path(m_sLevel).filename().replace_extension(".nav");
	const std::filesystem::path tGameDir = sGameDir;
	const std::filesystem::path aPaths[] = {
		tGameDir / "maps" / tName,
		tGameDir / "download" / "maps" / tName,
		std::filesystem::path("unibox") / "Nav" / tName
	};

	CNavFile tNav;
	bool bLoaded = false;
	for (const auto& tPath : aPaths)
	{
		const auto sPath = tPath.string();
		if (tNav.Load(sPath.c_str()))
		{
			bLoaded = true;
			break;
		}
	}
	if (!bLoaded)
		return;

	auto pFloor = std::make_shared<std::vector<FloorArea_t>>();
	pFloor->reserve(tNav.m_vAreas.size());
	for (const auto& tArea : tNav.m_vAreas)
	{
		if (tArea.m_vNwCorner.x < tArea.m_vSeCorner.x && tArea.m_vNwCorner.y < tArea.m_vSeCorner.y)
			pFloor->push_back({
				{ tArea.m_vNwCorner.x, tArea.m_vNwCorner.y, tArea.m_flMinZ },
				{ tArea.m_vSeCorner.x, tArea.m_vSeCorner.y, tArea.m_flMaxZ } });
	}
	m_pFloor = std::move(pFloor);
}

void CRadar::Reset()
{
	m_sLevel.clear();
	m_pFloor.reset();
	m_tCache.Set({});
}

void CRadar::Store(CTFPlayer* pLocal)
{
	if (!pLocal || !Vars::Radar::Enabled.Value)
	{
		if (m_tCache.Get()->m_bValid)
			m_tCache.Set({});
		return;
	}

	LoadFloor();

	auto pCenter = pLocal;
	const int iObserverMode = pLocal->m_iObserverMode();
	if (iObserverMode == OBS_MODE_FIRSTPERSON || iObserverMode == OBS_MODE_THIRDPERSON)
	{
		if (auto pTarget = pLocal->m_hObserverTarget().Get(); pTarget && pTarget->IsPlayer())
			pCenter = pTarget->As<CTFPlayer>();
	}

	Snapshot_t tSnapshot = {};
	tSnapshot.m_bValid = true;
	tSnapshot.m_vOrigin = pCenter->GetRenderOrigin();
	tSnapshot.m_flYaw = iObserverMode == OBS_MODE_FIRSTPERSON && pCenter != pLocal
		? pCenter->GetEyeAngles().y : I::EngineClient->GetViewAngles().y;
	tSnapshot.m_pFloor = m_pFloor;

	const int iTeam = pCenter->IsInValidTeam();
	const auto& vPlayers = H::Entities.GetGroup(EntityEnum::PlayerAll);
	tSnapshot.m_vPlayers.reserve(vPlayers.size());
	for (auto pEntity : vPlayers)
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer == pLocal || pPlayer == pCenter || pPlayer->IsDormant()
			|| !pPlayer->IsAlive() || pPlayer->IsAGhost() || !pPlayer->IsInValidTeam())
			continue;

		const bool bTeammate = iTeam ? pPlayer->m_iTeamNum() == iTeam : pPlayer->m_iTeamNum() == TF_TEAM_BLUE;
		if (!iTeam || !bTeammate || Vars::Radar::ShowTeammates.Value)
			tSnapshot.m_vPlayers.push_back({ pPlayer->GetRenderOrigin(), pPlayer->GetEyeAngles().y, pPlayer->m_iClass(), bTeammate });
	}
	m_tCache.Set(std::move(tSnapshot));
}

void CRadar::Draw(bool bInGame)
{
	using namespace ImGui;

	const bool bEditing = F::Menu.m_bIsOpen && F::Menu.m_bInputReady;
	if (!(bEditing ? FGet(Vars::Radar::Enabled) : Vars::Radar::Enabled.Value)
		|| (!bEditing && (!bInGame || I::MatSystemSurface->IsCursorVisible() && !I::EngineClient->IsPlayingDemo())))
		return;

	const auto pSnapshot = m_tCache.Get();
	const bool bHasView = bInGame && pSnapshot->m_bValid;
	if (!bHasView && !bEditing)
		return;

	auto& tWindow = Vars::Radar::Window[DEFAULT_BIND];
	const float flHeader = H::Draw.Scale(32), flPad = H::Draw.Scale(6);
	const ImVec2 vDisplay = GetIO().DisplaySize;
	const float flMin = H::Draw.Scale(100);
	const float flMax = H::Draw.Scale(600);
	const ImVec2 vMaxSize = { std::clamp(vDisplay.x, flMin, flMax), std::clamp(vDisplay.y - float(tWindow.y), flMin, flMax) };
	const ImVec2 vSavedSize = { std::clamp(float(tWindow.w), flMin, vMaxSize.x), std::clamp(float(tWindow.h), flMin, vMaxSize.y) };
	const auto iPlacement = tWindow == m_tLastWindow ? ImGuiCond_Appearing : ImGuiCond_Always;
	SetNextWindowPos({ float(tWindow.x - int(vSavedSize.x) / 2), float(tWindow.y) }, iPlacement);
	SetNextWindowSize(vSavedSize, iPlacement);
	SetNextWindowSizeConstraints({ flMin, flMin }, vMaxSize);
	PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
	PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	ImGuiWindowFlags iFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
		| ImGuiWindowFlags_NoNav;
	if (!bEditing)
		iFlags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoResize;

	if (Begin("Radar", nullptr, iFlags))
	{
		const ImVec2 vPos = GetWindowPos(), vSize = GetWindowSize();
		const ImVec2 vMin = vPos + ImVec2(flPad, flHeader + flPad), vMax = vPos + vSize - ImVec2(flPad, flPad);
		auto pDrawList = GetWindowDrawList();

		if (bEditing)
			tWindow = { int(vPos.x) + int(vSize.x) / 2, int(vPos.y), int(vSize.x), int(vSize.y) };
		m_tLastWindow = tWindow;

		RenderOverlayWindow(flHeader, ICON_MD_MAP, "Radar");
		pDrawList->AddRectFilled(vMin, vMax, F::Render.Background0p5);
		if (bHasView)
		{
			const float flRange = std::clamp(float(Vars::Radar::Range.Value), 250.f, 4000.f);
			const float flRadarYaw = Vars::Radar::RotateMap.Value ? pSnapshot->m_flYaw : 0.f;
			const float flYaw = Math::Deg2Rad(flRadarYaw);
			const float flCos = cosf(flYaw), flSin = sinf(flYaw);
			const ImVec2 vHalf = (vMax - vMin) * 0.5f;
			const float flScale = std::min(vHalf.x, vHalf.y) / flRange;
			const ImVec2 vCenter = (vMin + vMax) * 0.5f;
			const Vec3& vOrigin = pSnapshot->m_vOrigin;
			const auto fProject = [&](float x, float y)
			{
				x -= vOrigin.x;
				y -= vOrigin.y;
				return vCenter + ImVec2(x * flSin - y * flCos, -x * flCos - y * flSin) * flScale;
			};

			pDrawList->PushClipRect(vMin, vMax, true);
			if (Vars::Radar::DrawMap.Value && pSnapshot->m_pFloor)
			{
				const float flExtentX = (vHalf.x * fabsf(flSin) + vHalf.y * fabsf(flCos)) / flScale;
				const float flExtentY = (vHalf.x * fabsf(flCos) + vHalf.y * fabsf(flSin)) / flScale;
				const auto iDrawFlags = pDrawList->Flags;
				pDrawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
				for (bool bNear : { false, true })
				{
					ImVec4 tFill = MixColor(F::Render.Background1, F::Render.Active, bNear ? 0.38f : 0.16f);
					tFill.w = F::Render.Background1.Value.w;
					const ImU32 uFill = FadeColor(tFill);
					const ImU32 uLine = FadeColor(F::Render.Background0, 0.55f);
					for (const auto& [vLow, vHigh] : *pSnapshot->m_pFloor)
					{
						if (vLow.x > vOrigin.x + flExtentX || vHigh.x < vOrigin.x - flExtentX
							|| vLow.y > vOrigin.y + flExtentY || vHigh.y < vOrigin.y - flExtentY
							|| bNear != (vOrigin.z >= vLow.z - 80.f && vOrigin.z <= vHigh.z + 80.f))
							continue;

						pDrawList->AddQuadFilled(fProject(vLow.x, vLow.y), fProject(vLow.x, vHigh.y),
							fProject(vHigh.x, vHigh.y), fProject(vHigh.x, vLow.y), uFill);
						pDrawList->AddQuad(fProject(vLow.x, vLow.y), fProject(vLow.x, vHigh.y),
							fProject(vHigh.x, vHigh.y), fProject(vHigh.x, vLow.y), uLine);
					}
				}
				pDrawList->Flags = iDrawFlags;
			}

			const float flMarker = H::Draw.Scale(Vars::Radar::IconSize.Value);
			const float flDot = std::max(flMarker * 0.32f, H::Draw.Scale(2.5f));
			for (const auto& tPlayer : pSnapshot->m_vPlayers)
			{
				const ImVec2 vDot = fProject(tPlayer.m_vOrigin.x, tPlayer.m_vOrigin.y);
				if (vDot.x < vMin.x + flMarker * 0.5f || vDot.x > vMax.x - flMarker * 0.5f
					|| vDot.y < vMin.y + flMarker * 0.5f || vDot.y > vMax.y - flMarker * 0.5f)
					continue;

				const ImU32 uTeam = tPlayer.m_bTeammate ? IM_COL32(133, 198, 236, 255) : IM_COL32(247, 102, 96, 255);
				const float flPlayerYaw = Math::Deg2Rad(flRadarYaw - tPlayer.m_flYaw);
				if (Vars::Radar::PlayerStyle.Value == Vars::Radar::PlayerStyleEnum::ClassIcon)
				{
					const float flHalf = flMarker * 0.5f;
					pDrawList->AddRectFilled(vDot - ImVec2(flHalf + 1.f, flHalf + 1.f), vDot + ImVec2(flHalf + 1.f, flHalf + 1.f), IM_COL32(14, 15, 17, 230), H::Draw.Scale(3));
					pDrawList->AddRectFilled(vDot - ImVec2(flHalf, flHalf), vDot + ImVec2(flHalf, flHalf), uTeam, H::Draw.Scale(3));
					if (IDirect3DTexture9* pTexture = GetIconTexture(GetClassIconTexture(tPlayer.m_iClass)))
					{
						const float flIcon = flHalf - H::Draw.Scale(1.5f);
						pDrawList->AddImage(reinterpret_cast<ImTextureID>(pTexture), vDot - ImVec2(flIcon, flIcon), vDot + ImVec2(flIcon, flIcon));
					}
					else
						DrawArrow(pDrawList, vDot, flPlayerYaw, flMarker * 0.5f, uTeam);
				}
				else if (Vars::Radar::PlayerStyle.Value == Vars::Radar::PlayerStyleEnum::Arrow)
					DrawArrow(pDrawList, vDot, flPlayerYaw, std::max(flMarker * 0.62f, H::Draw.Scale(5)), uTeam);
				else
				{
					pDrawList->AddCircleFilled(vDot, flDot + 1.f, IM_COL32(14, 15, 17, 220));
					pDrawList->AddCircleFilled(vDot, flDot, uTeam);
				}
			}
			DrawArrow(pDrawList, vCenter, Math::Deg2Rad(flRadarYaw - pSnapshot->m_flYaw), std::max(flMarker * 0.58f, H::Draw.Scale(5)), FadeColor(F::Render.Active));
			pDrawList->PopClipRect();
		}
		if (bEditing)
		{
			const ImVec2 vCorner = vPos + vSize - ImVec2(H::Draw.Scale(3), H::Draw.Scale(3));
			const float flGrip = H::Draw.Scale(7);
			pDrawList->AddLine(vCorner - ImVec2(flGrip, 0.f), vCorner - ImVec2(0.f, flGrip), FadeColor(F::Render.Inactive, 0.55f));
		}
	}
	End();
	PopStyleVar(2);
	if (bEditing)
		F::Render.Cursor = GetMouseCursor();
}

#endif
