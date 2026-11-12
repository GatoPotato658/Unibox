#include "NetworkFix.h"

using CLReadPacketsFn = void(__fastcall*)(bool);

void CReadPacketState::Store()
{
	m_flFrameTimeClientState = I::ClientState->m_frameTime;
	m_flFrameTime = I::GlobalVars->frametime;
	m_flCurTime = I::GlobalVars->curtime;
	m_nTickCount = I::GlobalVars->tickcount;
}

void CReadPacketState::Restore()
{
	I::ClientState->m_frameTime = m_flFrameTimeClientState;
	I::GlobalVars->frametime = m_flFrameTime;
	I::GlobalVars->curtime = m_flCurTime;
	I::GlobalVars->tickcount = m_nTickCount;
}

void CNetworkFix::FixInputDelay(bool bFinalTick)
{
	if (!Vars::Misc::Game::NetworkFix.Value || !I::EngineClient->IsInGame() || SDK::IsLoopback())
		return;

	m_tBackup.Store();

	static auto CL_ReadPackets = U::Hooks.Get("CL_ReadPackets");
	CL_ReadPackets->As<CLReadPacketsFn>()(bFinalTick);

	m_tState.Store();
	m_tBackup.Restore();
}

bool CNetworkFix::ShouldReadPackets()
{
	if (!Vars::Misc::Game::NetworkFix.Value || !I::EngineClient->IsInGame() || SDK::IsLoopback())
		return true;

	m_tState.Restore();
	return false;
}

static int RoundToCode(float flValue)
{
	return static_cast<int>(flValue < 0.f ? flValue - 0.5f : flValue + 0.5f);
}

static float RangeMultiplier(int iBits, float flRange)
{
	const uint32_t uHighValue = (1u << uint32_t(iBits)) - 1u;
	const float flTop = float(uHighValue);

	for (float flFactor : { 1.f, 0.9999f, 0.99f, 0.9f, 0.8f, 0.7f })
	{
		const float flMultiplier = flTop / flRange * flFactor;
		if (uint32_t(flMultiplier * flRange) <= uHighValue && double(flMultiplier * flRange) <= double(uHighValue))
			return flMultiplier;
	}

	return 0.f;
}

static Quantiser_t BuildQuantiser(SendProp* pProp, SendPropType eType)
{
	Quantiser_t tOut = {};

	const int iFlags = pProp->GetFlags();
	if (iFlags & (SPROP_COORD | SPROP_COORD_MP))
	{
		tOut.m_eEncoding = EPropEncoding::FixedPoint;
		tOut.m_iDenominator = COORD_DENOMINATOR;
		return tOut;
	}
	if (iFlags & SPROP_COORD_MP_LOWPRECISION)
	{
		tOut.m_eEncoding = EPropEncoding::FixedPoint;
		tOut.m_iDenominator = COORD_DENOMINATOR_LOWPRECISION;
		return tOut;
	}
	if (iFlags & SPROP_COORD_MP_INTEGRAL)
	{
		tOut.m_eEncoding = EPropEncoding::FixedPoint;
		tOut.m_iDenominator = 1;
		return tOut;
	}
	if (iFlags & SPROP_NOSCALE)
		return tOut;
	if (iFlags & SPROP_NORMAL)
	{
		if (eType == DPT_Float)
			tOut.m_eEncoding = EPropEncoding::Normal;
		return tOut;
	}

	if (pProp->m_nBits <= 0 || pProp->m_nBits >= 32)
		return tOut;

	const float flRange = pProp->m_fHighValue - pProp->m_fLowValue;
	if (flRange <= 0.f)
		return tOut;

	const float flScale = RangeMultiplier(pProp->m_nBits, flRange);
	if (flScale <= 0.f)
		return tOut;

	tOut.m_eEncoding = EPropEncoding::Scaled;
	tOut.m_flLow = pProp->m_fLowValue;
	tOut.m_flHigh = pProp->m_fHighValue;
	tOut.m_flScale = flScale;
	tOut.m_flDecodeScale = float((1u << uint32_t(pProp->m_nBits)) - 1u) / flRange;
	tOut.m_bWraps = pProp->m_fLowValue == 0.f && pProp->m_fHighValue == 360.f;
	return tOut;
}

int Quantiser_t::Encode(float flValue) const
{
	switch (m_eEncoding)
	{
	case EPropEncoding::FixedPoint:
	{
		const int iMagnitude = int(fabsf(flValue) * float(m_iDenominator));
		return flValue < 0.f ? -iMagnitude : iMagnitude;
	}
	case EPropEncoding::Normal:
	{
		const int iMagnitude = std::min(int(fabsf(flValue) * float(NORMAL_DENOMINATOR)), NORMAL_DENOMINATOR);
		return flValue < 0.f ? -iMagnitude : iMagnitude;
	}
	case EPropEncoding::Scaled:
	{
		float flRanged = flValue;
		if (m_bWraps && (flRanged < m_flLow || flRanged > m_flHigh))
		{
			flRanged -= 360.f * float(int(flRanged / 360.f));
			if (flRanged < 0.f)
				flRanged += 360.f;
		}
		return int((std::clamp(flRanged, m_flLow, m_flHigh) - m_flLow) * m_flScale + 0.5f);
	}
	}
	return 0;
}

int Quantiser_t::Invert(float flDecoded) const
{
	switch (m_eEncoding)
	{
	case EPropEncoding::FixedPoint: return RoundToCode(flDecoded * float(m_iDenominator));
	case EPropEncoding::Normal: return RoundToCode(flDecoded * float(NORMAL_DENOMINATOR));
	case EPropEncoding::Scaled: return RoundToCode((flDecoded - m_flLow) * m_flDecodeScale);
	}
	return 0;
}

bool Quantiser_t::Holds(float flCandidate, float flDecoded) const
{
	if (m_eEncoding == EPropEncoding::None || flCandidate == flDecoded)
		return false;

	if (m_eEncoding == EPropEncoding::Scaled && !m_bWraps && (flCandidate < m_flLow || flCandidate > m_flHigh))
		return false;

	return Encode(flCandidate) == Invert(flDecoded);
}

static SendTable* FindSendTable(RecvTable* pRecv)
{
	if (!pRecv || !pRecv->m_pNetTableName || !pRecv->m_pDecoder)
		return nullptr;

	auto pDecoder = reinterpret_cast<void**>(pRecv->m_pDecoder);
	for (int i = 0; i < 8; i++)
	{
		if (pDecoder[i] != pRecv)
			continue;

		auto pSend = static_cast<SendTable*>(pDecoder[i + 1]);
		if (!pSend || uintptr_t(pSend) & (sizeof(void*) - 1) || uintptr_t(pSend) < 0x10000
			|| !pSend->m_pProps || pSend->m_nProps <= 0 || pSend->m_nProps > 4096
			|| !pSend->m_pNetTableName || strcmp(pSend->m_pNetTableName, pRecv->m_pNetTableName))
			return nullptr;

		return pSend;
	}

	return nullptr;
}

static void GatherExcludes(SendTable* pSend, std::vector<std::pair<std::string_view, std::string_view>>& vOut, int iDepth)
{
	if (!pSend || !pSend->m_pProps || iDepth >= 16)
		return;

	for (int i = 0; i < pSend->m_nProps; i++)
	{
		auto& tProp = pSend->m_pProps[i];
		if (tProp.GetFlags() & SPROP_EXCLUDE)
		{
			if (tProp.m_pExcludeDTName && tProp.m_pVarName)
				vOut.emplace_back(tProp.m_pExcludeDTName, tProp.m_pVarName);
		}
		else if (tProp.m_Type == DPT_DataTable)
			GatherExcludes(tProp.GetDataTable(), vOut, iDepth + 1);
	}
}

static SendProp* FindSendProp(SendTable* pSend, const char* sName)
{
	for (int i = 0; i < pSend->m_nProps; i++)
	{
		auto& tProp = pSend->m_pProps[i];
		if (tProp.m_pVarName && !strcmp(tProp.m_pVarName, sName))
			return &tProp;
	}
	return nullptr;
}

static EPropScope NarrowScope(EPropScope eScope, std::string_view sTable)
{
	if (eScope != EPropScope::Any)
		return eScope;
	if (sTable == "localdata" || sTable == "tflocaldata")
		return EPropScope::Local;
	if (sTable == "nonlocaldata" || sTable == "tfnonlocaldata")
		return EPropScope::NonLocal;
	return EPropScope::Any;
}

void CNetworkFix::CollectProps(RecvTable* pRecv, SendTable* pSend, const std::vector<std::pair<std::string_view, std::string_view>>& vExcluded, int iBaseOffset, int iDepth, EPropScope eScope)
{
	if (!pRecv || !pRecv->m_pProps || !pSend || !pSend->m_pProps || !pSend->m_pNetTableName || iDepth >= 16)
		return;

	for (int i = 0; i < pRecv->m_nProps; i++)
	{
		auto& tProp = pRecv->m_pProps[i];
		const char* sName = tProp.m_pVarName;
		if (!sName)
			continue;

		if (std::ranges::find(vExcluded, std::pair<std::string_view, std::string_view>(pSend->m_pNetTableName, sName)) != vExcluded.end())
			continue;

		auto pMirror = FindSendProp(pSend, sName);
		if (!pMirror)
			continue;

		const int iOffset = iBaseOffset + tProp.GetOffset();
		if (tProp.m_RecvType == DPT_DataTable)
		{
			if (tProp.GetDataTable() && pMirror->GetDataTable())
				CollectProps(tProp.GetDataTable(), pMirror->GetDataTable(), vExcluded, iOffset, iDepth + 1, NarrowScope(eScope, sName));
			continue;
		}

		int iComponents;
		switch (tProp.m_RecvType)
		{
		case DPT_Float: iComponents = 1; break;
		case DPT_VectorXY: iComponents = 2; break;
		case DPT_Vector: iComponents = 3; break;
		default: continue;
		}

		const Quantiser_t tQuantiser = BuildQuantiser(pMirror, tProp.m_RecvType);
		if (tQuantiser.m_eEncoding == EPropEncoding::None)
			continue;

		if (std::ranges::any_of(m_vCompressedProps, [&](const CompressedProp_t& tOther) { return tOther.m_iOffset == iOffset && tOther.m_eScope == eScope; }))
			continue;

		m_vCompressedProps.push_back({ iOffset, iComponents, eScope, tQuantiser });
	}
}

bool CNetworkFix::BuildCompression()
{
	if (m_pCompressedClass)
		return true;
	if (m_iBuildAttempts >= 32)
		return false;

	m_iBuildAttempts++;
	m_vCompressedProps.clear();

	for (auto pClass = I::Client->GetAllClasses(); pClass; pClass = pClass->m_pNext)
	{
		if (!pClass->m_pNetworkName || strcmp(pClass->m_pNetworkName, "CTFPlayer"))
			continue;

		auto pSend = FindSendTable(pClass->m_pRecvTable);
		if (!pSend)
			return false;

		std::vector<std::pair<std::string_view, std::string_view>> vExcluded = {};
		GatherExcludes(pSend, vExcluded, 0);
		CollectProps(pClass->m_pRecvTable, pSend, vExcluded, 0, 0, EPropScope::Any);
		if (m_vCompressedProps.empty())
			return false;

		for (auto& tSnapshot : m_aSnapshots)
			tSnapshot = { -1, std::vector<Vec3>(m_vCompressedProps.size()) };
		m_pCompressedClass = pClass;
		return true;
	}

	return false;
}

void CNetworkFix::StorePredicted(CBaseEntity* pLocal, int iCommand)
{
	if (!pLocal || iCommand < 0 || !BuildCompression() || pLocal->GetClientClass() != m_pCompressedClass)
		return;

	auto& tSnapshot = m_aSnapshots[iCommand % m_aSnapshots.size()];
	tSnapshot.m_iCommand = iCommand;

	auto pBase = reinterpret_cast<const byte*>(pLocal);
	for (size_t i = 0; i < m_vCompressedProps.size(); i++)
	{
		auto& tProp = m_vCompressedProps[i];
		auto pField = reinterpret_cast<const float*>(pBase + tProp.m_iOffset);
		for (int j = 0; j < tProp.m_iComponents; j++)
			tSnapshot.m_vValues[i][j] = pField[j];
	}
}

void CNetworkFix::RestoreCompressed()
{
	if (!m_pCompressedClass)
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	if (!pEntity || pEntity->GetClientClass() != m_pCompressedClass)
		return;

	const int iAck = I::ClientState->command_ack;
	auto& tSnapshot = m_aSnapshots[std::max(iAck, 0) % m_aSnapshots.size()];
	if (iAck < 0 || tSnapshot.m_iCommand != iAck)
		return;

	auto pBase = reinterpret_cast<byte*>(pEntity);
	for (size_t i = 0; i < m_vCompressedProps.size(); i++)
	{
		auto& tProp = m_vCompressedProps[i];
		if (tProp.m_eScope == EPropScope::NonLocal)
			continue;

		auto pField = reinterpret_cast<float*>(pBase + tProp.m_iOffset);
		for (int j = 0; j < tProp.m_iComponents; j++)
		{
			if (tProp.m_tQuantiser.Holds(tSnapshot.m_vValues[i][j], pField[j]))
				pField[j] = tSnapshot.m_vValues[i][j];
		}
	}
}

void CNetworkFix::Reset()
{
	m_pCompressedClass = nullptr;
	m_vCompressedProps.clear();
	m_aSnapshots = {};
	m_iBuildAttempts = 0;
}
