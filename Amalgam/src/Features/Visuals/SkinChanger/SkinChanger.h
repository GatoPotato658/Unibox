#pragma once
#include "../../../SDK/SDK.h"

class CSkinChanger
{
	bool m_bWasEnabled = false;
	int m_iLastHash = 0;

public:
	void Apply();
};

ADD_FEATURE(CSkinChanger, SkinChanger);
