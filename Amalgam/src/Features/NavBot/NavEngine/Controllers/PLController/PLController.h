#pragma once
#include "../../../../../SDK/SDK.h"

class CPLController
{
private:
	std::array<std::array<CObjectCartDispenser*, MAX_EDICTS>, 2> m_aPayloads = {};
	std::array<size_t, 2> m_aPayloadCounts = {};

public:
	// Get the closest Control Payload
	CObjectCartDispenser* GetClosestPayload(Vector vPos, int iTeam);

	void Init();
	void Update();
};

ADD_FEATURE(CPLController, PLController);
