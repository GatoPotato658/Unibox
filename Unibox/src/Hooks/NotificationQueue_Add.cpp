#include "../SDK/SDK.h"


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