#pragma once
#include "Interface.h"

class IDemoRecorder
{
public:
	VIRTUAL(IsRecording, bool, 4, this);
	VIRTUAL_ARGS(RecordUserInput, void, 9, (int iCmdNum), this, iCmdNum);
};
