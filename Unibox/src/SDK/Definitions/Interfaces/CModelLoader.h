#pragma once
#include "Interface.h"
#include "../Definitions.h"


class CModelLoader
{
public:
	model_t* FindModel(const char* name)
	{
		return S::CModelLoader_FindModel.Call<model_t*>(this, name);
	}
};
