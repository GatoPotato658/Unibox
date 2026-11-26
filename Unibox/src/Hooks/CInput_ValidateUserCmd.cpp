#include "../SDK/SDK.h"


MAKE_HOOK(CInput_ValidateUserCmd, S::CInput_ValidateUserCmd(), void,
	void* rcx, CUserCmd* usercmd, int sequence_number)
{
	DEBUG_RETURN(CInput_ValidateUserCmd, rcx, usercmd, sequence_number);
}