#include "../SDK/SDK.h"

#include "../Features/World/World.h"


MAKE_HOOK(CModelLoader_Map_LoadModel, S::CModelLoader_Map_LoadModel(), void,
	void* rcx, model_t* mod)
{
	DEBUG_RETURN(CModelLoader_Map_LoadModel, rcx, mod);

	CALL_ORIGINAL(rcx, mod);

	if (!I::BSPData || G::Unload)
		return;

	static bool s_bCaching = false;
	if (s_bCaching)
		return;

	if (I::BSPData->numbrushes <= 0 || I::BSPData->numcmodels <= 0)
		return;

	s_bCaching = true;
	F::World.CacheBoxBrushes();
	F::World.CachePlaneBrushes();
	F::World.CacheEntities();
	s_bCaching = false;
}

MAKE_HOOK(R_LevelInit, S::R_LevelInit(), void,
	)
{
	DEBUG_RETURN(R_LevelInit);

	CALL_ORIGINAL();

	if (!I::MDLCache || !I::MDLCache->m_bConnected || G::Unload)
		return;

	F::World.CacheProps();
}

MAKE_HOOK(CM_FreeMap, S::CM_FreeMap(), void,
	)
{
	DEBUG_RETURN(CM_FreeMap);

	CALL_ORIGINAL();

	F::World.Uncache();
}