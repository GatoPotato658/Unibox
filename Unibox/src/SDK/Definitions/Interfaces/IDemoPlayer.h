#pragma once
#include "Interface.h"

class IDemoPlayer
{
public:
	VIRTUAL(IsPlayingBack, bool, 6, this);
};
