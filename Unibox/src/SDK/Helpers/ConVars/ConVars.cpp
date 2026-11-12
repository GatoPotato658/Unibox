#include "ConVars.h"

#include "../../Definitions/Interfaces/ICVar.h"
#include "../../../Utils/Hash/FNV1A.h"

bool CConVars::UnlockInternal()
{
	if (!m_bUnlocked)
	{
		for (ConCommandBase* pBase = I::CVar->GetCommands(); pBase; pBase = pBase->m_pNext)
		{
			m_mFlagMap[pBase] = pBase->m_nFlags;
			pBase->m_nFlags &= ~(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT | FCVAR_NOT_CONNECTED);
		}
		m_bUnlocked = true;

		return true;
	}
	return false;
}

bool CConVars::RestoreInternal()
{
	if (m_bUnlocked)
	{
		for (ConCommandBase* pBase = I::CVar->GetCommands(); pBase; pBase = pBase->m_pNext)
		{
			if (auto it = m_mFlagMap.find(pBase); it != m_mFlagMap.end())
				pBase->m_nFlags = it->second;
		}
		m_mFlagMap.clear();
		m_bUnlocked = false;

		return true;
	}
	return false;
}

bool CConVars::Unlock()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return UnlockInternal();
}

bool CConVars::Restore()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return RestoreInternal();
}

bool CConVars::Modify(bool bUnlock)
{
	std::lock_guard<std::mutex> lock(m_Mutex);

	if (bUnlock == m_bUnlocked)
		return false;

	return bUnlock ? UnlockInternal() : RestoreInternal();
}

ConVar* CConVars::FindVar(const char* sCVar)
{
	std::lock_guard<std::mutex> lock(m_Mutex);

	auto uHash = FNV1A::Hash32(sCVar);
	if (auto it = m_mCVarMap.find(uHash); it != m_mCVarMap.end())
		return it->second;

	ConVar* pCVar = I::CVar->FindVar(sCVar);
	if (pCVar)
		m_mCVarMap.emplace(uHash, pCVar);
	return pCVar;
}

int CConVars::GetOriginalFlags(ConCommandBase* pCmd)
{
	std::lock_guard<std::mutex> lock(m_Mutex);

	if (auto it = m_mFlagMap.find(pCmd); it != m_mFlagMap.end())
		return it->second;
	return pCmd->m_nFlags;
}
