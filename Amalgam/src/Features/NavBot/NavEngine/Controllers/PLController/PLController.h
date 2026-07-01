#pragma once
#include "../../../../../SDK/SDK.h"

class CPLController
{
private:
	// Valid_ents that controls all the payloads for each team. Red team is first, then comes blue team.
	std::array<std::vector<CObjectCartDispenser*>, 2> m_aPayloads;

public:
	// Get the closest Control Payload
	CObjectCartDispenser* GetClosestPayload(Vector vPos, int iTeam);

	void Init();
	void Update();
};

ADD_FEATURE(CPLController, PLController);