#pragma once
#include "../../../SDK/SDK.h"

struct ImDrawList;

class CRadar
{
private:
	bool GetDrawPosition(CBaseEntity* pCenter, CBaseEntity* pEntity, float& x, float& y, float& z);
	void DrawBackground(ImDrawList* pDrawList);
	void DrawPoints(ImDrawList* pDrawList, CBaseEntity* pCenter);

public:
	void Draw(ImDrawList* pDrawList, CTFPlayer* pLocal);
};

ADD_FEATURE(CRadar, Radar);
