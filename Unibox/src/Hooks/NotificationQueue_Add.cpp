#include "../SDK/SDK.h"

MAKE_SIGNATURE(NotificationQueue_Add, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B F9 48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 84 C0 75", 0x0);

MAKE_HOOK(NotificationQueue_Add, S::NotificationQueue_Add(), int,
	CEconNotification* pNotification)
{
	DEBUG_RETURN(NotificationQueue_Add, pNotification);

	if (Vars::Misc::Automation::AcceptItemDrops.Value && FNV1A::Hash32(pNotification->m_pText) == FNV1A::Hash32Const("TF_HasNewItems"))
	{
		pNotification->Accept();
		pNotification->Trigger();
		pNotification->UpdateTick();
		pNotification->MarkForDeletion();
	}

	return CALL_ORIGINAL(pNotification);
}