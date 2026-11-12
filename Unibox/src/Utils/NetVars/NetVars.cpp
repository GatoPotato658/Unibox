#include "NetVars.h"

#include "../../SDK/Definitions/Interfaces/CHLClient.h"
#include "../Hash/FNV1A.h"

#ifdef GetProp
	#undef GetProp
#endif

bool CNetVars::GetOffset(RecvTable* pTable, uint32_t uHash, int& iOffset)
{
	if (!pTable)
		return false;

	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		RecvProp* pProp = pTable->GetProp(i);
		if (uHash == FNV1A::Hash32(pProp->m_pVarName))
		{
			iOffset = pProp->GetOffset();
			return true;
		}

		if (auto pDataTable = pProp->GetDataTable())
		{
			int iChildOffset = 0;
			if (GetOffset(pDataTable, uHash, iChildOffset))
			{
				iOffset = iChildOffset + pProp->GetOffset();
				return true;
			}
		}
	}

	return false;
}

int CNetVars::GetOffset(RecvTable* pTable, const char* szNetVar)
{
	int iOffset = 0;
	return GetOffset(pTable, FNV1A::Hash32(szNetVar), iOffset) ? iOffset : 0;
}

int CNetVars::GetNetVar(const char* szClass, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szClass);
	for (auto pCurrNode = I::Client->GetAllClasses(); pCurrNode; pCurrNode = pCurrNode->m_pNext)
	{
		if (uHash == FNV1A::Hash32(pCurrNode->m_pNetworkName))
			return GetOffset(pCurrNode->m_pRecvTable, szNetVar);
	}

	return 0;
}

RecvProp* CNetVars::GetProp(RecvTable* pTable, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szNetVar);
	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		RecvProp* pProp = pTable->GetProp(i);
		if (uHash == FNV1A::Hash32(pProp->m_pVarName))
			return pProp;

		if (auto pDataTable = pProp->GetDataTable())
		{
			if (pProp = GetProp(pDataTable, szNetVar))
				return pProp;
		}
	}

	return nullptr;
}
