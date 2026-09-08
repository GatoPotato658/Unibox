#include "../SDK/SDK.h"

MAKE_HOOK(CInput_GetUserCmd, U::Memory.GetVirtual(I::Input, 8), CUserCmd*,
	void* rcx, int sequence_number)
{
	DEBUG_RETURN(CInput_GetUserCmd, rcx, sequence_number);

	if (sequence_number < 0)
		return nullptr;

	return &I::Input->m_pCommands[sequence_number % MULTIPLAYER_BACKUP];
}
