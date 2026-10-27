#pragma once
#include "../../SDK/SDK.h"

class CCreateMove
{
private:
	void UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
#ifndef TEXTMODE
	void LocalAnimations(CTFPlayer* pLocal, CUserCmd* pCmd);
#endif

	struct CmdHistory_t
	{
		Vec3 m_vAngle;
		bool m_bAttack1;
		bool m_bAttack2;
		bool m_bSendingPacket;
	};
	void AntiCheatCompatibility(CUserCmd* pCmd);
public:
	void Run(int nSequenceNum, float flInputSampleFrametime);
};

ADD_FEATURE(CCreateMove, CreateMove);