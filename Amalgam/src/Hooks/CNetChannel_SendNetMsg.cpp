#include "../SDK/SDK.h"

#include "../Features/Ticks/Ticks.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Misc/Misc.h"
#include "../Features/AntiCheatCompatibility/AntiCheatCompatibility.h"

MAKE_SIGNATURE(CNetChannel_SendNetMsg, "engine.dll", "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B F1 45 0F B6 F1", 0x0);

MAKE_HOOK(CNetChannel_SendNetMsg, S::CNetChannel_SendNetMsg(), bool,
	CNetChannel* pNetChan, INetMessage& msg, bool bForceReliable, bool bVoice)
{
	DEBUG_RETURN(CNetChannel_SendNetMsg, pNetChan, msg, bForceReliable, bVoice);
	
	if (G::Unload)
		return CALL_ORIGINAL(pNetChan, msg, bForceReliable, bVoice);

	switch (msg.GetType())
	{
	case net_SetConVar:
	{
		auto pMsg = reinterpret_cast<NET_SetConVar*>(&msg);
		for (int i = 0; i < pMsg->m_ConVars.Count(); i++)
		{
			NET_SetConVar::CVar_t* localCvar = &pMsg->m_ConVars[i];

			// intercept and change any vars we want to control
			switch (FNV1A::Hash32(localCvar->Name))
			{
			case FNV1A::Hash32Const("cl_interp"):
				if (F::Backtrack.m_flSentInterp != -1.f)
					strncpy_s(localCvar->Value, std::to_string(F::Backtrack.m_flSentInterp).c_str(), MAX_OSPATH);
				if (F::AntiCheatCompatibility.Active())
				{ try {
					float flValue = std::stof(localCvar->Value);
					strncpy_s(localCvar->Value, std::to_string(std::min(flValue, 0.1f)).c_str(), MAX_OSPATH);
				} catch (...) {}; }
				break;
			case FNV1A::Hash32Const("cl_cmdrate"):
				if (F::Misc.m_iWishCmdrate != -1)
					strncpy_s(localCvar->Value, std::to_string(F::Misc.m_iWishCmdrate).c_str(), MAX_OSPATH);
				if (F::AntiCheatCompatibility.Active())
				{ try {
					int iValue = std::stof(localCvar->Value);
					strncpy_s(localCvar->Value, std::to_string(std::max(iValue, 10)).c_str(), MAX_OSPATH);
				} catch (...) {}; }
				break;
			/*
			case FNV1A::Hash32Const("cl_updaterate"):
				if (F::Misc.m_iWishUpdaterate != -1)
					strncpy_s(localCvar->Value, std::to_string(F::Misc.m_iWishUpdaterate).c_str(), MAX_OSPATH);
				break;
			*/
			case FNV1A::Hash32Const("cl_interp_ratio"):
			case FNV1A::Hash32Const("cl_interpolate"):
				strncpy_s(localCvar->Value, "1", MAX_OSPATH);
			}
		}
		break;
	}
	case clc_RespondCvarValue:
	{
		F::AntiCheatCompatibility.RespondCvarValue(msg);
		break;
	}
	case clc_VoiceData:
	{
		// stop lag with voice chat
		bVoice = true;
		break;
	}
	}

	return CALL_ORIGINAL(pNetChan, msg, bForceReliable, bVoice);
}