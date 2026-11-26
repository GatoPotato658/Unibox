#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Players/PlayerUtils.h"


MAKE_HOOK(bf_read_ReadString, S::bf_read_ReadString(), bool,
	void* rcx, char* pStr, int maxLen, bool bLine, int* pOutNumChars)
{
	DEBUG_RETURN(bf_read_ReadString, rcx, pStr, maxLen, bLine, pOutNumChars);

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CHudVote_MsgFunc_VoteStart_ReadString_Call();

	bool bReturn = CALL_ORIGINAL(rcx, pStr, maxLen, bLine, pOutNumChars);

	if (dwRetAddr == dwDesired)
	{
		auto pMsg = reinterpret_cast<bf_read*>(rcx);
		const int iOriginalBit = pMsg->m_iCurBit;
		const int iTarget = pMsg->ReadByte() >> 1;
		pMsg->Seek(iOriginalBit);

		if (!iTarget)
			return bReturn;

		int iType; const char* sName = F::PlayerUtils.GetPlayerName(iTarget, nullptr, &iType);
		if (iType == NameTypeEnum::None || !pStr || maxLen <= 0)
			return bReturn;

		int iChar = 0;
		while (iChar < maxLen - 1)
		{
			char val = sName[iChar];
			if (val == 0)
				break;
			else if (bLine && val == '\n')
				break;

			pStr[iChar] = val;
			++iChar;
		}
		pStr[iChar] = 0;
		if (pOutNumChars)
			*pOutNumChars = iChar;
	}

	return bReturn;
}
#endif
