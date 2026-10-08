#include "NavBotConfig.h"

#include "Jobs/NavBotJobs.h"

namespace NavBotConfig
{
	NavBotClassConfig_t Select(CTFPlayer* pLocal)
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

			return G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger ? CONFIG_GUNSLINGER_ENGINEER : CONFIG_ENGINEER;
		case TF_CLASS_SNIPER:
			return G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_COMPOUND_BOW ? CONFIG_MID_RANGE : CONFIG_LONG_RANGE;
		default:
			return CONFIG_MID_RANGE;
		}
	}
}
