#pragma once

#ifdef _WIN32
#include "../../../SDK/SDK.h"
#else
#include "../../../Utils/Macros/Macros.h"
struct DrawModelState_t;
struct ModelRenderInfo_t;
using matrix3x4 = float[3][4];
struct ChamsMaterial_t;
#endif

struct IDirect3DDevice9;

class CD3DMaterials
{
public:
	void Initialize(IDirect3DDevice9* pDevice);
	bool Draw(const DrawModelState_t& state, const ModelRenderInfo_t& info, const matrix3x4* bones, const ChamsMaterial_t& params, bool bOccluded, bool bViewmodel = false);
	void Unload();
};

ADD_FEATURE(CD3DMaterials, D3DMaterials);
