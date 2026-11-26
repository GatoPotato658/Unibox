#pragma once
#include "../../Definitions/Types.h"
#include "../../Definitions/Entities/CBaseEntity.h"


enum ParticleAttachment_t
{
	PATTACH_ABSORIGIN = 0,			// Create at absorigin, but don't follow
	PATTACH_ABSORIGIN_FOLLOW,		// Create at absorigin, and update to follow the entity
	PATTACH_CUSTOMORIGIN,			// Create at a custom origin, but don't follow
	PATTACH_POINT,					// Create on attachment point, but don't follow
	PATTACH_POINT_FOLLOW,			// Create on attachment point, and update to follow the entity
	PATTACH_WORLDORIGIN,			// Used for control points that don't attach to an entity
	PATTACH_ROOTBONE_FOLLOW,		// Create at the root bone of the entity, and update to follow
	MAX_PATTACH_TYPES,
};

struct te_tf_particle_effects_colors_t
{
	Vec3 m_vecColor1;
	Vec3 m_vecColor2;
};

struct te_tf_particle_effects_control_point_t
{
	ParticleAttachment_t m_eParticleAttachment;
	Vec3 m_vecOffset;
};

class CEffectData
{
public:
	Vec3									m_vOrigin;
	Vec3									m_vStart;
	Vec3									m_vNormal;
	Vec3									m_vAngles;
	int										m_fFlags;
	int										m_nEntIndex;
	float									m_flScale;
	float									m_flMagnitude;
	float									m_flRadius;
	int										m_nAttachmentIndex;
	short									m_nSurfaceProp;
	int										m_nMaterial;
	int										m_nDamageType;
	int										m_nHitBox;
	unsigned char							m_nColor;
	bool									m_bCustomColors;
	te_tf_particle_effects_colors_t			m_CustomColors;
	bool									m_bControlPoint1;
	te_tf_particle_effects_control_point_t	m_ControlPoint1;
	int										m_iEffectName;
};

class CParticles
{
public:
	inline void DispatchEffect(const char* pName, const CEffectData& data)
	{
		S::CTE_DispatchEffect.Call<int>(pName, std::ref(data));
	}

	inline int GetParticleSystemIndex(const char* pParticleSystemName)
	{
		return S::Get_ParticleSystemIndex.Call<int>(pParticleSystemName);
	}

	inline void DispatchParticleEffect(int iEffectIndex, const Vec3& vecOrigin, const Vec3& vecStart, const Vec3& vecAngles, CBaseEntity* pEntity = nullptr)
	{
		CEffectData data = {};
		data.m_nHitBox = iEffectIndex;
		data.m_vOrigin = vecOrigin;
		data.m_vStart = vecStart;
		data.m_vAngles = vecAngles;
		data.m_bCustomColors = true;
		if (pEntity)
		{
			data.m_nEntIndex = pEntity->entindex();
			data.m_fFlags |= (1 << 0);
			data.m_nDamageType = 2;
		}
		else
			data.m_nEntIndex = 0;

		DispatchEffect("ParticleEffect", data);
	}

	inline void DispatchParticleEffect(const char* pszParticleName, const Vec3& vecOrigin, const Vec3& vecAngles, CBaseEntity* pEntity = nullptr)
	{
		const int iIndex = GetParticleSystemIndex(pszParticleName);
		DispatchParticleEffect(iIndex, vecOrigin, vecOrigin, vecAngles, pEntity);
	}

	inline void ParticleTracer(const char* pszTracerEffectName, const Vector& vecStart, const Vector& vecEnd, int iEntIndex, int iAttachment, bool bWhiz)
	{
		S::UTIL_ParticleTracer.Call<void>(pszTracerEffectName, std::ref(vecStart), std::ref(vecEnd), iEntIndex, iAttachment, bWhiz);
	}
};

ADD_FEATURE_CUSTOM(CParticles, Particles, H);