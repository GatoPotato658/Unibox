#include <Windows.h>
#include "Core/Core.h"
#include "Utils/ExceptionHandler/ExceptionHandler.h"

DWORD WINAPI MainThread(LPVOID lpParam)
{
	U::ExceptionHandler.Initialize(lpParam);
	U::Core.Load();
	U::Core.Loop();
	U::Core.Unload();
	U::ExceptionHandler.Unload();

	FreeLibraryAndExitThread(static_cast<HMODULE>(lpParam), EXIT_SUCCESS);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (const auto hThread = CreateThread(nullptr, 0, MainThread, hinstDLL, 0, nullptr))
			CloseHandle(hThread);
	}

	return TRUE;
}
