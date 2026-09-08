#include "BytePatches.h"

#include "../Core/Core.h"
#include <cstring>
#include <format>
#include <limits>

BytePatch::BytePatch(const char* sModule, const char* sSignature, int iOffset, const char* sPatch)
{
	m_sModule = sModule;
	m_sSignature = sSignature;
	m_iOffset = iOffset;

	auto vPatch = U::Memory.PatternToByte(sPatch);
	m_vPatch = vPatch;
	m_iSize = vPatch.size();
	m_vOriginal.resize(m_iSize);
}

bool BytePatch::Write(const std::vector<byte>& vBytes)
{
	if (!m_pAddress || !m_iSize || vBytes.size() != m_iSize)
		return false;

	DWORD flOldProtect;
	if (!VirtualProtect(m_pAddress, m_iSize, PAGE_EXECUTE_READWRITE, &flOldProtect))
		return false;

	std::memcpy(m_pAddress, vBytes.data(), m_iSize);
	const auto bFlushed = FlushInstructionCache(GetCurrentProcess(), m_pAddress, m_iSize) != FALSE;
	DWORD flRestoredProtect;
	const auto bRestored = VirtualProtect(m_pAddress, m_iSize, flOldProtect, &flRestoredProtect) != FALSE;
	return bFlushed && bRestored;
}

bool BytePatch::Initialize()
{
	if (m_bIsPatched)
		return true;

	const auto Fail = [this]()
	{
		U::Core.AppendFailText(std::format("BytePatch::Initialize() failed to initialize:\n  {}\n  {}", m_sModule ? m_sModule : "", m_sSignature ? m_sSignature : "").c_str());
		return false;
	};
	if (!m_iSize || m_vPatch.size() != m_iSize || !m_sModule || !m_sSignature)
		return Fail();

	m_pAddress = LPVOID(U::Memory.FindSignature(m_sModule, m_sSignature));
	if (!m_pAddress)
		return Fail();

	const auto pSignatureAddress = reinterpret_cast<const byte*>(m_pAddress);
	const auto vExpected = U::Memory.PatternToInt(m_sSignature);
	if (vExpected.empty())
		return Fail();
	for (size_t i = 0; i < vExpected.size(); ++i)
	{
		if (vExpected[i] != -1 && pSignatureAddress[i] != vExpected[i])
			return Fail();
	}

	const auto uBaseAddress = reinterpret_cast<uintptr_t>(m_pAddress);
	uintptr_t uPatchAddress = uBaseAddress;
	if (m_iOffset < 0)
	{
		const auto uOffset = static_cast<uintptr_t>(-static_cast<int64_t>(m_iOffset));
		if (uOffset > uBaseAddress)
			return Fail();

		uPatchAddress -= uOffset;
	}
	else
	{
		const auto uOffset = static_cast<uintptr_t>(m_iOffset);
		if (uOffset > (std::numeric_limits<uintptr_t>::max)() - uBaseAddress)
			return Fail();

		uPatchAddress += uOffset;
	}
	m_pAddress = reinterpret_cast<LPVOID>(uPatchAddress);

	MEMORY_BASIC_INFORMATION tMemoryInfo;
	if (VirtualQuery(m_pAddress, &tMemoryInfo, sizeof(tMemoryInfo)) != sizeof(tMemoryInfo)
		|| tMemoryInfo.State != MEM_COMMIT
		|| !tMemoryInfo.Protect
		|| (tMemoryInfo.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		return Fail();

	const auto uRegionBase = reinterpret_cast<uintptr_t>(tMemoryInfo.BaseAddress);
	if (uPatchAddress < uRegionBase || uPatchAddress - uRegionBase >= tMemoryInfo.RegionSize
		|| m_iSize > tMemoryInfo.RegionSize - (uPatchAddress - uRegionBase))
		return Fail();

	DWORD flOldProtect;
	if (!VirtualProtect(m_pAddress, m_iSize, PAGE_EXECUTE_READWRITE, &flOldProtect))
		return Fail();

	std::memcpy(m_vOriginal.data(), m_pAddress, m_iSize);
	DWORD flRestoredProtect;
	if (!VirtualProtect(m_pAddress, m_iSize, flOldProtect, &flRestoredProtect))
		return Fail();

	if (!Write(m_vPatch))
	{
		m_bIsPatched = !Write(m_vOriginal);
		return Fail();
	}

	return m_bIsPatched = true;
}

void BytePatch::Unload()
{
	if (!m_bIsPatched)
		return;

	if (Write(m_vOriginal))
		m_bIsPatched = false;
	else
		U::Core.AppendFailText(std::format("BytePatch::Unload() failed to unload:\n  {}\n  {}", m_sModule ? m_sModule : "", m_sSignature ? m_sSignature : "").c_str());
}



bool CBytePatches::Initialize()
{
	m_vPatches = {
		BytePatch("engine.dll", "0F 82 ? ? ? ? 4A 63 84 2F", 0x0, "90 90 90 90 90 90"), // skybox fix
		//BytePatch("server.dll", "75 ? 44 38 A7 ? ? ? ? 75 ? 41 3B DD", 0x0, "EB"), // listen server speedhack
		BytePatch("vguimatsurface.dll", "66 83 FE ? 0F 84", 0x0, "66 83 FE 00"), // include '&' in text size

		// CStorePage::DoPreviewItem
		BytePatch("client.dll", "40 53 48 81 EC ? ? ? ? 0F B7 DA", 0xe8, "A7"),
		// Removes loadout switch delay
		BytePatch("client.dll", "73 ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 32 C0", 0x0, "EB"),
	};

	bool bFail = false;
	for (auto& tPatch : m_vPatches)
	{
		if (!tPatch.Initialize())
			bFail = true;
	}

	return !bFail;
}

void CBytePatches::Unload()
{
	for (auto& tPatch : m_vPatches)
		tPatch.Unload();
}
