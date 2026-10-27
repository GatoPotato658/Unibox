#pragma once
#include "../Macros/Macros.h"
#include <Windows.h>

class CExceptionHandler
{
public:
	void Initialize(LPVOID lpParam = nullptr);
	void Unload();
};

ADD_FEATURE_CUSTOM(CExceptionHandler, ExceptionHandler, U);