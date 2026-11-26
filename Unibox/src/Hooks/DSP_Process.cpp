#include "../SDK/SDK.h"


MAKE_HOOK(DSP_Process, S::DSP_Process(), void,
	unsigned int idsp, int* pbfront, int* pbrear, int* pbcenter, int sampleCount)
{
	DEBUG_RETURN(DSP_Process, idsp, pbfront, pbrear, pbcenter, sampleCount);

#ifndef TEXTMODE
	if (!Vars::Misc::Sound::RemoveDSP.Value)
		CALL_ORIGINAL(idsp, pbfront, pbrear, pbcenter, sampleCount);
#endif
}
