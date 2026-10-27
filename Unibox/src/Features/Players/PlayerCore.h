#pragma once
#include "../../SDK/SDK.h"

class CPlayerlistCore
{
private:
	void SavePlayerlist();
	void LoadPlayerlist();
	void SaveCheaterlist();
	void LoadCheaterlist();

public:
	void Run();
};

ADD_FEATURE(CPlayerlistCore, PlayerCore);