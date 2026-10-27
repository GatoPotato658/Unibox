#include "Signatures.h"

#include "../Memory/Memory.h"
#include "../../Core/Core.h"
#include "../../SDK/SDK.h"
#include <string>
#include <format>
#include <limits>

CSignature::CSignature(const char* sDLLName, const char* sSignature, int8_t nOffset, const char* sName)
{
	m_dwVal = 0x0;
	m_sDLLName = sDLLName;
	m_sSignature = sSignature;
	m_nOffset = nOffset;
	m_sName = sName;

	U::Signatures.AddSignature(this);
}

bool CSignature::Initialize()
{
	m_dwVal = 0x0;
	const auto Fail = [this]()
	{
		U::Core.AppendFailText(std::format("CSignature::Initialize() failed to initialize:\n  {}\n  {}\n  {}", m_sName ? m_sName : "", m_sDLLName ? m_sDLLName : "", m_sSignature ? m_sSignature : "").c_str());
		return false;
	};

	if (!m_sDLLName || !m_sSignature)
		return Fail();

	const double flScanStart = SDK::InitNowMs();
	const auto dwAddress = U::Memory.FindSignature(m_sDLLName, m_sSignature);
	const double flScanMs = SDK::InitNowMs() - flScanStart;
	if (flScanMs >= 20.0)
		SDK::LogInitTiming(m_sName ? m_sName : "signature", flScanMs);
	if (!dwAddress)
		return Fail();

	uintptr_t dwValue = dwAddress;
	if (m_nOffset < 0)
	{
		const auto uOffset = static_cast<uintptr_t>(-static_cast<int>(m_nOffset));
		if (uOffset > dwValue)
			return Fail();

		dwValue -= uOffset;
	}
	else
	{
		const auto uOffset = static_cast<uintptr_t>(m_nOffset);
		if (uOffset > (std::numeric_limits<uintptr_t>::max)() - dwValue)
			return Fail();

		dwValue += uOffset;
	}

	if (!dwValue)
		return Fail();

	m_dwVal = dwValue;
	return true;
}

bool CSignatures::Initialize()
{
	SDK::CInitTimingScope tTotal("Signatures.Initialize");

	m_bFailed = false;
	for (auto pSignature : m_vSignatures)
	{
		if (!pSignature)
			continue;

		if (!pSignature->Initialize())
			m_bFailed = true;
	}

	return !m_bFailed;
}
