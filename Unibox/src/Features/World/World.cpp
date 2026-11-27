#include "../World/World.h"

#include <numeric>

class CEntityEnumerator : public IPartitionEnumerator
{
public:
	IterationRetval_t EnumElement(IHandleEntity* pHandleEntity)
	{
		m_vEntities.push_back(pHandleEntity);
		return ITERATION_CONTINUE;
	}

	std::vector<IHandleEntity*>	m_vEntities = {};
};

void CWorld::SetNormalValidCallback(NormalValidCallback* pCallback)
{
	m_pNormalValidCallback = pCallback ? pCallback : &s_fNormalValid;
}

bool CWorld::FaceValid(const std::vector<Vec3>& vVertices, const Vec3* pNormal, const Vec3* pMins, const Vec3* pMaxs)
{
	if (!(*m_pVerticesValidCallback)(vVertices))
		return false;

	if (pMins && pMaxs && !(*m_pBoundsValidCallback)(vVertices, *pMins, *pMaxs))
		return false;

	if (pNormal && !(*m_pNormalValidCallback)(vVertices, *pNormal))
		return false;

	return true;
}



static CPhysConvex* s_aConvexes[1024];
static inline void GetFacesForConvex(CPhysConvex* pConvex, std::vector<Face_t>& vFaces, float flResize = 0.f, int8_t iType = FaceTypeEnum::None)
{
	CPolyhedron* pPolyhedron = I::PhysicsCollision->PolyhedronFromConvex(pConvex, true);
	if (!pPolyhedron)
		return;

	if (!flResize)
	{
		std::vector<std::pair<std::vector<int>, Vec3>> vTemp = {};
		for (int iPolygon = 0; iPolygon < pPolyhedron->iPolygonCount; iPolygon++)
		{
			Polyhedron_IndexedPolygon_t* pPolygon = &pPolyhedron->pPolygons[iPolygon];

			std::vector<int>* pVertices = nullptr;
			for (auto& [vVertices, vNormal] : vTemp)
			{
				if (pPolygon->polyNormal.Dot(vNormal) > 1 - CALC_EPSILON)
				{
					pVertices = &vVertices;
					break;
				}
			}
			if (!pVertices)
			{
				vTemp.emplace_back(std::vector<int>(), pPolygon->polyNormal);
				pVertices = &vTemp.back().first;
			}

			for (int iLineRef = 0; iLineRef < pPolygon->iIndexCount; iLineRef++)
			{
				Polyhedron_IndexedLineReference_t* pLineReference = &pPolyhedron->pIndices[pPolygon->iFirstIndex + iLineRef];
				Polyhedron_IndexedLine_t* pLine = &pPolyhedron->pLines[pLineReference->iLineIndex];
				int iVertex = pLine->iPointIndices[pLineReference->iEndPointIndex];
				if (std::find(pVertices->begin(), pVertices->end(), iVertex) == pVertices->end())
					pVertices->push_back(iVertex);
			}
		}

		for (auto& [vIndices, vNormal] : vTemp)
		{
			if (vIndices.size() < 3)
				continue;

			std::vector<Vec3> vVertices = {};
			for (auto& iVertex : vIndices)
				vVertices.push_back(pPolyhedron->pVertices[iVertex]);

			if (vVertices.size() > 3)
			{
				Vec3 vCenter = std::reduce(vVertices.begin(), vVertices.end()) / vVertices.size();
				Vec3 vDir1 = (vVertices.front() - vCenter).Normalized();
				Vec3 vDir2 = vDir1.Cross(vNormal);
				std::sort(vVertices.begin(), vVertices.end(), [&](const Vec3& a, const Vec3& b) -> bool
				{
					return atan2f(vDir2.Dot(a - vCenter), vDir1.Dot(a - vCenter)) < atan2f(vDir2.Dot(b - vCenter), vDir1.Dot(b - vCenter));
				});
			}

			vFaces.emplace_back(vVertices, vNormal, iType);
		}
	}
	else
	{	// rough approximation, not too sure why this is needed in the first place
		std::vector<Plane_t> vPlanes = {}; vPlanes.reserve(pPolyhedron->iPolygonCount);
		for (int iPolygon = 0; iPolygon < pPolyhedron->iPolygonCount; iPolygon++)
		{
			Polyhedron_IndexedPolygon_t* pPolygon = &pPolyhedron->pPolygons[iPolygon];

			if (pPolygon->iIndexCount)
			{
				Polyhedron_IndexedLineReference_t* pLineReference = &pPolyhedron->pIndices[pPolygon->iFirstIndex];
				Polyhedron_IndexedLine_t* pLine = &pPolyhedron->pLines[pLineReference->iLineIndex];
				Vec3& vVertex = pPolyhedron->pVertices[pLine->iPointIndices[pLineReference->iEndPointIndex]];
				vPlanes.emplace_back(pPolygon->polyNormal, pPolygon->polyNormal.Dot(vVertex + flResize * pPolygon->polyNormal));
			}
		}

		if (CPhysConvex* pConvex2 = I::PhysicsCollision->ConvexFromPlanes(reinterpret_cast<float*>(vPlanes.data()), int(vPlanes.size()), 0.f))
		{
			GetFacesForConvex(pConvex2, vFaces, 0.f, iType);
			I::PhysicsCollision->ConvexFree(pConvex2);
		}
	}

	pPolyhedron->Release();
}

static inline void GetFacesForCollision(CPhysCollide* pCollision, std::vector<Face_t>& vFaces, float flResize = 0.f, int8_t iType = FaceTypeEnum::None)
{
	int iConvexes = I::PhysicsCollision->GetConvexesUsedInCollideable(pCollision, s_aConvexes, sizeof(s_aConvexes) / sizeof(CPhysConvex*));
	for (int iConvex = 0; iConvex < iConvexes; iConvex++)
		GetFacesForConvex(s_aConvexes[iConvex], vFaces, flResize, iType);

	/*
	Vec3* pVertices; int iVertices = I::PhysicsCollision->CreateDebugMesh(pCollision, &pVertices); iVertices -= iVertices % 3;
	for (int iVertex = 0; iVertex < iVertices; iVertex += 3)
	{
		std::vector<Vec3> vVertices = { pVertices[iVertex], pVertices[iVertex + 1], pVertices[iVertex + 2] };
		Vec3 vNormal = (vVertices[0] - vVertices[2]).Cross(vVertices[0] - vVertices[1]).Normalized();

		vFaces.emplace_back(vVertices, vNormal, iType);
	}
	I::PhysicsCollision->DestroyDebugMesh(iVertices, pVertices);
	*/
}

void CWorld::Cache()
{
	if (!m_iNeedsCache)
		return;

	CacheBoxBrushes();
	CachePlaneBrushes();
	CacheProps();
	CacheEntities();
}

void CWorld::CacheBoxBrushes()
{
	if (!(m_iNeedsCache & FaceTypeEnum::BoxBrush))
		return;

	SDK::Output(__FUNCTION__, "Cache start", {}, OUTPUT_DEBUG);

	if (!I::BSPData || I::BSPData->numbrushes <= 0 || I::BSPData->numboxbrushes <= 0)
	{
		SDK::Output(__FUNCTION__, "BSP not ready, skipping", {}, OUTPUT_DEBUG);
		return;
	}

	m_iNeedsCache &= ~FaceTypeEnum::BoxBrush;

	for (int iBrush = 0; iBrush < I::BSPData->numbrushes; iBrush++)
	{
		cbrush_t* pBrush = &I::BSPData->map_brushes[iBrush];
		if (!pBrush->IsBox())
			continue;

		const int iBoxIdx = pBrush->GetBox();
		if (iBoxIdx < 0 || iBoxIdx >= I::BSPData->numboxbrushes)
			continue;

		cboxbrush_t* pBoxBrush = &I::BSPData->map_boxbrushes[iBoxIdx];

		Vec3 aVertices[8];
		for (int i = 0; i != 8; i++)
		{
			aVertices[i].x = i & (1 << 0) ? pBoxBrush->maxs.x : pBoxBrush->mins.x;
			aVertices[i].y = i & (1 << 1) ? pBoxBrush->maxs.y : pBoxBrush->mins.y;
			aVertices[i].z = i & (1 << 2) ? pBoxBrush->maxs.z : pBoxBrush->mins.z;
		}
		Vec3 aNormals[6] = {
			{ 1, 0, 0 },
			{ -1, 0, 0 },
			{ 0, 1, 0 },
			{ 0, -1, 0 },
			{ 0, 0, 1 },
			{ 0, 0, -1 }
		};

		for (int i = 0; i < 6; i++)
		{
			const int iSurfIdx = pBoxBrush->surfaceIndex[i];
			if (iSurfIdx == 0xFFFF)
				continue;
			if (I::BSPData->map_surfaces[iSurfIdx].flags & SURF_SKY)
				continue;

			std::vector<Vec3> vVertices;
			switch (i)
			{
			case 0: vVertices = { aVertices[1], aVertices[5], aVertices[7], aVertices[3] }; break;
			case 1: vVertices = { aVertices[2], aVertices[6], aVertices[4], aVertices[0] }; break;
			case 2: vVertices = { aVertices[3], aVertices[7], aVertices[6], aVertices[2] }; break;
			case 3: vVertices = { aVertices[0], aVertices[4], aVertices[5], aVertices[1] }; break;
			case 4: vVertices = { aVertices[6], aVertices[7], aVertices[5], aVertices[4] }; break;
			case 5: vVertices = { aVertices[0], aVertices[1], aVertices[3], aVertices[2] }; break;
			}
			Vec3& vNormal = aNormals[i];

			m_mFaceCache[pBrush].emplace_back(vVertices, vNormal, FaceTypeEnum::BoxBrush);
		}
	}

	SDK::Output(__FUNCTION__, "Cache finish", {}, OUTPUT_DEBUG);
}

void CWorld::CachePlaneBrushes()
{
	if (!(m_iNeedsCache & FaceTypeEnum::PlaneBrush))
		return;

	SDK::Output(__FUNCTION__, "Cache start", {}, OUTPUT_DEBUG);

	if (!I::BSPData || I::BSPData->numbrushes <= 0 || I::BSPData->numbrushsides <= 0)
	{
		SDK::Output(__FUNCTION__, "BSP not ready, skipping", {}, OUTPUT_DEBUG);
		return;
	}

	m_iNeedsCache &= ~FaceTypeEnum::PlaneBrush;

	for (int iBrush = 0; iBrush < I::BSPData->numbrushes; iBrush++)
	{
		cbrush_t* pBrush = &I::BSPData->map_brushes[iBrush];
		if (pBrush->IsBox())
			continue;

		std::vector<Plane_t> vPlanes = {}; vPlanes.reserve(pBrush->numsides);
		for (int iSide = 0; iSide != pBrush->numsides; iSide++)
		{
			const int iSideIdx = pBrush->firstbrushside + iSide;
			if (iSideIdx < 0 || iSideIdx >= I::BSPData->numbrushsides)
				continue;

			cbrushside_t* pBrushSide = &I::BSPData->map_brushsides[iSideIdx];
			const int iSurfIdx = pBrushSide->surfaceIndex;
			if (iSurfIdx == 0xFFFF)
				continue;
			if (I::BSPData->map_surfaces[iSurfIdx].flags & SURF_SKY)
				continue;

			cplane_t* pPlane = pBrushSide->plane;
			if (!pPlane)
				continue;
			vPlanes.emplace_back(pPlane->normal, pPlane->dist);
		}
		if (vPlanes.empty())
			continue;

		CPhysConvex* pConvex = I::PhysicsCollision->ConvexFromPlanes(reinterpret_cast<float*>(vPlanes.data()), int(vPlanes.size()), 0.f);
		if (!pConvex)
			continue;

		GetFacesForConvex(pConvex, m_mFaceCache[pBrush], 0.f, FaceTypeEnum::PlaneBrush);
		I::PhysicsCollision->ConvexFree(pConvex);
	}

	SDK::Output(__FUNCTION__, "Cache finish", {}, OUTPUT_DEBUG);
}

void CWorld::CacheProps()
{
	if (!(m_iNeedsCache & FaceTypeEnum::Prop))
		return;

	SDK::Output(__FUNCTION__, "Cache start", {}, OUTPUT_DEBUG);

	if (!I::MDLCache || !I::MDLCache->m_bConnected)
	{
		SDK::Output(__FUNCTION__, "MDLCache not ready, skipping", {}, OUTPUT_DEBUG);
		return;
	}

	m_iNeedsCache &= ~FaceTypeEnum::Prop;

	for (int i = I::MDLCache->m_MDLDict.First(); i != I::MDLCache->m_MDLDict.InvalidIndex(); i = I::MDLCache->m_MDLDict.Next(i))
	{
		studiodata_t* pStudioData = I::MDLCache->m_MDLDict[i];
		if (!pStudioData)
			continue;

		vcollide_t* pCollide = &pStudioData->m_VCollisionData;
		if (m_mFaceCache.contains(pCollide))
			continue;

		if (pCollide->solidCount <= 0)
			continue;

		for (int iSolid = 0; iSolid < pCollide->solidCount; iSolid++)
		{
			CPhysCollide* pCollision = pCollide->solids[iSolid];
			if (!pCollision)
				continue;

			GetFacesForCollision(pCollision, m_mFaceCache[pCollide], 0.f, FaceTypeEnum::Prop);
		}
	}

	SDK::Output(__FUNCTION__, "Cache finish", {}, OUTPUT_DEBUG);
}

void CWorld::CacheEntities()
{
	if (!(m_iNeedsCache & FaceTypeEnum::Entity))
		return;

	SDK::Output(__FUNCTION__, "Cache start", {}, OUTPUT_DEBUG);

	if (!I::BSPData || I::BSPData->numcmodels <= 0)
	{
		SDK::Output(__FUNCTION__, "BSP not ready, skipping", {}, OUTPUT_DEBUG);
		return;
	}

	m_iNeedsCache &= ~FaceTypeEnum::Entity;

	for (int iCModel = 0; iCModel < I::BSPData->numcmodels; iCModel++)
	{
		cmodel_t* pCModel = &I::BSPData->map_cmodels[iCModel];
		vcollide_t* pCollide = &pCModel->vcollisionData;

		if (pCollide->solidCount <= 0)
			continue;

		for (int iSolid = 0; iSolid < pCollide->solidCount; iSolid++)
		{
			CPhysCollide* pCollision = pCollide->solids[iSolid];
			if (!pCollision)
				continue;

			GetFacesForCollision(pCollision, m_mFaceCache[pCollide], 0.5f, FaceTypeEnum::Entity);
		}
	}

	SDK::Output(__FUNCTION__, "Cache finish", {}, OUTPUT_DEBUG);
}



void CWorld::Uncache()
{
	m_iQueryCacheTick = -1;
	m_uQueryCacheCount = 0;
	m_uQueryCacheNext = 0;
	for (auto& tEntry : m_aQueryCache)
		tEntry.m_vRawFaces.clear();

	if (m_iNeedsCache == FaceTypeEnum::Cache)
		return;

	m_mFaceCache.clear();

	m_iNeedsCache = FaceTypeEnum::Cache;
}

void CWorld::CollectFacesInAABB(const Vec3& vMins, const Vec3& vMaxs, int iMask, ITraceFilter* pFilter, int iFlags, bool bCheckNormal, std::vector<Face_t>& vOut)
{
	if ((iFlags & (FaceTypeEnum::BoxBrush | FaceTypeEnum::PlaneBrush)) && I::BSPData && I::BSPData->numbrushes > 0)
	{
		m_vBrushStorage.resize(I::BSPData->numbrushes);
		CUtlVector<int> vBrushes(m_vBrushStorage.data(), static_cast<int>(m_vBrushStorage.size()));
		I::EngineTrace->GetBrushesInAABB(vMins, vMaxs, &vBrushes, iMask);
		for (int iBrush = 0; iBrush < vBrushes.Count(); iBrush++)
		{
			cbrush_t* pBrush = &I::BSPData->map_brushes[vBrushes[iBrush]];
			auto it = m_mFaceCache.find(pBrush);
			if (it == m_mFaceCache.end())
				continue;

			bool bBox = pBrush->IsBox();
			if (!(iFlags & (bBox ? FaceTypeEnum::BoxBrush : FaceTypeEnum::PlaneBrush)))
				continue;

			auto& vCache = it->second;
			if (bBox && bCheckNormal)
			{
				for (auto& tFace : vCache)
				{
					if (FaceValid(tFace.m_vVertices, &tFace.m_vNormal, &vMins, &vMaxs))
					{
						vOut.push_back(tFace);
						for (auto& vVertex : vOut.back().m_vVertices)
							vVertex = vVertex.Clamp(vMins, vMaxs);
					}
				}
			}
			else
			{
				for (auto& tFace : vCache)
				{
					if (FaceValid(tFace.m_vVertices, bCheckNormal ? &tFace.m_vNormal : nullptr, &vMins, &vMaxs))
						vOut.push_back(tFace);
				}
			}
		}
	}

	if (iFlags & FaceTypeEnum::Displacement && *I::DispCollTrees)
	{
		std::vector<Vec3>& vVertices = m_vDispVertices;
		for (int iDisplacement = 0; iDisplacement < *I::DispCollTreeCount; iDisplacement++)
		{
			CDispCollTree* pDispTree = &(*I::DispCollTrees)[iDisplacement];
			if (!Math::IsBoxIntersectingBox(vMins, vMaxs, pDispTree->m_mins, pDispTree->m_maxs))
				continue;

			int iFaces = pDispTree->GetFaceCount();
			for (int iFace = 0; iFace < iFaces; iFace++)
			{
				CDispCollTri* pTri = &pDispTree->m_aTris[iFace];

				vVertices[0] = pDispTree->m_aVerts[pTri->GetVert(0)];
				vVertices[1] = pDispTree->m_aVerts[pTri->GetVert(1)];
				vVertices[2] = pDispTree->m_aVerts[pTri->GetVert(2)];

				if (FaceValid(vVertices, bCheckNormal ? &pTri->m_vecNormal : nullptr, &vMins, &vMaxs))
					vOut.emplace_back(vVertices, pTri->m_vecNormal, FaceTypeEnum::Displacement);
			}
		}
	}

	if (iFlags & (FaceTypeEnum::Entity | FaceTypeEnum::Prop))
	{
		CEntityEnumerator tEnumerator; I::SpatialPartition->EnumerateElementsInBox(I::EngineTrace->SpatialPartitionMask(), vMins, vMaxs, false, &tEnumerator);
		for (auto pHandleEntity : tEnumerator.m_vEntities)
		{
			if (!pHandleEntity)
				continue;
			if (pFilter && !pFilter->ShouldHitEntity(pHandleEntity, iMask))
				continue;

			bool bProp = I::StaticPropMgr->IsStaticProp(pHandleEntity);
			if (!(iFlags & (bProp ? FaceTypeEnum::Prop : FaceTypeEnum::Entity)))
				continue;

			ICollideable* pCollideable = nullptr;
			if (!bProp)
			{
				CBaseEntity* pEntity = reinterpret_cast<CBaseEntity*>(pHandleEntity);
				if (!pEntity->GetAbsVelocity().IsZero())
					continue;

				pCollideable = pEntity->GetCollideable();
			}
			else
				pCollideable = I::StaticPropMgr->GetStaticProp(pHandleEntity);
			if (!pCollideable || !pCollideable->IsSolid())
				continue;

			vcollide_t* pCollide = I::ModelInfoClient->GetVCollide(pCollideable->GetCollisionModel());
			auto it = m_mFaceCache.find(pCollide);
			if (it == m_mFaceCache.end())
				continue;

			const bool bHasAngles = bool(pCollideable->GetCollisionAngles());
			Vec3 vCollisionOrigin;
			matrix3x4 mTransform;
			if (!bHasAngles)
				vCollisionOrigin = pCollideable->GetCollisionOrigin();
			else
				Math::MatrixCopy(pCollideable->CollisionToWorldTransform(), mTransform);

			for (auto& tFace : it->second)
			{
				m_vTempVertices = tFace.m_vVertices;
				Vec3 vNormal = tFace.m_vNormal;
				if (!bHasAngles)
				{
					for (auto& vVertex : m_vTempVertices)
						vVertex += vCollisionOrigin;
				}
				else
				{
					for (auto& vVertex : m_vTempVertices)
						vVertex = Math::VectorTransform(vVertex, mTransform);
					vNormal = (m_vTempVertices[0] - m_vTempVertices[2]).Cross(m_vTempVertices[0] - m_vTempVertices[1]).Normalized();
				}

				if (FaceValid(m_vTempVertices, bCheckNormal ? &vNormal : nullptr, &vMins, &vMaxs))
					vOut.emplace_back(m_vTempVertices, vNormal, tFace.m_iType, pHandleEntity);
			}
		}
	}
}

void CWorld::GetFacesInAABB(const Vec3& vMins, const Vec3& vMaxs, int iMask, ITraceFilter* pFilter, int iFlags, bool bCacheQuery, std::vector<Face_t>& vFaces)
{
	Cache();

	vFaces.clear();
	if (!bCacheQuery)
	{
		CollectFacesInAABB(vMins, vMaxs, iMask, pFilter, iFlags, true, vFaces);
		return;
	}

	const int iTick = I::GlobalVars ? I::GlobalVars->tickcount : -1;
	if (m_iQueryCacheTick != iTick)
	{
		m_iQueryCacheTick = iTick;
		m_uQueryCacheCount = 0;
		m_uQueryCacheNext = 0;
	}

	const bool bHasFilter = pFilter != nullptr;
	AABBCacheEntry_t* pEntry = nullptr;
	for (size_t i = 0; i < m_uQueryCacheCount; i++)
	{
		auto& tEntry = m_aQueryCache[i];
		if (tEntry.m_iMask == iMask
			&& tEntry.m_iFlags == iFlags
			&& tEntry.m_bHasFilter == bHasFilter
			&& tEntry.m_vMins == vMins
			&& tEntry.m_vMaxs == vMaxs)
		{
			pEntry = &tEntry;
			break;
		}
	}

	if (!pEntry)
	{
		pEntry = &m_aQueryCache[m_uQueryCacheNext];
		m_uQueryCacheNext = (m_uQueryCacheNext + 1) % m_aQueryCache.size();
		if (m_uQueryCacheCount < m_aQueryCache.size())
			m_uQueryCacheCount++;

		pEntry->m_vMins = vMins;
		pEntry->m_vMaxs = vMaxs;
		pEntry->m_iMask = iMask;
		pEntry->m_iFlags = iFlags;
		pEntry->m_bHasFilter = bHasFilter;
		pEntry->m_vRawFaces.clear();
		CollectFacesInAABB(vMins, vMaxs, iMask, pFilter, iFlags, false, pEntry->m_vRawFaces);
	}

	vFaces.reserve(pEntry->m_vRawFaces.size());
	for (const auto& tFace : pEntry->m_vRawFaces)
	{
		if ((*m_pNormalValidCallback)(tFace.m_vVertices, tFace.m_vNormal))
		{
			vFaces.push_back(tFace);
			if (tFace.m_iType == FaceTypeEnum::BoxBrush)
			{
				for (auto& vVertex : vFaces.back().m_vVertices)
					vVertex = vVertex.Clamp(vMins, vMaxs);
			}
		}
	}
}

std::vector<Face_t> CWorld::GetFacesInAABB(const Vec3& vMins, const Vec3& vMaxs, int iMask, ITraceFilter* pFilter, int iFlags, bool bCacheQuery)
{
	std::vector<Face_t> vFaces;
	GetFacesInAABB(vMins, vMaxs, iMask, pFilter, iFlags, bCacheQuery, vFaces);
	return vFaces;
}

void CWorld::ProbePlayerGeometry(const Vec3& vOrigin, float flEyeYawDeg, bool bDucked, ITraceFilter* pFilter, float& flGroundDist, Vec3& vGroundNormal, float& flCeilingDist, float aWallDists[8], float aLedgeDepths[8], bool& bWallContact)
{
	CTraceFilterWorldAndPropsOnly tDefaultFilter;
	ITraceFilter* pUseFilter = pFilter ? pFilter : &tDefaultFilter;

	const float flHullMaxZ = bDucked ? 62.f : 82.f;
	const Vec3 vMins = { -24.f, -24.f, 0.f };
	const Vec3 vMaxs = { 24.f, 24.f, flHullMaxZ };

	const Vec3 vGroundStart = { vOrigin.x, vOrigin.y, vOrigin.z + 2.f };
	const Vec3 vGroundEnd = { vOrigin.x, vOrigin.y, vOrigin.z - 512.f };
	CGameTrace tGroundTrace = {};
	SDK::TraceHull(vGroundStart, vGroundEnd, vMins, vMaxs, MASK_PLAYERSOLID, pUseFilter, &tGroundTrace);
	if (tGroundTrace.startsolid)
	{
		flGroundDist = 0.f;
		vGroundNormal = { 0.f, 0.f, 1.f };
	}
	else if (tGroundTrace.fraction < 1.f)
	{
		const float flHitZ = vGroundStart.z + (vGroundEnd.z - vGroundStart.z) * tGroundTrace.fraction;
		flGroundDist = std::max(vOrigin.z - flHitZ, 0.f);
		vGroundNormal = tGroundTrace.plane.normal;
	}
	else
	{
		flGroundDist = 512.f;
		vGroundNormal = {};
	}

	const Vec3 vCeilStart = vOrigin;
	const Vec3 vCeilEnd = { vOrigin.x, vOrigin.y, vOrigin.z + 512.f };
	CGameTrace tCeilTrace = {};
	SDK::TraceHull(vCeilStart, vCeilEnd, vMins, vMaxs, MASK_PLAYERSOLID, pUseFilter, &tCeilTrace);
	if (tCeilTrace.startsolid)
		flCeilingDist = 0.f;
	else if (tCeilTrace.fraction < 1.f)
		flCeilingDist = std::max(512.f * tCeilTrace.fraction, 0.f);
	else
		flCeilingDist = 512.f;

	const Vec3 vBoxMins = { vOrigin.x - 260.f, vOrigin.y - 260.f, vOrigin.z - 520.f };
	const Vec3 vBoxMaxs = { vOrigin.x + 260.f, vOrigin.y + 260.f, vOrigin.z + 520.f };
	static thread_local std::vector<Face_t> vFaces;
	GetFacesInAABB(vBoxMins, vBoxMaxs, MASK_PLAYERSOLID, pUseFilter, FaceTypeEnum::All, true, vFaces);

	auto fTraceFacesRay = [&](const Vec3& vStart, const Vec3& vEnd, bool& bStartSolid) -> float
	{
		bStartSolid = false;
		if (vFaces.empty())
		{
			CGameTrace tTrace = {};
			SDK::Trace(vStart, vEnd, MASK_PLAYERSOLID, pUseFilter, &tTrace);
			bStartSolid = tTrace.startsolid;
			return tTrace.startsolid ? 0.f : tTrace.fraction;
		}

		const Vec3 vDir = vEnd - vStart;
		float flBest = 1.f;
		for (const auto& tFace : vFaces)
		{
			const size_t n = tFace.m_vVertices.size();
			if (n < 3)
				continue;

			const float flDenom = tFace.m_vNormal.Dot(vDir);
			if (fabsf(flDenom) < 1e-6f)
				continue;

			const float t = tFace.m_vNormal.Dot(tFace.m_vVertices[0] - vStart) / flDenom;
			if (t < 0.f || t >= flBest)
				continue;

			const Vec3 vHit = vStart + vDir * t;
			bool bHasPos = false, bHasNeg = false;
			for (size_t i = 0; i < n; ++i)
			{
				const Vec3& vA = tFace.m_vVertices[i];
				const Vec3& vB = tFace.m_vVertices[(i + 1) % n];
				const float flSide = (vB - vA).Cross(vHit - vA).Dot(tFace.m_vNormal);
				if (flSide < -0.05f)
					bHasNeg = true;
				else if (flSide > 0.05f)
					bHasPos = true;
				if (bHasNeg && bHasPos)
					break;
			}
			if (!(bHasNeg && bHasPos))
				flBest = t;
		}
		if (flBest <= 1e-5f)
			bStartSolid = true;
		return flBest;
	};

	const Vec3 vWaist = { vOrigin.x, vOrigin.y, vOrigin.z + 36.f };
	const float flYawRad = Math::Deg2Rad(flEyeYawDeg);
	float flMinWall = 256.f;
	for (int k = 0; k < 8; ++k)
	{
		const float flAng = flYawRad + float(k) * (Math::PI * 0.25f);
		const float flDx = cosf(flAng);
		const float flDy = sinf(flAng);
		const Vec3 vWallEnd = { vWaist.x + flDx * 256.f, vWaist.y + flDy * 256.f, vWaist.z };
		bool bWallSolid = false;
		const float flWallFrac = fTraceFacesRay(vWaist, vWallEnd, bWallSolid);
		const float flWallDist = bWallSolid ? 0.f : (flWallFrac < 1.f ? flWallFrac * 256.f : 256.f);
		aWallDists[k] = flWallDist;
		if (flWallDist < flMinWall)
			flMinWall = flWallDist;

		if (flWallDist < 64.f)
		{
			aLedgeDepths[k] = -1.f;
		}
		else
		{
			const Vec3 vLedgeStart = { vOrigin.x + flDx * 64.f, vOrigin.y + flDy * 64.f, vOrigin.z + 36.f };
			const Vec3 vLedgeEnd = { vLedgeStart.x, vLedgeStart.y, vOrigin.z - 512.f };
			bool bLedgeSolid = false;
			const float flLedgeFrac = fTraceFacesRay(vLedgeStart, vLedgeEnd, bLedgeSolid);
			if (bLedgeSolid)
				aLedgeDepths[k] = -36.f;
			else if (flLedgeFrac < 1.f)
			{
				const float flHitZ = vLedgeStart.z + (vLedgeEnd.z - vLedgeStart.z) * flLedgeFrac;
				aLedgeDepths[k] = vOrigin.z - flHitZ;
			}
			else
				aLedgeDepths[k] = 512.f;
		}
	}
	bWallContact = flMinWall < 26.5f;
}

#ifdef WORLD_DEBUG
void CWorld::DrawFace(const Face_t& tFace, int iFlags, std::optional<Color_t> tColor)
{
	if (!tColor)
	{
		switch (tFace.m_iType)
		{
		case FaceTypeEnum::BoxBrush: tColor = Vars::World::BoxBrush.Value; break;
		case FaceTypeEnum::PlaneBrush: tColor = Vars::World::PlaneBrush.Value; break;
		case FaceTypeEnum::Displacement: tColor = Vars::World::Displacement.Value; break;
		case FaceTypeEnum::Prop: tColor = Vars::World::Prop.Value; break;
		case FaceTypeEnum::Entity: tColor = Vars::World::Entity.Value; break;
		default: tColor = { 255, 255, 255 }; break;
		}
	}

	if (iFlags & (DrawTypeEnum::Points | DrawTypeEnum::Edges))
	{
		for (int i = 0; i < tFace.m_vVertices.size(); i++)
		{
			const Vec3& vVertex1 = tFace.m_vVertices[i], &vVertex2 = tFace.m_vVertices[(i + 1) % tFace.m_vVertices.size()];

			if (iFlags & DrawTypeEnum::Points)
				G::BoxStorage.emplace_back(vVertex1, Vec3::Get(-1), Vec3::Get(1), Vec3(0, 0, 0), I::GlobalVars->curtime + 60.f, *tColor, Color_t(0, 0, 0, 0), true);
			if (iFlags & DrawTypeEnum::Edges)
				G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vVertex1, vVertex2), I::GlobalVars->curtime + 60.f, *tColor, true);
		}
	}

	if (iFlags & DrawTypeEnum::Faces)
	{
		for (int i = 0; ++i < tFace.m_vVertices.size() - 1;)
		{
			const Vec3& vVertex1 = tFace.m_vVertices[0], &vVertex2 = tFace.m_vVertices[i], &vVertex3 = tFace.m_vVertices[i + 1];
			G::TriangleStorage.emplace_back(std::array<Vec3, 3>({ vVertex1, vVertex2, vVertex3 }), I::GlobalVars->curtime + 60.f, tColor->Alpha(50), true);
		}
	}

	if (iFlags & DrawTypeEnum::Normals)
	{
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tFace.m_vVertices[0], tFace.m_vVertices[0] + tFace.m_vNormal * 10), I::GlobalVars->curtime + 60.f, tColor->Inverse(), true);
		G::BoxStorage.emplace_back(tFace.m_vVertices[0] + tFace.m_vNormal * 10, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + 60.f, tColor->Inverse(), Color_t(0, 0, 0, 0), true);
	}
}
#endif
