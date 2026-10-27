#pragma once
#include "CBaseTrigger.h"

class CFuncPasstimeGoal : public CBaseTrigger
{
public:
	enum GoalType
	{
		TYPE_HOOP,
		TYPE_ENDZONE,
		TYPE_TOWER
	};

	NETVAR(m_bTriggerDisabled, bool, "CFuncPasstimeGoal", "m_bTriggerDisabled");
	NETVAR(m_iGoalType, int, "CFuncPasstimeGoal", "m_iGoalType");
};
