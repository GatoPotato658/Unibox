#ifndef TEXTMODE
#include "../SDK/SDK.h"

enum EHoliday
{
	kHoliday_None = 0,
	kHoliday_TFBirthday,
	kHoliday_Halloween,
	kHoliday_Christmas,
	kHoliday_CommunityUpdate,
	kHoliday_EOTL,
	kHoliday_Valentines,
	kHoliday_MeetThePyro,
	kHoliday_FullMoon,
	kHoliday_HalloweenOrFullMoon,
	kHoliday_HalloweenOrFullMoonOrValentines,
	kHoliday_AprilFools,
	kHolidayCount,
};


MAKE_HOOK(TF_IsHolidayActive, S::TF_IsHolidayActive(), bool,
	int eHoliday)
{
	DEBUG_RETURN(TF_IsHolidayActive, eHoliday);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFPlayer_FireEvent_IsHolidayActive_Call();

	if (dwRetAddr == dwDesired)
		return true;
	
	return CALL_ORIGINAL(eHoliday);
}
#endif