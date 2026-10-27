#ifdef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(Voice_Enabled, "engine.dll", "48 8B 05 ? ? ? ? 83 78 ? ? 0F 95 C0 C3 CC 48 89 5C 24", 0x0);

MAKE_HOOK(Voice_Enabled, S::Voice_Enabled(), bool)
{
	// Should prevent engine from reading voicechat data
	return false;
}
#endif