#pragma once
#include "ICollideable.h"
#include "../../../Utils/Signatures/Signatures.h"


typedef unsigned short SpatialPartitionHandle_t;

class CCollisionProperty : public ICollideable
{
public:
	CBaseEntity* m_pOuter;
	Vec3 m_vecMinsPreScaled;
	Vec3 m_vecMaxsPreScaled;
	Vec3 m_vecMins;
	Vec3 m_vecMaxs;
	float m_flRadius;
	unsigned short m_usSolidFlags;
	SpatialPartitionHandle_t m_Partition;
	unsigned char m_nSurroundType;
	unsigned char m_nSolidType;
	unsigned char m_triggerBloat;
	bool m_bUniformTriggerBloat;
	Vec3 m_vecSpecifiedSurroundingMinsPreScaled;
	Vec3 m_vecSpecifiedSurroundingMaxsPreScaled;
	Vec3 m_vecSpecifiedSurroundingMins;
	Vec3 m_vecSpecifiedSurroundingMaxs;
	Vec3 m_vecSurroundingMins;
	Vec3 m_vecSurroundingMaxs;

public:
	SIGNATURE_ARGS(SetCollisionBounds, void, CCollisionProperty, (const Vec3& mins, const Vec3& maxs), this, std::ref(mins), std::ref(maxs));
	SIGNATURE_ARGS(CalcNearestPoint, void, CCollisionProperty, (const Vec3& vecWorldPt, Vec3* pVecNearestWorldPt), this, std::ref(vecWorldPt), pVecNearestWorldPt);
};