#pragma once
#include "../../../SDK/SDK.h"

class CAutoQueue
{
private:
	std::string m_sLastLevelName;
	bool m_bNavmeshAbandonTriggered = false;
	float m_flNavmeshAbandonStartTime = 0.0f;
	float m_flAutoDumpStartTime = 0.0f;
	bool m_bAutoDumpedThisMatch = false;

public:
	void Run();
};

ADD_FEATURE(CAutoQueue, AutoQueue);
