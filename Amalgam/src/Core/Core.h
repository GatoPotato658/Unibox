#pragma once
#include "../Utils/Macros/Macros.h"

#include <sstream>
#include <fstream>
#include <atomic>

class CCore
{
private:
	bool m_bFailed = false;
	bool m_bFailed2 = false;
	std::stringstream m_ssFailStream;

public:
	void Load();
	void Loop();
	void Unload();

	void AppendFailText(const char* sMessage);
	void LogFailText();

	std::atomic_bool m_bUnload = false;
};

ADD_FEATURE_CUSTOM(CCore, Core, U);
