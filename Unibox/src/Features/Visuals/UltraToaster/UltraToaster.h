#pragma once

#ifndef TEXTMODE
#include "../../../SDK/SDK.h"

#include <algorithm>

namespace UltraToaster
{
	inline bool IsActive()
	{
		return Vars::Visuals::UltraToaster::Enabled.Value && !G::Unload
			&& I::EngineClient && I::EngineClient->IsInGame()
			&& !I::EngineClient->IsPlayingDemo();
	}

	inline int LowerLod(const studiohwdata_t* pHardware, int iCurrent, int iBias)
	{
		if (!pHardware || !pHardware->m_pLODs || pHardware->m_NumStudioMeshes <= 0
			|| pHardware->m_NumLODs <= 1 || pHardware->m_NumLODs > MAX_NUM_LODS
			|| iCurrent < 0 || iCurrent < pHardware->m_RootLOD || iBias <= 0)
			return iCurrent;

		int iLastLod = pHardware->m_NumLODs - 1;
		if (pHardware->m_pLODs[iLastLod].m_SwitchPoint < 0.f)
			--iLastLod;
		if (iCurrent >= iLastLod)
			return iCurrent;

		for (int iLod = std::min(iCurrent + std::min(iBias, MAX_NUM_LODS - 1), iLastLod); iLod > iCurrent; --iLod)
		{
			const auto* pMeshes = pHardware->m_pLODs[iLod].m_pMeshData;
			if (pMeshes && std::any_of(pMeshes, pMeshes + pHardware->m_NumStudioMeshes,
				[](const studiomeshdata_t& tMesh) { return tMesh.m_NumGroup > 0; }))
				return iLod;
		}
		return iCurrent;
	}
}
#endif
