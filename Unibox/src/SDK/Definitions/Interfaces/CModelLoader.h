#pragma once
#include "Interface.h"
#include "../Definitions.h"

MAKE_SIGNATURE(CModelLoader_FindModel, "engine.dll", "40 53 41 56 48 83 EC ? 4C 8B F2", 0x0);

class CModelLoader
{
public:
	model_t* FindModel(const char* name)
	{
		return S::CModelLoader_FindModel.Call<model_t*>(this, name);
	}
};

MAKE_INTERFACE_SIGNATURE(CModelLoader, ModelLoader, "engine.dll", "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B D8 8B 40", 0x0, 0);