#pragma once
#include "../../SDK/SDK.h"
#include "../ImGui/IndicatorCache.h"

class CNavRender
{
private:
	struct NavIndicatorLine_t
	{
		std::string m_sText = {};
		Color_t m_tColor = {};
	};
	CIndicatorCache<std::vector<NavIndicatorLine_t>> m_tDrawCache = {};

	struct ObjectiveMarker_t
	{
		Vector m_vPos = {};
		float m_flRadius = 0.f;
		int m_iSegments = 0;
		float m_flExpire = 0.f;
		Color_t m_tColor = {};
	};
	std::vector<ObjectiveMarker_t> m_vObjectiveMarkers = {};

	void DrawPath();
	void DrawLocalArea(CTFPlayer* pLocal);
	void DrawBlacklist();
	void DrawHazards();
	void DrawEngineer(CTFPlayer* pLocal);
	void DrawFollowBotPath();
	void DrawHullCheck();

	void DrawDangerOverlay(CTFPlayer* pLocal);
	void DrawJumpPrediction(CTFPlayer* pLocal);
	void DrawCaptureTarget();
	void DrawPasstimeGoals();
	void DrawLookTarget();
	void DrawObjectiveMarkers();

public:
	void RenderWorld();
	void DrawOverlay(CTFPlayer* pLocal);
	void CacheIndicators(CTFPlayer* pLocal);
	void DrawIndicators();
	void AddObjectiveMarker(const Vector& vPos, float flRadius, int iSegments, float flDuration, Color_t tColor);
};

ADD_FEATURE(CNavRender, NavRender);
