#include "Memory.h"

#include <cctype>
#include <cstdlib>
#include <format>
#include <Psapi.h>
#include <MinHook/hde/hde64.h>

namespace
{
	bool ParsePattern(const char* szPattern, std::vector<int>& vPattern, bool bAllowWildcards)
	{
		if (!szPattern)
			return false;

		const auto* pCurrent = szPattern;
		while (*pCurrent)
		{
			while (*pCurrent && std::isspace(static_cast<unsigned char>(*pCurrent)))
				++pCurrent;

			if (!*pCurrent)
				break;

			if (*pCurrent == '?')
			{
				if (!bAllowWildcards)
					return false;

				++pCurrent;
				if (*pCurrent == '?')
					++pCurrent;

				if (*pCurrent && !std::isspace(static_cast<unsigned char>(*pCurrent)))
					return false;

				vPattern.push_back(-1);
				continue;
			}

			char* pEnd = nullptr;
			const auto uValue = std::strtoul(pCurrent, &pEnd, 16);
			if (pEnd == pCurrent || uValue > 0xFF || pEnd - pCurrent > 2 || (*pEnd && !std::isspace(static_cast<unsigned char>(*pEnd))))
				return false;

			vPattern.push_back(static_cast<int>(uValue));
			pCurrent = pEnd;
		}

		return !vPattern.empty();
	}
}

std::vector<byte> CMemory::PatternToByte(const char* szPattern)
{
	std::vector<int> vParsed;
	if (!ParsePattern(szPattern, vParsed, false))
		return {};

	std::vector<byte> vPattern;
	vPattern.reserve(vParsed.size());
	for (const auto iByte : vParsed)
		vPattern.push_back(static_cast<byte>(iByte));
	return vPattern;
}

std::vector<int> CMemory::PatternToInt(const char* szPattern)
{
	std::vector<int> vPattern;
	if (!ParsePattern(szPattern, vPattern, true))
		return {};
	return vPattern;
}

uintptr_t CMemory::FindSignature(const char* szModule, const char* szPattern)
{
	if (!szModule || !szPattern)
		return 0x0;

	const auto hModule = GetModuleHandle(szModule);
	if (!hModule)
		return 0x0;

	MODULEINFO lpModuleInfo;
	if (!GetModuleInformation(GetCurrentProcess(), hModule, &lpModuleInfo, sizeof(MODULEINFO)))
		return 0x0;

	const auto iImageSize = static_cast<size_t>(lpModuleInfo.SizeOfImage);
	if (!iImageSize)
		return 0x0;

	const auto vPattern = PatternToInt(szPattern);
	const auto iPatternSize = vPattern.size();
	if (!iPatternSize || iPatternSize > iImageSize)
		return 0x0;

	const auto pImageBytes = reinterpret_cast<byte*>(hModule);
	const int* iPatternBytes = vPattern.data();
	for (size_t i = 0; i <= iImageSize - iPatternSize; ++i)
	{
		auto bFound = true;
		for (size_t j = 0; j < iPatternSize; ++j)
		{
			if (iPatternBytes[j] != -1 && pImageBytes[i + j] != iPatternBytes[j])
			{
				bFound = false;
				break;
			}
		}

		if (bFound)
			return uintptr_t(&pImageBytes[i]);
	}

	return 0x0;
}

uintptr_t CMemory::FindOptionalSignature(const char* szModule, const char* szPattern)
{
	const auto dwAddress = FindSignature(szModule, szPattern);
	if (!dwAddress || FindSignatureAtAddress(dwAddress, szPattern, dwAddress))
		return 0x0;

	return dwAddress;
}

std::string CMemory::GetModuleName(uintptr_t uAddress)
{
	HMODULE hModule;
	char cModuleName[MAX_PATH];

	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)uAddress, &hModule))
	{
		GetModuleBaseNameA(GetCurrentProcess(), hModule, cModuleName, MAX_PATH);
		return cModuleName;
	}

	return "Unknown";
}

uintptr_t CMemory::FindSignatureAtAddress(uintptr_t uAddress, const char* szPattern, uintptr_t uSkipAddress, bool* bRetFound)
{
	// Convert IDA-Style signature to a byte sequence
	const auto vPattern = PatternToInt(szPattern);
	return FindSignatureAtAddress(uAddress, vPattern, uSkipAddress, bRetFound);
}

uintptr_t CMemory::FindSignatureAtAddress(uintptr_t uAddress, std::vector<int> vPattern, uintptr_t uSkipAddress, bool* bRetFound)
{
	if (bRetFound)
		*bRetFound = false;

	if (!uAddress || vPattern.empty())
		return 0x0;
	for (const auto iByte : vPattern)
	{
		if (iByte < -1 || iByte > 0xFF)
			return 0x0;
	}

	if (const auto hMod = GetModuleHandleA(GetModuleName(uAddress).c_str()))
	{
		MODULEINFO lpModuleInfo;
		if (!GetModuleInformation(GetCurrentProcess(), hMod, &lpModuleInfo, sizeof(MODULEINFO)))
			return 0x0;

		const auto uModuleBase = reinterpret_cast<uintptr_t>(hMod);
		const auto iImageSize = static_cast<size_t>(lpModuleInfo.SizeOfImage);
		if (!iImageSize || uAddress < uModuleBase || uAddress - uModuleBase >= iImageSize)
			return 0x0;

		const auto iPatternSize = vPattern.size();
		const auto iRemainingSize = iImageSize - (uAddress - uModuleBase);
		if (iPatternSize > iRemainingSize)
			return 0x0;

		const int* iPatternBytes = vPattern.data();

		const auto pImageBytes = reinterpret_cast<byte*>(uAddress);
		for (size_t i = 0; i <= iRemainingSize - iPatternSize; ++i)
		{
			auto bFound = true;

			for (size_t j = 0; j < iPatternSize; ++j)
			{
				if (iPatternBytes[j] != -1 && pImageBytes[i + j] != iPatternBytes[j])
				{
					bFound = false;
					break;
				}
			}

			if (uSkipAddress && uSkipAddress == uintptr_t(&pImageBytes[i]))
			{
				if (bRetFound)
					*bRetFound = bFound;
				continue;
			}

			if (bFound)
				return uintptr_t(&pImageBytes[i]);
		}

		return 0x0;
	}

	return 0x0;
}

using CreateInterfaceFn = void*(*)(const char* pName, int* pReturnCode);

PVOID CMemory::FindInterface(const char* szModule, const char* szObject)
{
	const auto CreateInterface = GetModuleExport<CreateInterfaceFn>(szModule, "CreateInterface");
	return CreateInterface ? CreateInterface(szObject, nullptr) : nullptr;
}

std::string CMemory::GetModuleOffset(uintptr_t uAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(uAddress), &hModule))
		return std::format("{:#x}", uAddress);

	uintptr_t uBase = uintptr_t(hModule);
	if (char buffer[MAX_PATH]; GetModuleBaseName(GetCurrentProcess(), hModule, buffer, sizeof(buffer) / sizeof(char)))
		return std::format("{}+{:#x}", buffer, uAddress - uBase);

	return std::format("{:#x}+{:#x}", uBase, uAddress - uBase);
}

// AI failed me so i had to learn this shit by myself
// Some parts were taken from IDA-Fusion plugin
std::string CMemory::GenerateSignatureAtAddress(uintptr_t uAddress, size_t maxLength)
{
	// byte, iswildcar
	std::vector<std::pair<byte, bool>> vBytes;
	std::string sPattern;
	std::string sModule = GetModuleName(uAddress);

	uintptr_t uMinAddr = 0x0, uMaxAddr = 0x0;
	if (const auto hMod = GetModuleHandleA(sModule.c_str()))
	{
		uMinAddr = (uintptr_t)hMod;

		MODULEINFO lpModuleInfo;
		if (GetModuleInformation(GetCurrentProcess(), hMod, &lpModuleInfo, sizeof(MODULEINFO)) && lpModuleInfo.SizeOfImage)
			uMaxAddr = uMinAddr + lpModuleInfo.SizeOfImage;
	}

	if (!uMaxAddr)
		return {};

	// This is stupid but works
	auto fGetDispSize = [](uint32_t flags) -> byte
		{
			byte immOff = 0;
			if (flags & F_IMM64) immOff = 8;
			else if (flags & F_IMM32) immOff = 4;
			else if (flags & F_IMM16) immOff = 2;
			else if (flags & F_IMM8) immOff = 1;

			byte dispOff = 0;
			if (flags & F_DISP32) dispOff = 4;
			else if (flags & F_DISP16) dispOff = 2;
			else if (flags & F_DISP8) dispOff = 1;

			return dispOff + immOff;
		};

	uintptr_t uLastFound = uMinAddr;
	uintptr_t uCurrentAddr = uAddress;
	while (uCurrentAddr - uAddress < maxLength && uCurrentAddr < uMaxAddr)
	{
		hde64s tDecoded;
		byte uLen = hde64_disasm((LPVOID)(uCurrentAddr), &tDecoded);
		if (tDecoded.flags & F_ERROR)
		{
			sPattern = "Decoding error";
			break;
		}

		byte uDispSize = fGetDispSize(tDecoded.flags);
		byte uOffset = uLen - uDispSize;
		for (int i = 0; i < uLen; i++)
			vBytes.push_back({ *(byte*)(uCurrentAddr + i), uOffset && i >= uOffset });

		// Attempt to search for this signature, if nothing is found then we have a unique signature
		{
			std::string sTempPattern;
			std::vector<int> vTempPattern;
			sPattern.clear();

			for (int i = 0; i < vBytes.size(); i++)
			{
				auto [uByte, bWildcard] = vBytes.at(i);
				if (bWildcard)
				{
					sTempPattern.append("? ");
					if ((i + 1) != vBytes.size())
						vTempPattern.push_back(-1);
					continue;
				}
				sTempPattern.append(std::format("{:02X} ", uByte));
				vTempPattern.push_back(uByte);
			}
			sTempPattern.pop_back();

			// Try to find it at original address
			bool bFoundAtCurrent = false;
			FindSignatureAtAddress(uAddress, vTempPattern, uAddress, &bFoundAtCurrent);

			uintptr_t uFound = FindSignatureAtAddress(uLastFound, vTempPattern, uAddress);
			if (bFoundAtCurrent && !uFound)
			{
				sTempPattern.erase(sTempPattern.length() - uDispSize*2); // Erase last wildcard bytes
				sPattern = sTempPattern;
				break;
			}
			// Failsafe in case we can no longer find it at the original address
			else if (!bFoundAtCurrent)
				break;

			sPattern = std::format("Not a unique signature ({})", sTempPattern);

			// Update the last found address so we dont have to scan that region again
			uLastFound = uFound;
		}

		uCurrentAddr += uLen;
	}

	return sPattern;
}

uintptr_t CMemory::GetOffsetFromBase(uintptr_t uAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(uAddress), &hModule))
		return -1;

	uintptr_t uBase = uintptr_t(hModule);
	return uAddress - uBase;
}
