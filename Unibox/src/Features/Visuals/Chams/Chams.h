#pragma once
#include "../../../SDK/SDK.h"
#include "D3DMaterials.h"
#include <vector>

Enum(Model, Visible, Occluded);

struct Group_t;
struct Material_t;

class CChams
{
public:
	struct ActiveLayer_t
	{
		ChamsMaterial_t m_tMaterial = {};
		Material_t* m_pMaterial = nullptr;
		bool m_bInvertCull = false;
		bool m_bBlockOccluded = false;
	};

	struct PendingD3DOccluded_t
	{
		struct PoseEntry_t
		{
			CD3DMaterials::PoseRef m_pPose = {};
			float m_flBlend = 1.f;
		};
		CD3DMaterials::GeometryRef m_pGeometry = {};
		std::vector<PoseEntry_t> m_vPoses = {};
		bool m_bScaleAlpha = false;
	};

	static inline void ConfigureSourceStencil(IMatRenderContext* pRenderContext, bool bOccluded, int iStencilRef, bool bBlockOccluded = false)
	{
		pRenderContext->SetStencilEnable(true);
		pRenderContext->SetStencilCompareFunction(bOccluded ? STENCILCOMPARISONFUNCTION_EQUAL : STENCILCOMPARISONFUNCTION_ALWAYS);
		pRenderContext->SetStencilPassOperation(bOccluded ? STENCILOPERATION_KEEP : STENCILOPERATION_REPLACE);
		pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
		pRenderContext->SetStencilZFailOperation((!bOccluded && bBlockOccluded) ? STENCILOPERATION_REPLACE : STENCILOPERATION_KEEP);
		pRenderContext->SetStencilReferenceValue(bOccluded ? 0 : iStencilRef);
		pRenderContext->SetStencilWriteMask(bOccluded ? 0x0 : 0xFF);
		pRenderContext->SetStencilTestMask(bOccluded ? 0xFF : 0x0);
	}

	static inline D3DStencilState_t MakeD3DStencil(bool bEnable, bool bOccluded, int iStencilRef, bool bBlockOccluded = false)
	{
		if (!bEnable)
			return {};
		if (bOccluded)
			return { true, 3u, 1u, 1u, 1u, 0u, 0x0u, 0xFFu };
		return { true, 8u, 3u, 1u, bBlockOccluded ? 3u : 1u, std::uint32_t(iStencilRef), 0xFFu, 0x0u };
	}

private:
	void Begin();
	void End();
	template <typename FDraw>
	bool RenderViewmodelMaterial(IMatRenderContext* pRenderContext, Group_t* pGroup, MaterialCullMode_t restore, FDraw fDraw);

	void DrawModel(CBaseEntity* pEntity, const std::vector<ActiveLayer_t>& vLayers, IMatRenderContext* pRenderContext, int iModel, int iStencilReference);
	void RenderActiveModel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);
	void DrawSourceLayer(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld, const ActiveLayer_t& tLayer, bool bOccluded, bool bTwoModel, int iStencilRef);

	void RenderFakeAngle(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo);

	struct ChamsInfo_t
	{
		CBaseEntity* m_pEntity;
		Chams_t* m_pChams;
		float m_flDistance;
		int m_iFlags = 0;
	};
	struct PreparedEntity_t
	{
		ChamsInfo_t m_tInfo = {};
		std::vector<ActiveLayer_t> m_vVisible = {};
		std::vector<ActiveLayer_t> m_vOccluded = {};
		std::vector<PendingD3DOccluded_t> m_vPendingD3D = {};
		bool m_bPureD3D = false;
		bool m_bNeedsPhase2Fallback = false;
	};
	std::vector<ChamsInfo_t> m_vEntities = {};
	std::vector<ActiveLayer_t> m_vActiveVisible = {};
	std::vector<ActiveLayer_t> m_vActiveOccluded = {};

	Color_t m_tOriginalColor = {};
	float m_flOriginalBlend = 1.f;
	IMaterial* m_pOriginalMaterial = nullptr;
	OverrideType_t m_iOriginalOverride = OVERRIDE_NORMAL;
	float m_flCurrentDistance = -1.f;

	int m_iFlags = false;
	ChamsMaterial_t m_tCurrentMaterial = {};
	bool m_bOccludedMaterial = false;
	bool m_bViewmodelMaterial = false;
	int m_iViewmodelCullOverride = -1;
	bool m_bCurrentBlockOccluded = false;
	int m_iCurrentCullOverride = 0;

	IMatRenderContext* m_pActiveRenderContext = nullptr;
	int m_iActiveStencilRef = 0;
	int m_iActivePhase = 0;
	bool m_bActiveSingleDraw = false;
	std::vector<PendingD3DOccluded_t>* m_pPendingD3DOccluded = nullptr;
	bool* m_pNeedsPhase2Fallback = nullptr;

public:
	void Reset();
	void Store(CTFPlayer* pLocal);
	void RenderMain();
	void RenderHandler(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);
	bool RenderMaterial(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);

	bool RenderViewmodel(CBaseAnimating* rcx, int flags, int* iReturn);
	bool RenderViewmodel(IVModelRender* pModelRender, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);

	bool m_bRendering = false;

	std::unordered_mapset<int> m_mEntities = {};
};

ADD_FEATURE(CChams, Chams);
