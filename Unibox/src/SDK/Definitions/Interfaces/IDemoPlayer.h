#pragma once
#include "Interface.h"

class IDemoPlayer
{
public:
	VIRTUAL(IsPlayingBack, bool, 6, this);
};

MAKE_INTERFACE_SIGNATURE(IDemoPlayer, DemoPlayer, "engine.dll", "48 8B 0D ? ? ? ? 40 B7", 0x0, 1);