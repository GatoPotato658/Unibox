#pragma once
#include "../../SDK/SDK.h"

class CAutoJoin
{
private:
	const std::array<std::string, 9u> m_aClassNames{ "scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer" };

public:
	void Run(CTFPlayer* pLocal);
};

ADD_FEATURE(CAutoJoin, AutoJoin)