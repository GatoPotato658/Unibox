#pragma once
#include "Interface.h"
#include "../Types.h"

class CThirdPersonManager
{
public:
	Vector m_vecCameraOffset;
	Vector m_vecDesiredCameraOffset;
	Vector m_vecCameraOrigin;
	bool m_bUseCameraOffsets;
	QAngle m_ViewAngles;
	float m_flFraction;
	float m_flUpFraction;
	float m_flTargetFraction;
	float m_flTargetUpFraction;
	bool m_bOverrideThirdPerson;
	bool m_bForced;
	float m_flUpOffset;
	float m_flLerpTime;
	float m_flUpLerpTime;
};
