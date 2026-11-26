#include "../SDK/SDK.h"


MAKE_HOOK(DataTable_Warning, S::DataTable_Warning(), void,
	const char* pInMessage, ...)
{
	TRACK_HOOK;

	// dont send datatable warnings
}