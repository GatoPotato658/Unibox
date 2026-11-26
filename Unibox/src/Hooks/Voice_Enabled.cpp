#ifdef TEXTMODE
#include "../SDK/SDK.h"


MAKE_HOOK(Voice_Enabled, S::Voice_Enabled(), bool)
{
	TRACK_HOOK;

	// Should prevent engine from reading voicechat data
	return false;
}
#endif