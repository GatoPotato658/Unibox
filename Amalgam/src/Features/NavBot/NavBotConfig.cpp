#include "NavBotConfig.h"

#include "NavBotJobs/NavBotJobs.h"

namespace NavBotConfig
{
	NavBotClassConfig_t Select(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
	{
		if (!pLocal)
			return CONFIG_MID_RANGE;

		switch (pLocal->m_iClass())
		{
		case TF_CLASS_SCOUT:
		case TF_CLASS_HEAVY:
			return CONFIG_SHORT_RANGE;
		case TF_CLASS_ENGINEER:
			if (!F::NavBotEngineer.IsEngieMode(pLocal))
				return CONFIG_SHORT_RANGE;

			if (pWeapon && pWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger)
				return CONFIG_GUNSLINGER_ENGINEER;

			return CONFIG_ENGINEER;
		case TF_CLASS_SNIPER:
			return pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? CONFIG_MID_RANGE : CONFIG_LONG_RANGE;
		default:
			return CONFIG_MID_RANGE;
		}
	}
}