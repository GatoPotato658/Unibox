#include "CBaseAnimating.h"

#include "../../SDK.h"

struct AimStudioHdr
{
	studiohdr_t* m_pStudioHdr;
};

struct AimActivityTables_t
{
	acttable_t* m_pClient = nullptr;
	int m_iClient = 0;
	acttable_t* m_pServer = nullptr;
	int m_iServer = 0;
	void* m_pItemDefinition = nullptr;
	int m_iTeam = 0;
	bool m_bWinner = false;
};

struct AimPoseMap_t
{
	std::array<int, MAXSTUDIOPOSEPARAM> m_aSource = {};
	int m_iSourceMoveX = -1, m_iSourceMoveY = -1;
	int m_iTargetMoveX = -1, m_iTargetMoveY = -1;
};

static constexpr int STUDIOHDR_SIZE = 232;
static constexpr int MAX_OVERLAYS = 15;

static struct
{
	alignas(16) byte m_aStorage[STUDIOHDR_SIZE] = {};
	CStudioHdr* m_pStudio = nullptr;
	studiohdr_t* m_pHeader = nullptr;
	std::unordered_map<int, int> m_mSequences = {};
	std::unordered_map<const studiohdr_t*, AimPoseMap_t> m_mPoseParameters = {};
} s_tAimStudio;

static studiohdr_t* GetSpyStudiomodel()
{
	const int iModel = I::ModelInfoClient->GetModelIndex("models/player/spy.mdl");
	auto pModel = iModel >= 0 ? I::ModelInfoClient->GetModel(iModel) : nullptr;
	if (!pModel)
		return nullptr;

	return I::ModelInfoClient->GetStudiomodel(pModel);
}

CStudioHdr* CBaseAnimating::GetModelPtr()
{
	return S::CBaseAnimating_GetModelPtr.Call<CStudioHdr*, CBaseAnimating*>(this);
}

void CBaseAnimating::ResetAimStudio()
{
	if (s_tAimStudio.m_pStudio)
		S::CStudioHdr_Destructor.Call<void, CStudioHdr*>(s_tAimStudio.m_pStudio);
	s_tAimStudio.m_pStudio = nullptr;
	s_tAimStudio.m_pHeader = nullptr;
	s_tAimStudio.m_mSequences.clear();
	s_tAimStudio.m_mPoseParameters.clear();
}

CStudioHdr* CBaseAnimating::GetAimStudio()
{
	if (!IsPlayer() || As<CTFPlayer>()->m_iClass() != TF_CLASS_SPY)
		return GetModelPtr();

	auto pSpyHeader = GetSpyStudiomodel();
	if (!pSpyHeader)
		return GetModelPtr();

	auto pDisplay = reinterpret_cast<AimStudioHdr*>(GetModelPtr());
	if (pDisplay && pDisplay->m_pStudioHdr == pSpyHeader)
		return GetModelPtr();

	if (!s_tAimStudio.m_pStudio || s_tAimStudio.m_pHeader != pSpyHeader)
	{
		ResetAimStudio();
		s_tAimStudio.m_pStudio = S::CStudioHdr_Constructor.Call<CStudioHdr*, void*, studiohdr_t*, IMDLCache*>(
			s_tAimStudio.m_aStorage, pSpyHeader, I::MDLCache);
		s_tAimStudio.m_pHeader = pSpyHeader;
	}

	return s_tAimStudio.m_pStudio;
}

bool CBaseAnimating::UsesAimStudio()
{
	if (!IsPlayer() || As<CTFPlayer>()->m_iClass() != TF_CLASS_SPY)
		return false;

	auto pSourceStudio = GetModelPtr();
	return pSourceStudio && GetAimStudio() != pSourceStudio;
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

static int GetAimActivity(const char* pszName)
{
	return S::ActivityList_IndexForName.Call<int, const char*>(pszName);
}

static AimActivityTables_t GetAimActivityTables(CTFPlayer* pPlayer)
{
	AimActivityTables_t tTables = {};
	tTables.m_iTeam = pPlayer->m_iTeamNum();
	tTables.m_bWinner = pPlayer->InCond(TF_COND_COMPETITIVE_WINNER);

	auto pWeapon = pPlayer->m_hActiveWeapon().Get();
	if (!pWeapon)
		return tTables;

	auto pTFWeapon = pWeapon->As<CTFWeaponBase>();
	tTables.m_pClient = pTFWeapon->ActivityList(tTables.m_iClient);
	G::IgnoreDisguiseShared = pPlayer->m_Shared();
	tTables.m_pServer = pTFWeapon->ActivityList(tTables.m_iServer);
	G::IgnoreDisguiseShared = nullptr;

	auto pItem = reinterpret_cast<void*>(uintptr_t(&pTFWeapon->m_iItemDefinitionIndex()) - 72);
	tTables.m_pItemDefinition = reinterpret_cast<void* (*)(void*)>(U::Memory.RelToAbs(S::CEconItemView_GetStaticData_Call(), 1))(pItem);
	return tTables;
}

static int OverrideAimActivity(int iActivity, const AimActivityTables_t& tTables)
{
	if (!tTables.m_pItemDefinition)
		return iActivity;
	return S::CTFItemDefinition_GetActivityOverride.Call<int, void*, int, int>(tTables.m_pItemDefinition, tTables.m_iTeam, iActivity);
}

static bool IsAimWinnerActivity(int iActivity)
{
	static const int iStandPrimary = GetAimActivity("ACT_MP_STAND_PRIMARY");
	static const int iStandMelee = GetAimActivity("ACT_MP_STAND_MELEE");
	return iActivity == iStandPrimary || iActivity == iStandMelee;
}

static int TranslateAimActivity(int iActivity, const AimActivityTables_t& tTables)
{
	static const int iWinner = GetAimActivity("ACT_MP_COMPETITIVE_WINNERSTATE");
	const bool bClientWinner = tTables.m_bWinner && iActivity == iWinner;

	int iBase = iActivity;
	for (int i = 0; tTables.m_pClient && i < tTables.m_iClient; i++)
	{
		const int iClientActivity = OverrideAimActivity(tTables.m_pClient[i].weaponAct, tTables);
		if (bClientWinner ? IsAimWinnerActivity(iClientActivity) : iClientActivity == iActivity)
		{
			iBase = tTables.m_pClient[i].baseAct;
			break;
		}
	}

	int iServerActivity = iBase;
	for (int i = 0; tTables.m_pServer && i < tTables.m_iServer; i++)
	{
		if (tTables.m_pServer[i].baseAct == iBase)
		{
			iServerActivity = tTables.m_pServer[i].weaponAct;
			break;
		}
	}
	iServerActivity = OverrideAimActivity(iServerActivity, tTables);

	if (tTables.m_bWinner && IsAimWinnerActivity(iServerActivity))
		return iWinner;
	return iServerActivity;
}

static int SelectAimSequence(CStudioHdr* pStudio, int iActivity)
{
	if (iActivity < 0)
		return -1;
	if (auto it = s_tAimStudio.m_mSequences.find(iActivity); it != s_tAimStudio.m_mSequences.end())
		return it->second;

	const int iSequence = S::SelectWeightedSequence.Call<int, CStudioHdr*, int, int>(pStudio, iActivity, -1);
	s_tAimStudio.m_mSequences[iActivity] = iSequence;
	return iSequence;
}

static int MapAimSequence(CStudioHdr* pSource, CStudioHdr* pTarget, int iSequence, const AimActivityTables_t& tTables, bool bMain)
{
	if (!pSource || !pTarget || iSequence < 0)
		return -1;

	int iMapped = -1;
	if (iSequence < S::CStudioHdr_GetNumSeq.Call<int, CStudioHdr*>(pSource))
	{
		auto pDescription = S::CStudioHdr_pSeqdesc.Call<const mstudioseqdesc_t*, CStudioHdr*, int>(pSource, iSequence);
		if (pDescription)
		{
			if (pDescription->activity >= 0)
				iMapped = SelectAimSequence(pTarget, TranslateAimActivity(pDescription->activity, tTables));
			if (iMapped < 0)
				iMapped = S::CStudioHdr_LookupSequence.Call<int, CStudioHdr*, const char*>(pTarget, pDescription->pszLabel());
		}
	}
	if (iMapped < 0 && bMain)
	{
		static const int iStandIdle = GetAimActivity("ACT_MP_STAND_IDLE");
		iMapped = SelectAimSequence(pTarget, TranslateAimActivity(iStandIdle, tTables));
	}
	return iMapped;
}

static const mstudioposeparamdesc_t* GetAimPoseParameter(CStudioHdr* pStudio, int iParameter)
{
	return S::CStudioHdr_pPoseParameter.Call<const mstudioposeparamdesc_t*, CStudioHdr*, int>(pStudio, iParameter);
}

static int GetAimPoseParameterCount(CStudioHdr* pStudio)
{
	return std::min(S::CStudioHdr_GetNumPoseParameters.Call<int, CStudioHdr*>(pStudio), MAXSTUDIOPOSEPARAM);
}

static int FindAimPoseParameter(CStudioHdr* pStudio, const char* pszName)
{
	const int iCount = GetAimPoseParameterCount(pStudio);
	for (int i = 0; i < iCount; i++)
	{
		auto pPose = GetAimPoseParameter(pStudio, i);
		if (pPose && !_stricmp(pPose->pszName(), pszName))
			return i;
	}
	return -1;
}

static float GetAimPoseValue(CStudioHdr* pStudio, int iParameter, float flNormalized)
{
	auto pPose = GetAimPoseParameter(pStudio, iParameter);
	return pPose ? pPose->start + flNormalized * (pPose->end - pPose->start) : 0.f;
}

static float GetAimPoseNormalized(CStudioHdr* pStudio, int iParameter, float flValue)
{
	auto pPose = GetAimPoseParameter(pStudio, iParameter);
	if (!pPose || pPose->end == pPose->start)
		return 0.f;
	return std::clamp((flValue - pPose->start) / (pPose->end - pPose->start), 0.f, 1.f);
}

static float GetAimGroundSpeed(CStudioHdr* pStudio, int iSequence, const float* pPoseParameters)
{
	if (iSequence < 0 || iSequence >= S::CStudioHdr_GetNumSeq.Call<int, CStudioHdr*>(pStudio))
		return 0.f;

	const float flDuration = S::Studio_Duration.Call<float, CStudioHdr*, int, const float*>(pStudio, iSequence, pPoseParameters);
	if (flDuration <= 0.f)
		return 0.f;

	Vec3 vMotion = {};
	S::GetSequenceLinearMotion.Call<void, CStudioHdr*, int, const float*, Vec3*>(pStudio, iSequence, pPoseParameters, &vMotion);
	return vMotion.Length() / flDuration;
}

static const AimPoseMap_t& GetAimPoseMap(CStudioHdr* pSource, CStudioHdr* pTarget)
{
	const studiohdr_t* pSourceHeader = reinterpret_cast<AimStudioHdr*>(pSource)->m_pStudioHdr;
	if (auto it = s_tAimStudio.m_mPoseParameters.find(pSourceHeader); it != s_tAimStudio.m_mPoseParameters.end())
		return it->second;

	AimPoseMap_t tMap = {};
	tMap.m_aSource.fill(-1);
	const int iTargetCount = GetAimPoseParameterCount(pTarget);
	for (int i = 0; i < iTargetCount; i++)
	{
		if (auto pTargetPose = GetAimPoseParameter(pTarget, i))
			tMap.m_aSource[i] = FindAimPoseParameter(pSource, pTargetPose->pszName());
	}
	tMap.m_iSourceMoveX = FindAimPoseParameter(pSource, "move_x");
	tMap.m_iSourceMoveY = FindAimPoseParameter(pSource, "move_y");
	tMap.m_iTargetMoveX = FindAimPoseParameter(pTarget, "move_x");
	tMap.m_iTargetMoveY = FindAimPoseParameter(pTarget, "move_y");
	return s_tAimStudio.m_mPoseParameters.emplace(pSourceHeader, tMap).first->second;
}

static void MapAimPoseParameters(CStudioHdr* pSource, int iSourceSequence, CStudioHdr* pTarget, int iTargetSequence,
	const float* pSourceValues, float flSpeed, float* pTargetValues)
{
	const auto& tMap = GetAimPoseMap(pSource, pTarget);
	const int iTargetCount = GetAimPoseParameterCount(pTarget);

	float aTargetValues[MAXSTUDIOPOSEPARAM] = {};
	for (int i = 0; i < iTargetCount; i++)
	{
		const int j = tMap.m_aSource[i];
		if (j >= 0)
			aTargetValues[i] = GetAimPoseValue(pSource, j, pSourceValues[j]);
	}

	if (tMap.m_iSourceMoveX >= 0 && tMap.m_iSourceMoveY >= 0 && tMap.m_iTargetMoveX >= 0 && tMap.m_iTargetMoveY >= 0)
	{
		const float flMoveX = GetAimPoseValue(pSource, tMap.m_iSourceMoveX, pSourceValues[tMap.m_iSourceMoveX]);
		const float flMoveY = GetAimPoseValue(pSource, tMap.m_iSourceMoveY, pSourceValues[tMap.m_iSourceMoveY]);
		const float flScale = std::max(fabsf(flMoveX), fabsf(flMoveY));
		if (flScale > 0.f)
		{
			const float flUnitX = flMoveX / flScale, flUnitY = flMoveY / flScale;

			float aSourceUnit[MAXSTUDIOPOSEPARAM];
			memcpy(aSourceUnit, pSourceValues, sizeof(aSourceUnit));
			aSourceUnit[tMap.m_iSourceMoveX] = GetAimPoseNormalized(pSource, tMap.m_iSourceMoveX, flUnitX);
			aSourceUnit[tMap.m_iSourceMoveY] = GetAimPoseNormalized(pSource, tMap.m_iSourceMoveY, flUnitY);
			const float flSourceMaxSpeed = GetAimGroundSpeed(pSource, iSourceSequence, aSourceUnit);

			aTargetValues[tMap.m_iTargetMoveX] = flUnitX;
			aTargetValues[tMap.m_iTargetMoveY] = flUnitY;
			float aTargetUnit[MAXSTUDIOPOSEPARAM] = {};
			for (int i = 0; i < iTargetCount; i++)
				aTargetUnit[i] = GetAimPoseNormalized(pTarget, i, aTargetValues[i]);
			const float flTargetMaxSpeed = GetAimGroundSpeed(pTarget, iTargetSequence, aTargetUnit);

			const float flMoveSpeed = flScale < 1.f ? flScale * flSourceMaxSpeed : std::max(flSourceMaxSpeed, flSpeed);
			const float flTargetScale = flTargetMaxSpeed > flMoveSpeed ? flMoveSpeed / flTargetMaxSpeed : 1.f;
			aTargetValues[tMap.m_iTargetMoveX] = flUnitX * flTargetScale;
			aTargetValues[tMap.m_iTargetMoveY] = flUnitY * flTargetScale;
		}
	}

	for (int i = 0; i < iTargetCount; i++)
		pTargetValues[i] = GetAimPoseNormalized(pTarget, i, aTargetValues[i]);
}

bool CBaseAnimating::SetupAimBones(matrix3x4* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
	if (!UsesAimStudio())
		return SetupBones(pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	CMDLCacheCriticalSection tLock(I::MDLCache);

	auto pSpyStudio = GetAimStudio();
	auto pSourceStudio = GetModelPtr();
	auto pStudio = reinterpret_cast<AimStudioHdr*>(pSpyStudio);
	if (!pStudio || !pStudio->m_pStudioHdr || !pSourceStudio || !pBoneToWorldOut)
		return false;

	auto pHeader = pStudio->m_pStudioHdr;
	if (!S::CStudioHdr_SequencesAvailable.Call<bool, CStudioHdr*>(pSpyStudio) ||
		pHeader->numbones <= 0 || pHeader->numbones > MAXSTUDIOBONES ||
		nMaxBones < pHeader->numbones)
		return false;

	auto pPlayer = As<CTFPlayer>();
	const auto tTables = GetAimActivityTables(pPlayer);
	const int iSequence = MapAimSequence(pSourceStudio, pSpyStudio, m_nSequence(), tTables, true);
	if (iSequence < 0)
		return false;

	Vector aPos[MAXSTUDIOBONES] = {};
	Quaternion aQuat[MAXSTUDIOBONES] = {};
	float aPoseParameters[MAXSTUDIOPOSEPARAM] = {};
	MapAimPoseParameters(pSourceStudio, m_nSequence(), pSpyStudio, iSequence, m_flPoseParameter().data(),
		pPlayer->m_vecVelocity().Length2D(), aPoseParameters);
	BoneSetupState tState = { pSpyStudio, boneMask, aPoseParameters, nullptr };

	S::BoneSetup_InitPose.Call<void, CStudioHdr*, Vector*, Quaternion*, int>(
		pSpyStudio, aPos, aQuat, boneMask);
	S::BoneSetup_AccumulatePose.Call<void, BoneSetupState*, Vector*, Quaternion*, int, float, float, float, void*>(
		&tState, aPos, aQuat, iSequence, m_flCycle(), 1.f, currentTime, nullptr);

	int aSorted[MAX_OVERLAYS];
	for (int i = 0; i < MAX_OVERLAYS; i++)
		aSorted[i] = -1;

	auto& tOverlays = m_AnimOverlay();
	const int iLayerCount = tOverlays.Count();
	for (int i = 0; i < iLayerCount; i++)
	{
		const auto& tLayer = tOverlays[i];
		if (tLayer.m_flWeight > 0.f && tLayer.m_nSequence >= 0 &&
			tLayer.m_nOrder >= 0 && tLayer.m_nOrder < MAX_OVERLAYS)
			aSorted[tLayer.m_nOrder] = i;
	}

	for (int i = 0; i < MAX_OVERLAYS; i++)
	{
		if (aSorted[i] < 0)
			continue;

		const auto& tLayer = tOverlays[aSorted[i]];
		const int iLayerSequence = MapAimSequence(pSourceStudio, pSpyStudio, tLayer.m_nSequence, tTables, false);
		if (iLayerSequence < 0)
			continue;

		S::BoneSetup_AccumulatePose.Call<void, BoneSetupState*, Vector*, Quaternion*, int, float, float, float, void*>(
			&tState, aPos, aQuat, iLayerSequence, tLayer.m_flCycle, std::min(tLayer.m_flWeight, 1.f), currentTime, nullptr);
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
		const matrix3x4& mParent = pBone->parent < 0 ? mRoot : pBoneToWorldOut[pBone->parent];
		if (!(pBone->flags & boneMask))
		{
			memcpy(pBoneToWorldOut[i], mParent, sizeof(matrix3x4));
			continue;
		}

		QuaternionMatrix(aQuat[i], aPos[i], mLocal);
		Math::ConcatTransforms(mParent, mLocal, pBoneToWorldOut[i]);
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
