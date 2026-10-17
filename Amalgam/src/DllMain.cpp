#include <Windows.h>
#include "Core/Core.h"
#include "Utils/ExceptionHandler/ExceptionHandler.h"

static PRUNTIME_FUNCTION s_pFunctionTable = nullptr;
static bool s_bManualMapped = false;
static uintptr_t s_uModuleBase = 0;

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);

static uintptr_t GetCurrentModuleBase(LPVOID lpParam)
{
	if (lpParam)
		return reinterpret_cast<uintptr_t>(lpParam);

	MEMORY_BASIC_INFORMATION mbi{};
	if (VirtualQuery(reinterpret_cast<LPCVOID>(&DllMain), &mbi, sizeof(mbi)))
		return reinterpret_cast<uintptr_t>(mbi.AllocationBase);

	return 0;
}

static void InitializeManualMap(uintptr_t uBase)
{
	if (!uBase || s_uModuleBase)
		return;

	s_uModuleBase = uBase;

	HMODULE hModule = nullptr;
	const bool bIsLdrModule = GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(uBase),
		&hModule
	) && hModule == reinterpret_cast<HMODULE>(uBase);

	if (!bIsLdrModule)
	{
		s_bManualMapped = true;

		const auto pDosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(uBase);
		if (pDosHeader->e_magic == IMAGE_DOS_SIGNATURE && pDosHeader->e_lfanew > 0 && pDosHeader->e_lfanew < 0x1000)
		{
			const auto pNtHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(uBase + pDosHeader->e_lfanew);
			if (pNtHeaders->Signature == IMAGE_NT_SIGNATURE && pNtHeaders->OptionalHeader.SizeOfImage)
			{
				const auto& exceptionDir = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
				if (exceptionDir.VirtualAddress && exceptionDir.Size && exceptionDir.VirtualAddress < pNtHeaders->OptionalHeader.SizeOfImage && exceptionDir.Size <= pNtHeaders->OptionalHeader.SizeOfImage - exceptionDir.VirtualAddress)
				{
					const DWORD dwEntryCount = exceptionDir.Size / sizeof(RUNTIME_FUNCTION);
					auto pFunctionTable = reinterpret_cast<PRUNTIME_FUNCTION>(uBase + exceptionDir.VirtualAddress);
					if (dwEntryCount && RtlAddFunctionTable(pFunctionTable, dwEntryCount, uBase))
						s_pFunctionTable = pFunctionTable;
				}
			}
		}
	}
}

static void CleanupManualMap()
{
	if (s_pFunctionTable)
	{
		RtlDeleteFunctionTable(s_pFunctionTable);
		s_pFunctionTable = nullptr;
	}
}

DWORD WINAPI MainThread(LPVOID lpParam)
{
	const uintptr_t uBase = s_uModuleBase ? s_uModuleBase : GetCurrentModuleBase(lpParam);
	if (!s_uModuleBase)
		InitializeManualMap(uBase);

	U::ExceptionHandler.Initialize(reinterpret_cast<LPVOID>(uBase));
	U::Core.Load();
	U::Core.Loop();
	U::Core.Unload();
	U::ExceptionHandler.Unload();
	CleanupManualMap();

	if (s_bManualMapped || !uBase)
		ExitThread(EXIT_SUCCESS);
	else
		FreeLibraryAndExitThread(reinterpret_cast<HMODULE>(uBase), EXIT_SUCCESS);

	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		const uintptr_t uBase = GetCurrentModuleBase(hinstDLL);
		InitializeManualMap(uBase);

		if (const auto hThread = CreateThread(nullptr, 0, MainThread, reinterpret_cast<LPVOID>(uBase), 0, nullptr))
			CloseHandle(hThread);
	}

	return TRUE;
}
