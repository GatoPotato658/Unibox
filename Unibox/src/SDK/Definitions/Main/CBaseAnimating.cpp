#include "CBaseAnimating.h"

#include "../../SDK.h"

struct AimStudioHdr
{
	studiohdr_t* m_pStudioHdr;
};

static constexpr int STUDIOHDR_SIZE = 232;
static constexpr int MAX_OVERLAYS = 15;

static studiohdr_t* GetSpyStudiomodel()
{
	static int iModel = -1;
	if (iModel < 0)
		iModel = I::ModelInfoClient->GetModelIndex("models/player/spy.mdl");

	auto pModel = iModel >= 0 ? I::ModelInfoClient->GetModel(iModel) : nullptr;
	if (!pModel)
		return nullptr;

	return I::ModelInfoClient->GetStudiomodel(pModel);
}

CStudioHdr* CBaseAnimating::GetModelPtr()
{
	return S::CBaseAnimating_GetModelPtr.Call<CStudioHdr*, CBaseAnimating*>(this);
}

CStudioHdr* CBaseAnimating::GetAimStudio()
{
	if (!IsPlayer() || As<CTFPlayer>()->m_iClass() != TF_CLASS_SPY)
		return GetModelPtr();

	static AimStudioHdr* pStudio = nullptr;
	static studiohdr_t* pHeader = nullptr;
	static alignas(16) byte aStorage[STUDIOHDR_SIZE] = {};

	auto pSpyHeader = GetSpyStudiomodel();
	if (!pSpyHeader)
		return GetModelPtr();

	auto pDisplay = reinterpret_cast<AimStudioHdr*>(GetModelPtr());
	if (pDisplay && pDisplay->m_pStudioHdr == pSpyHeader)
		return GetModelPtr();

	if (!pStudio || pHeader != pSpyHeader)
	{
		pStudio = reinterpret_cast<AimStudioHdr*>(aStorage);
		pStudio = S::CStudioHdr_Constructor.Call<AimStudioHdr*, AimStudioHdr*, studiohdr_t*, IMDLCache*>(
			pStudio, pSpyHeader, I::MDLCache);
		pHeader = pSpyHeader;
	}

	return reinterpret_cast<CStudioHdr*>(pStudio);
}

static void QuaternionMatrix(const Quaternion& q, const Vec3& vOrigin, matrix3x4& mMatrix)
{
	mMatrix[0][0] = 1.f - 2.f * q.y * q.y - 2.f * q.z * q.z;
	mMatrix[0][1] = 2.f * q.x * q.y + 2.f * q.w * q.z;
	mMatrix[0][2] = 2.f * q.x * q.z - 2.f * q.w * q.y;
	mMatrix[1][0] = 2.f * q.x * q.y - 2.f * q.w * q.z;
	mMatrix[1][1] = 1.f - 2.f * q.x * q.x - 2.f * q.z * q.z;
	mMatrix[1][2] = 2.f * q.y * q.z + 2.f * q.w * q.x;
	mMatrix[2][0] = 2.f * q.x * q.z + 2.f * q.w * q.y;
	mMatrix[2][1] = 2.f * q.y * q.z - 2.f * q.w * q.x;
	mMatrix[2][2] = 1.f - 2.f * q.x * q.x - 2.f * q.y * q.y;
	mMatrix[0][3] = vOrigin.x;
	mMatrix[1][3] = vOrigin.y;
	mMatrix[2][3] = vOrigin.z;
}

static int MapAimSequence(CStudioHdr* pSource, CStudioHdr* pTarget, int iSequence)
{
	if (!pSource || !pTarget || iSequence < 0)
		return iSequence;

	const int iSourceCount = S::CStudioHdr_GetNumSeq.Call<int, CStudioHdr*>(pSource);
	if (iSequence >= iSourceCount)
		return iSequence;

	auto pDescription = S::CStudioHdr_pSeqdesc.Call<const mstudioseqdesc_t*, CStudioHdr*, int>(pSource, iSequence);
	if (!pDescription || !pDescription->pszLabel())
		return iSequence;

	const int iMapped = S::CStudioHdr_LookupSequence.Call<int, CStudioHdr*, const char*>(
		pTarget, pDescription->pszLabel());
	return iMapped >= 0 ? iMapped : iSequence;
}

bool CBaseAnimating::SetupAimBones(matrix3x4* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
	if (!IsPlayer() || As<CTFPlayer>()->m_iClass() != TF_CLASS_SPY)
		return SetupBones(pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	CMDLCacheCriticalSection tLock(I::MDLCache);

	auto pSpyStudio = GetAimStudio();
	auto pStudio = reinterpret_cast<AimStudioHdr*>(pSpyStudio);
	if (!pStudio || !pStudio->m_pStudioHdr || !pBoneToWorldOut)
		return false;

	auto pHeader = pStudio->m_pStudioHdr;
	auto pSourceStudio = GetModelPtr();
	if (!S::CStudioHdr_SequencesAvailable.Call<bool, CStudioHdr*>(pSpyStudio) ||
		m_nSequence() < 0 || pHeader->numbones <= 0 || pHeader->numbones > MAXSTUDIOBONES ||
		nMaxBones < pHeader->numbones)
		return false;

	const int iSequence = MapAimSequence(pSourceStudio, pSpyStudio, m_nSequence());

	Vector aPos[MAXSTUDIOBONES] = {};
	Quaternion aQuat[MAXSTUDIOBONES] = {};
	const float* pPoseParameters = m_flPoseParameter().data();
	BoneSetupState tState = { pSpyStudio, boneMask, pPoseParameters, nullptr };

	S::BoneSetup_InitPose.Call<void, CStudioHdr*, Vector*, Quaternion*, int>(
		pSpyStudio, aPos, aQuat, boneMask);
	S::BoneSetup_AccumulatePose.Call<void, BoneSetupState*, Vector*, Quaternion*, int, float, float, float, void*>(
		&tState, aPos, aQuat, iSequence, m_flCycle(), 1.f, currentTime, nullptr);

	int aSorted[MAX_OVERLAYS];
	for (int i = 0; i < MAX_OVERLAYS; i++)
		aSorted[i] = -1;

	auto& tOverlays = m_AnimOverlay();
	const int iLayerCount = std::min(tOverlays.Count(), MAX_OVERLAYS);
	for (int i = 0; i < iLayerCount; i++)
	{
		const auto& tLayer = tOverlays[i];
		if (tLayer.m_flWeight > 0.f && tLayer.m_nSequence >= 0 &&
			tLayer.m_nOrder >= 0 && tLayer.m_nOrder < iLayerCount)
			aSorted[tLayer.m_nOrder] = i;
	}

	for (int i = 0; i < iLayerCount; i++)
	{
		if (aSorted[i] < 0)
			continue;

		const auto& tLayer = tOverlays[aSorted[i]];
		const int iLayerSequence = MapAimSequence(pSourceStudio, pSpyStudio, tLayer.m_nSequence);
		S::BoneSetup_AccumulatePose.Call<void, BoneSetupState*, Vector*, Quaternion*, int, float, float, float, void*>(
			&tState, aPos, aQuat, iLayerSequence, tLayer.m_flCycle, tLayer.m_flWeight, currentTime, nullptr);
	}

	BoneSetupState* pState = &tState;
	S::BoneSetup_CalcAutoplaySequences.Call<void, BoneSetupState**, Vector*, Quaternion*, float, void*>(
		&pState, aPos, aQuat, currentTime, nullptr);

	if (pHeader->numbonecontrollers > 0)
	{
		S::BoneSetup_CalcBoneAdj.Call<void, CStudioHdr*, Vector*, Quaternion*, const float*, int>(
			pSpyStudio, aPos, aQuat, reinterpret_cast<const float*>(&m_flEncodedController()), boneMask);
	}

	matrix3x4 mRoot;
	Math::AngleMatrix(m_angRotation(), mRoot);
	mRoot[0][3] = m_vecOrigin().x;
	mRoot[1][3] = m_vecOrigin().y;
	mRoot[2][3] = m_vecOrigin().z;

	const float flScale = m_flModelScale();
	if (flScale < 0.99999988f || flScale > 1.0000001f)
	{
		for (int iRow = 0; iRow < 3; iRow++)
		{
			mRoot[iRow][0] *= flScale;
			mRoot[iRow][1] *= flScale;
			mRoot[iRow][2] *= flScale;
		}
	}

	matrix3x4 mLocal;
	for (int i = 0; i < pHeader->numbones; i++)
	{
		auto pBone = pHeader->pBone(i);
		if (!(pBone->flags & boneMask))
			continue;

		QuaternionMatrix(aQuat[i], aPos[i], mLocal);
		if (pBone->parent < 0)
			Math::ConcatTransforms(mRoot, mLocal, pBoneToWorldOut[i]);
		else
			Math::ConcatTransforms(pBoneToWorldOut[pBone->parent], mLocal, pBoneToWorldOut[i]);
	}

	return true;
}

mstudiohitboxset_t* CBaseAnimating::GetHitboxSet()
{
	auto pHDR = GetStudiomodel();
	if (!pHDR)
		return nullptr;

	return pHDR->pHitboxSet(m_nHitboxSet());
}

mstudiohitboxset_t* CBaseAnimating::GetAimHitboxSet()
{
	auto pHDR = GetStudiomodel();
	if (IsPlayer() && As<CTFPlayer>()->m_iClass() == TF_CLASS_SPY)
	{
		CMDLCacheCriticalSection tLock(I::MDLCache);
		auto pSpyHDR = GetSpyStudiomodel();
		if (pSpyHDR)
			pHDR = pSpyHDR;
	}
	if (!pHDR)
		return nullptr;

	return pHDR->pHitboxSet(m_nHitboxSet());
}

int CBaseAnimating::GetHitboxGroup(int nHitbox)
{
	auto pSet = GetHitboxSet();
	if (!pSet)
		return -1;
	auto pBox = pSet->pHitbox(nHitbox);
	if (!pBox)
		return -1;

	return pBox->group;
}

int CBaseAnimating::GetNumOfHitboxes()
{
	auto pSet = GetHitboxSet();
	if (!pSet)
		return 0;

	return pSet->numhitboxes;
}

Vec3 CBaseAnimating::GetHitboxOrigin(matrix3x4* aBones, int nHitbox, Vec3 vOffset)
{
	auto pSet = GetHitboxSet();
	if (!pSet || pSet->numhitboxes <= nHitbox)
		return {};
	auto pBox = pSet->pHitbox(nHitbox);
	if (!pBox)
		return {};

	Vec3 vOut; Math::VectorTransform(vOffset, aBones[pBox->bone], vOut);
	return vOut;
}

Vec3 CBaseAnimating::GetHitboxCenter(matrix3x4* aBones, int nHitbox, Vec3 vOffset)
{
	auto pSet = GetHitboxSet();
	if (!pSet || pSet->numhitboxes <= nHitbox)
		return {};
	auto pBox = pSet->pHitbox(nHitbox);
	if (!pBox)
		return {};

	Vec3 vOut; Math::VectorTransform((pBox->bbmin + pBox->bbmax) / 2 + vOffset, aBones[pBox->bone], vOut);
	return vOut;
}

void CBaseAnimating::GetHitboxInfo(matrix3x4* aBones, int nHitbox, Vec3* pCenter, Vec3* pMins, Vec3* pMaxs, matrix3x4* pMatrix, Vec3 vOffset)
{
	auto pSet = GetHitboxSet();
	if (!pSet || pSet->numhitboxes <= nHitbox)
		return;
	auto pBox = pSet->pHitbox(nHitbox);
	if (!pBox)
		return;

	if (pMins)
		*pMins = pBox->bbmin;

	if (pMaxs)
		*pMaxs = pBox->bbmax;

	if (pCenter)
		Math::VectorTransform(vOffset, aBones[pBox->bone], *pCenter);

	if (pMatrix)
		memcpy(*pMatrix, aBones[pBox->bone], sizeof(matrix3x4));
}
