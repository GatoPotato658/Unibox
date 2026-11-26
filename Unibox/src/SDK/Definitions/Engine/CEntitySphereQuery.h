#pragma once
#include "../Definitions.h"
#include "../../../Utils/Signatures/Signatures.h"

#pragma warning (push)
#pragma warning (disable : 26495)


#define MAX_SPHERE_QUERY	512

class CBaseEntity;

// credits to KGB
class CEntitySphereQuery
{
public:
	CEntitySphereQuery(const Vec3& center, const float radius, const int flagMask = 0, const int partitionMask = PARTITION_CLIENT_NON_STATIC_EDICTS)
	{
		S::CEntitySphereQuery.Call<CEntitySphereQuery*>(this, std::ref(center), radius, flagMask, partitionMask);
	}

	inline CBaseEntity* GetCurrentEntity()
	{
		return (m_nListIndex < m_nListCount) ? m_pList[m_nListIndex] : nullptr;
	}

	inline void NextEntity()
	{
		m_nListIndex++;
	}

private:
	int m_nListIndex, m_nListCount;
	CBaseEntity* m_pList[MAX_SPHERE_QUERY];
};

#pragma warning (pop)