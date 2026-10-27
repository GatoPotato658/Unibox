#pragma once
#include "../../Definitions/Misc/ConVar.h"
#include "../../../Utils/Macros/Macros.h"
#include "../../../Utils/Hash/FNV1A.h"
#include <mutex>
#include <unordered_map>

class CConVars
{
private:
	std::unordered_map<uint32_t, ConVar*> m_mCVarMap = {};
	std::unordered_map<ConCommandBase*, int> m_mFlagMap = {};
	std::mutex m_Mutex = {};

	bool m_bUnlocked = false;

	bool UnlockInternal();
	bool RestoreInternal();

public:
	bool Unlock();
	bool Restore();
	bool Modify(bool bUnlock);

	ConVar* FindVar(const char* sCVar);
	int GetOriginalFlags(ConCommandBase* pCmd);
};

ADD_FEATURE_CUSTOM(CConVars, ConVars, H);
