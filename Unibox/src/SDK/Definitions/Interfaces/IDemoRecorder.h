#pragma once
#include "Interface.h"

class IDemoRecorder
{
public:
	VIRTUAL(IsRecording, bool, 4, this);
	VIRTUAL_ARGS(RecordUserInput, void, 9, (int iCmdNum), this, iCmdNum);
};

MAKE_INTERFACE_SIGNATURE(IDemoRecorder, DemoRecorder, "engine.dll", "48 8B 0D ? ? ? ? 8D 56", 0x0, 1);